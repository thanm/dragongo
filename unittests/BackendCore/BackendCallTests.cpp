//===- llvm/tools/dragongo/unittests/BackendCore/BackendCallTests.cpp ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TestUtils.h"
#include "go-llvm-backend.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace goBackendUnitTests;

namespace {

TEST(BackendCallTests, TestSimpleCall) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();
  Location loc;

  Btype *bi64t = be->integer_type(false, 64);
  Btype *bpi64t = be->pointer_type(bi64t);
  Bexpression *fn = be->function_code_expression(func, loc);
  std::vector<Bexpression *> args;
  args.push_back(mkInt32Const(be, int64_t(3)));
  args.push_back(mkInt32Const(be, int64_t(6)));
  args.push_back(be->zero_expression(bpi64t));
  Bexpression *call = be->call_expression(fn, args, nullptr, loc);
  Bvariable *x = h.mkLocal("x", bi64t, call);
  h.mkReturn(be->var_expression(x, VE_rvalue, loc));


  const char *exp = R"RAW_RESULT(
      %call.0 = call i64 @foo(i32 3, i32 6, i64* null)
      store i64 %call.0, i64* %x
      %x.ld.0 = load i64, i64* %x
      ret i64 %x.ld.0
    )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

}
