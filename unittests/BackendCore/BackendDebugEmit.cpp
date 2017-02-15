//===- llvm/tools/dragongo/unittests/BackendCore/BackendDebugEmit.cpp -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TestUtils.h"
#include "go-llvm-backend.h"
#include "gtest/gtest.h"

using namespace goBackendUnitTests;

namespace {

// Insure that dbg.declare is emitted for a used-defined local
// variable. Remark: I worry that this unit test may be too brittle
// (vulnerable to spurious failures if other things in the bridge are
// changed). Perhaps there is some other way to verify this
// functionality.

TEST(BackendDebugEmit, TestSimpleDecl) {
  FcnTestHarness h;
  Llvm_backend *be = h.be();
  BFunctionType *befty = mkFuncTyp(be, L_END);
  Bfunction *func = h.mkFunction("foo", befty);

  Btype *bu32t = be->integer_type(true, 32);
  h.mkLocal("x", bu32t);

  const char *exp = R"RAW_RESULT(
      define void @foo() #0 {
      entry:
        %x = alloca i32
        store i32 0, i32* %x
        call void @llvm.dbg.declare(metadata i32* %x, metadata !3,
                                    metadata !11), !dbg !12
        ret void
      }
  )RAW_RESULT";

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");

  bool isOK = h.expectValue(func->function(), exp);
  EXPECT_TRUE(isOK && "Function does not have expected contents");
}

}
