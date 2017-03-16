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

TEST(BackendFcnTests, MakeEmptyFunction) {

  // Create empty function
  FcnTestHarness h;
  Llvm_backend *be = h.be();
  BFunctionType *befty1 = mkFuncTyp(be, L_END);
  h.mkFunction("foo", befty1);

  const char *exp = R"RAW_RESULT(
    )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendFcnTests, MakeFuncWithLotsOfArgs) {

  // Create empty function
  FcnTestHarness h;
  Llvm_backend *be = h.be();
  Btype *bi32t = be->integer_type(false, 32);
  Btype *bi64t = be->integer_type(false, 64);
  Bexpression *val10 = mkInt64Const(be, int64_t(10));
  Btype *at10 = be->array_type(bi64t, val10);
  Btype *st3 = mkBackendThreeFieldStruct(be);
  BFunctionType *befty1 = mkFuncTyp(be,
                                    L_RCV, st3,
                                    L_PARM, at10,
                                    L_PARM, be->bool_type(),
                                    L_PARM, bi32t,
                                    L_PARM, bi64t,
                                    L_PARM, be->pointer_type(st3),
                                    L_RES, bi32t,
                                    L_RES, bi64t,
                                    L_END);
  h.mkFunction("foo", befty1);

  const char *exp = R"RAW_RESULT(
    )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendFcnTests, MakeFunction) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  Btype *bi64t = be->integer_type(false, 64);
  Btype *bi32t = be->integer_type(false, 32);

  // func foo(i1, i2 int32) int64 { }
  BFunctionType *befty =
      mkFuncTyp(be.get(), L_PARM, bi32t, L_PARM, bi32t, L_RES, bi64t, L_END);

  // FIXME: this is not supported yet.
  bool in_unique_section = false;

  const bool is_declaration = true;
  const bool is_visible[2] = {true, false};
  const bool is_inlinable[2] = {true, false};
  bool split_stack[2] = {true, false};
  Location loc;
  unsigned count = 0;
  for (auto vis : is_visible) {
    for (auto inl : is_inlinable) {
      for (auto split : split_stack) {
        std::stringstream ss;
        ss << "fcn" << count++;
        Bfunction *befcn =
            be->function(befty, "_foo", ss.str(), vis, is_declaration,
                         inl, split, in_unique_section, loc);
        llvm::Function *llfunc = befcn->function();
        ASSERT_TRUE(llfunc != NULL);
        EXPECT_EQ(llfunc->getName(), ss.str());
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

    // builtin variant
    char nbuf[128];
    sprintf(nbuf, "__builtin_%s", fname.c_str());
    Bfunction *bifcn = be->lookup_builtin(nbuf);
    EXPECT_TRUE(bifcn != NULL);
    EXPECT_TRUE(bifcn == bfcn);
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
}

TEST(BackendFcnTests, MakeFuncWithRecursiveTypeParam) {

  // Create empty function
  FcnTestHarness h;
  Llvm_backend *be = h.be();
  Location loc;

  // type P *P
  Btype *cpht = be->placeholder_pointer_type("ph", loc, false);
  Btype *cpt = be->circular_pointer_type(cpht, false);
  be->set_placeholder_pointer_type(cpht, cpt);

  // struct A { f2 bool, fn *A }
  Btype *php = be->placeholder_pointer_type("ph", loc, false);
  std::vector<Backend::Btyped_identifier> fields = {
      Backend::Btyped_identifier("f1", be->bool_type(), Location()),
      Backend::Btyped_identifier("fn", php, Location())
  };
  Btype *bst = be->struct_type(fields);
  Btype *bpst = be->pointer_type(bst);
  be->set_placeholder_pointer_type(php, bpst);
  Btype *bi64t = be->integer_type(false, 64);
  BFunctionType *befty1 = mkFuncTyp(be,
                                    L_RCV, php,
                                    L_PARM, cpht,
                                    L_PARM, bpst,
                                    L_PARM, be->bool_type(),
                                    L_PARM, bst,
                                    L_RES, bi64t,
                                    L_END);
  h.mkFunction("foo", befty1);

  const char *exp = R"RAW_RESULT(
    )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendFcnTests, MakeMultipleDeclarations) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Location loc;

  // If a function of a given name is declared more than once,
  // we expect to get back the original decl on the second time.
  // For definitions, a new function will be created each time (although
  // this could certainly be changed if needed);
  Btype *bi32t = be->integer_type(false, 32);
  Btype *bi64t = be->integer_type(false, 64);
  BFunctionType *befty1 = mkFuncTyp(be, L_RES, bi32t, L_PARM, bi64t, L_END);
  bool is_visible = false;
  bool is_declaration = true;
  bool is_inl = true;
  bool is_splitstack = true;
  bool in_unique_section = false;
  Bfunction *bf1 =
      be->function(befty1, "_foo", "bar", is_visible, is_declaration,
                         is_inl, is_splitstack, in_unique_section, loc);
  Bfunction *bf2 =
      be->function(befty1, "_foo", "bar", is_visible, is_declaration,
                         is_inl, is_splitstack, in_unique_section, loc);
  Bfunction *bf3 =
      be->function(befty1, "_foo", "bar", is_visible, !is_declaration,
                         is_inl, is_splitstack, in_unique_section, loc);
  EXPECT_EQ(bf1, bf2);
  EXPECT_NE(bf1, bf3);

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

}
