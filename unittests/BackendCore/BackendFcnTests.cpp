//===- llvm/tools/dragongo/unittests/BackendCore/BackendFcnTests.cpp ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TestUtils.h"
#include "go-llvm-backend.h"
#include "llvm/IR/Function.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace goBackendUnitTests;

namespace {

TEST(BackendFcnTests, MakeFunction) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  Btype *bi64t = be->integer_type(false, 64);
  Btype *bi32t = be->integer_type(false, 32);

  // func foo(i1, i2 int32) int64 { }
  Btype *befty =
      mkFuncTyp(be.get(), L_PARM, bi32t, L_PARM, bi32t, L_RES, bi64t, L_END);

  // FIXME: this is not supported yet.
  bool in_unique_section = false;

  const bool is_declaration = true;
  const bool is_visible[2] = {true, false};
  const bool is_inlinable[2] = {true, false};
  bool split_stack[2] = {true, false};
  Location loc;
  bool first = true;
  for (auto vis : is_visible) {
    for (auto inl : is_inlinable) {
      for (auto split : split_stack) {
        Bfunction *befcn =
            be->function(befty, "foo", "foo", vis, is_declaration, inl, split,
                         in_unique_section, loc);
        llvm::Function *llfunc = befcn->function();
        ASSERT_TRUE(llfunc != NULL);
        if (first) {
          EXPECT_EQ(llfunc->getName(), "foo");
          first = false;
        }
        EXPECT_FALSE(llfunc->isVarArg());
        EXPECT_EQ(llfunc->hasFnAttribute(Attribute::NoInline), !inl);
        EXPECT_EQ(llfunc->hasHiddenVisibility(), !vis);
        EXPECT_EQ(befcn->splitStack() == Bfunction::YesSplit, !split);
      }
    }
  }

  // Error function
  Bfunction *be_error_fcn = be->error_function();
  ASSERT_TRUE(be_error_fcn != NULL);

  // Try to create a function with an error type -- we should
  // get back error_function
  Bfunction *mistake = be->function(be->error_type(), "bad", "bad", true, true,
                                    false, false, false, loc);
  EXPECT_EQ(mistake, be_error_fcn);
}

TEST(BackendFcnTests, BuiltinFunctionsMisc) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  std::unordered_set<Bfunction *> results;
  std::vector<std::string> tocheck = {
      "__sync_fetch_and_add_1",  "__sync_fetch_and_add_2",
      "__sync_fetch_and_add_4",  "__sync_fetch_and_add_8",
      "__builtin_trap",          "__builtin_expect",
      "__builtin_memcmp",        "__builtin_ctz",
      "__builtin_ctzll",         "__builtin_bswap32",
      "__builtin_bswap64",       "__builtin_return_address",
      "__builtin_frame_address",
  };
  for (auto fname : tocheck) {
    Bfunction *bfcn = be->lookup_builtin(fname);
    ASSERT_TRUE(bfcn != NULL);
    EXPECT_TRUE(results.find(bfcn) == results.end());
    results.insert(bfcn);
  }
  EXPECT_TRUE(results.size() == tocheck.size());
}

TEST(BackendFcnTests, BuiltinFunctionsTrig) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  std::unordered_set<Bfunction *> results;
  std::vector<std::string> tocheck = {
      "acos",  "asin", "atan",  "atan2", "ceil",  "cos",   "exp",
      "expm1", "fabs", "floor", "fmod",  "log",   "log1p", "log10",
      "log2",  "sin",  "sqrt",  "tan",   "trunc", "ldexp",
  };
  for (auto fname : tocheck) {

    // function
    Bfunction *bfcn = be->lookup_builtin(fname);
    ASSERT_TRUE(bfcn != NULL);
    EXPECT_TRUE(results.find(bfcn) == results.end());
    results.insert(bfcn);

    // long variant
    char nbuf[128];
    sprintf(nbuf, "%sl", fname.c_str());
    Bfunction *lbfcn = be->lookup_builtin(nbuf);
    EXPECT_TRUE(lbfcn != NULL);
    EXPECT_TRUE(lbfcn != bfcn);

    // builtin variant
    sprintf(nbuf, "__builtin_%s", fname.c_str());
    Bfunction *bifcn = be->lookup_builtin(nbuf);
    EXPECT_TRUE(bifcn != NULL);
    EXPECT_TRUE(bifcn != bfcn);

    // long builtin variant
    sprintf(nbuf, "__builtin_%sl", fname.c_str());
    Bfunction *lbifcn = be->lookup_builtin(nbuf);
    EXPECT_TRUE(lbifcn != NULL);
    EXPECT_TRUE(lbifcn != bfcn);
  }
  EXPECT_TRUE(results.size() == tocheck.size());
}

TEST(BackendFcnTests, MakeBlocks) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));
  Bfunction *bfcn = mkFunci32o64(be.get(), "foo");
  const std::vector<Bvariable *> vars;
  Bblock *bb = be->block(bfcn, nullptr, vars, Location(), Location());
  ASSERT_TRUE(bb != nullptr);
  delete bb;
}
}
