//===- llvm/tools/dragongo/unittests/BackendCore/BackendFcnTests.cpp ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TestUtils.h"
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

  const bool is_visible = true;
  const bool is_declaration = true;
  const bool is_inlinable = false;
  bool disable_split_stack = true;
  bool in_unique_section = false;
  Location loc;
  Bfunction *befcn = be->function(befty, "foo", "foo", is_visible,
                                  is_declaration, is_inlinable,
                                  disable_split_stack, in_unique_section, loc);
  ASSERT_TRUE(befcn != NULL);
}

}
