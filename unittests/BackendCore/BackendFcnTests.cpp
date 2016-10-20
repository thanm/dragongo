//===- llvm/tools/dragongo/unittests/BackendCore/BackendFcnTests.cpp ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TestUtils.h"
#include "llvm/IR/Function.h"
#include "gtest/gtest.h"
#include "go-llvm-backend.h"

using namespace llvm;

namespace {

TEST(BackendFcnTests, MakeFunction) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  Btype *bi64t = be->integer_type(false, 64);
  Btype *bi32t = be->integer_type(false, 32);

  // func foo(i1, i2 int32) int64 { }
  Btype *befty = mkFuncTyp(be.get(),
                            L_PARM, bi32t,
                            L_PARM, bi32t,
                            L_RES, bi64t,
                            L_END);

  const bool is_declaration = true;
  const bool is_visible[2] = { true, false };
  const bool is_inlinable[2] = { true, false };
  bool split_stack[2] = { true, false };
  bool in_unique_section = false;
  Location loc;
  bool first = true;
  for (auto vis : is_visible) {
    for (auto inl : is_inlinable) {
      for (auto split : split_stack) {
        Bfunction *befcn =
            be->function(befty, "foo", "foo", vis,
                         is_declaration, inl, split, in_unique_section, loc);
        llvm::Function *llfunc = befcn->function();
        ASSERT_TRUE(llfunc != NULL);
        if (first) {
          ASSERT_EQ(llfunc->getName(), "foo");
          first = false;
        }
        ASSERT_FALSE(llfunc->isVarArg());
        ASSERT_EQ(llfunc->hasFnAttribute(Attribute::NoInline), !inl);
        ASSERT_EQ(llfunc->hasHiddenVisibility(), !vis);
        ASSERT_EQ(befcn->splitStack() == Bfunction::YesSplit, !split);
      }
    }
  }
}

}
