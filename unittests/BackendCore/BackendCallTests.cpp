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

TEST(BackendCallTests, CallToVoid) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Location loc;

  // Declare a function bar with no args and no return.
  Btype *befty = mkFuncTyp(be, L_END);
  bool is_decl = true; bool is_inl = false;
  bool is_vis = true; bool is_split = true;
  Bfunction *befcn = be->function(befty, "bar", "bar",
                                  is_vis, is_decl, is_inl, is_split,
                                  false, loc);

  // Create call to it
  Bexpression *fn = be->function_code_expression(befcn, loc);
  std::vector<Bexpression *> args;
  Bexpression *call = be->call_expression(fn, args, nullptr, loc);
  h.mkExprStmt(call);

  const char *exp = R"RAW_RESULT(
     call void @bar()
    )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendCallTests, MultiReturnCall) {

  FcnTestHarness h;
  Llvm_backend *be = h.be();

  // Create function with multiple returns
  Btype *bi64t = be->integer_type(false, 64);
  Btype *bi32t = be->integer_type(false, 32);
  Btype *bi8t = be->integer_type(false, 8);
  BFunctionType *befty1 = mkFuncTyp(be,
                            L_PARM, be->pointer_type(bi8t),
                            L_RES, be->pointer_type(bi8t),
                            L_RES, be->pointer_type(bi32t),
                            L_RES, be->pointer_type(bi64t),
                            L_RES, bi64t,
                            L_END);
  Bfunction *func = h.mkFunction("foo", befty1);

  // Emit a suitable return suitable for "foo" as declared above.
  // This returns a constant expression.
  std::vector<Bexpression *> rvals = {
    be->nil_pointer_expression(),
    be->nil_pointer_expression(),
    be->nil_pointer_expression(),
    mkInt64Const(be, 101) };
  Bstatement *s1 = h.mkReturn(rvals, FcnTestHarness::NoAppend);

  {
    const char *exp = R"RAW_RESULT(
     ret { i8*, i32*, i64*, i64 } { i8* null, i32* null, i64* null, i64 101 }
    )RAW_RESULT";

    bool isOK = h.expectStmt(s1, exp);
    EXPECT_TRUE(isOK && "First return stmt does not have expected contents");
  }

  // This is intended to be something like
  //
  //  return p8, nil, nil, 101
  //
  Bvariable *p1 = func->getBvarForValue(func->getNthArgValue(0));
  Bexpression *vex = be->var_expression(p1, VE_rvalue, Location());
  std::vector<Bexpression *> rvals2 = {
    vex,
    be->nil_pointer_expression(),
    be->nil_pointer_expression(),
    mkInt64Const(be, 101) };
  Bstatement *s2 = h.mkReturn(rvals2, FcnTestHarness::NoAppend);

  {
    const char *exp = R"RAW_RESULT(
%p0.ld.0 = load i8*, i8** %p0.addr
  %field.0 = getelementptr inbounds { i8*, i32*, i64*, i64 }, { i8*, i32*, i64*, i64 }* %tmp.0, i32 0, i32 0
  store i8* %p0.ld.0, i8** %field.0
  %field.1 = getelementptr inbounds { i8*, i32*, i64*, i64 }, { i8*, i32*, i64*, i64 }* %tmp.0, i32 0, i32 1
  store i32* null, i32** %field.1
  %field.2 = getelementptr inbounds { i8*, i32*, i64*, i64 }, { i8*, i32*, i64*, i64 }* %tmp.0, i32 0, i32 2
  store i64* null, i64** %field.2
  %field.3 = getelementptr inbounds { i8*, i32*, i64*, i64 }, { i8*, i32*, i64*, i64 }* %tmp.0, i32 0, i32 3
  store i64 101, i64* %field.3
  %.ld.0 = load { i8*, i32*, i64*, i64 }, { i8*, i32*, i64*, i64 }* %tmp.0
  ret { i8*, i32*, i64*, i64 } %.ld.0
    )RAW_RESULT";

    bool isOK = h.expectStmt(s2, exp);
    EXPECT_TRUE(isOK && "Second return stmt does not have expected contents");
  }

  // If statement
  Location loc;
  Bexpression *ve2 = be->var_expression(p1, VE_rvalue, Location());
  Bexpression *npe = be->nil_pointer_expression();
  Bexpression *cmp = be->binary_expression(OPERATOR_EQEQ, ve2, npe, loc);
  h.mkIf(cmp, s1, s2);

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

}
