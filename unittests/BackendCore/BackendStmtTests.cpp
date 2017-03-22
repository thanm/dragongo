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
#include "gtest/gtest.h"

using namespace llvm;
using namespace goBackendUnitTests;

namespace {

TEST(BackendStmtTests, TestInitStmt) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Btype *bi64t = be->integer_type(false, 64);
  Location loc;

  // local variable with init
  Bvariable *loc1 = be->local_variable(func, "loc1", bi64t, true, loc);
  Bstatement *is = be->init_statement(func, loc1, mkInt64Const(be, 10));
  ASSERT_TRUE(is != nullptr);
  h.addStmt(is);
  EXPECT_EQ(repr(is), "store i64 10, i64* %loc1");

  // error handling
  Bvariable *loc2 = be->local_variable(func, "loc2", bi64t, true, loc);
  Bstatement *bad = be->init_statement(func, loc2, be->error_expression());
  ASSERT_TRUE(bad != nullptr);
  EXPECT_EQ(bad, be->error_statement());
  Bstatement *notsobad = be->init_statement(func, loc2, nullptr);
  h.addStmt(notsobad);

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendStmtTests, TestAssignmentStmt) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Btype *bi64t = be->integer_type(false, 64);
  Location loc;

  // assign a constant to a variable
  Bvariable *loc1 = h.mkLocal("loc1", bi64t);
  Bexpression *ve1 = be->var_expression(loc1, VE_lvalue, loc);
  Bstatement *as =
      be->assignment_statement(func, ve1, mkInt64Const(be, 123), loc);
  ASSERT_TRUE(as != nullptr);
  h.addStmt(as);

  // assign a variable to a variable
  Bvariable *loc2 = h.mkLocal("loc2", bi64t);
  Bexpression *ve2 = be->var_expression(loc2, VE_lvalue, loc);
  Bexpression *ve3 = be->var_expression(loc1, VE_rvalue, loc);
  Bstatement *as2 = be->assignment_statement(func, ve2, ve3, loc);
  ASSERT_TRUE(as2 != nullptr);
  h.addStmt(as2);

  const char *exp = R"RAW_RESULT(
    store i64 0, i64* %loc1
      store i64 123, i64* %loc1
      store i64 0, i64* %loc2
      %loc1.ld.0 = load i64, i64* %loc1
      store i64 %loc1.ld.0, i64* %loc2
   )RAW_RESULT";
  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  // error handling
  Bvariable *loc3 = h.mkLocal("loc3", bi64t);
  Bexpression *ve4 = be->var_expression(loc3, VE_lvalue, loc);
  Bstatement *badas =
      be->assignment_statement(func, ve4, be->error_expression(), loc);
  ASSERT_TRUE(badas != nullptr);
  EXPECT_EQ(badas, be->error_statement());

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendStmtTests, TestReturnStmt) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *loc1 = h.mkLocal("loc1", bi64t, mkInt64Const(be, 10));

  // return loc1
  Location loc;
  Bexpression *ve1 = be->var_expression(loc1, VE_rvalue, loc);
  Bstatement *ret = h.mkReturn(ve1);

  const char *exp = R"RAW_RESULT(
     %loc1.ld.0 = load i64, i64* %loc1
     ret i64 %loc1.ld.0
   )RAW_RESULT";
  std::string reason;
  bool equal = difftokens(exp, repr(ret), reason);
  EXPECT_EQ("pass", equal ? "pass" : reason);

  // error handling
  std::vector<Bexpression *> bvals;
  bvals.push_back(be->error_expression());
  Bstatement *bret = be->return_statement(func, bvals, loc);
  EXPECT_EQ(bret, be->error_statement());

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendStmtTests, TestLabelGotoStmts) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  // loc1 = 10
  Location loc;
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *loc1 = h.mkLocal("loc1", bi64t, mkInt64Const(be, 10));

  // goto labeln
  Blabel *lab1 = be->label(func, "foolab", loc);
  Bstatement *gots = be->goto_statement(lab1, loc);
  h.addStmt(gots);

  // dead stmt: loc1 = 11
  h.mkAssign(be->var_expression(loc1, VE_lvalue, loc),
             mkInt64Const(be, 11));

  // labeldef
  Bstatement *ldef = be->label_definition_statement(lab1);
  h.addStmt(ldef);

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendStmtTests, TestIfStmt) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Location loc;
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *loc1 = h.mkLocal("loc1", bi64t);

  // loc1 = 123
  Bexpression *ve1 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *c123 = mkInt64Const(be, 123);
  Bstatement *as1 = be->assignment_statement(func, ve1, c123, loc);

  // loc1 = 987
  Bexpression *ve2 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *c987 = mkInt64Const(be, 987);
  Bstatement *as2 = be->assignment_statement(func, ve2, c987, loc);

  // loc1 = 456
  Bexpression *ve3 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *c456 = mkInt64Const(be, 456);
  Bstatement *as3 = be->assignment_statement(func, ve3, c456, loc);

  // if true b1 else b2
  Bexpression *trueval = be->boolean_constant_expression(true);
  Bstatement *ifst = h.mkIf(trueval, as1, as2, FcnTestHarness::NoAppend);

  // if true if
  Bexpression *tv2 = be->boolean_constant_expression(true);
  h.mkIf(tv2, ifst, as3);

  // return 10101
  h.mkReturn(mkInt64Const(be, 10101));

  bool broken = h.finish(StripDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");

  // verify
  const char *exp = R"RAW_RESULT(
    define i64 @foo(i8* nest %nest.0, i32 %param1, i32 %param2, i64* %param3) #0 {
    entry:
      %param1.addr = alloca i32
      %param2.addr = alloca i32
      %param3.addr = alloca i64*
      %loc1 = alloca i64
      store i32 %param1, i32* %param1.addr
      store i32 %param2, i32* %param2.addr
      store i64* %param3, i64** %param3.addr
      store i64 0, i64* %loc1
      br i1 true, label %then.0, label %else.0
    then.0:                                           ; preds = %entry
      br i1 true, label %then.1, label %else.1
    fallthrough.0:                 ; preds = %else.0, %fallthrough.1
      ret i64 10101
    else.0:                                           ; preds = %entry
      store i64 456, i64* %loc1
      br label %fallthrough.0
    then.1:                                           ; preds = %then.0
      store i64 123, i64* %loc1
      br label %fallthrough.1
    fallthrough.1:                             ; preds = %else.1, %then.1
      br label %fallthrough.0
    else.1:                                           ; preds = %then.0
      store i64 987, i64* %loc1
      br label %fallthrough.1
    }
    )RAW_RESULT";

  bool isOK = h.expectValue(func->function(), exp);
  EXPECT_TRUE(isOK && "Function does not have expected contents");
}

TEST(BackendStmtTests, TestSwitchStmt) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Location loc;
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *loc1 = h.mkLocal("loc1", bi64t);

  // loc1 = loc1 / 123
  Bexpression *ve1 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *ve1r = be->var_expression(loc1, VE_rvalue, loc);
  Bexpression *c123 = mkInt64Const(be, 123);
  Bexpression *div = be->binary_expression(OPERATOR_DIV, ve1r, c123, loc);
  Bstatement *as1 = be->assignment_statement(func, ve1, div, loc);

  // loc1 = 987 * loc1
  Bexpression *ve2 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *ve2r = be->var_expression(loc1, VE_rvalue, loc);
  Bexpression *c987 = mkInt64Const(be, 987);
  Bexpression *mul = be->binary_expression(OPERATOR_MULT, c987, ve2r, loc);
  Bstatement *as2 = be->assignment_statement(func, ve2, mul, loc);

  // loc1 = 456
  Bexpression *ve3 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *c456 = mkInt64Const(be, 456);
  Bstatement *as3 = be->assignment_statement(func, ve3, c456, loc);

  // Set up switch statements
  std::vector<Bstatement*> statements = {as1, as2, as3};
  std::vector<std::vector<Bexpression*> > cases = {
    { mkInt64Const(be, 1), mkInt64Const(be, 2) },
    { mkInt64Const(be, 3), mkInt64Const(be, 4) } };
  cases.push_back(std::vector<Bexpression*>()); // default

  // switch
  Bexpression *vesw = be->var_expression(loc1, VE_rvalue, loc);
  h.mkSwitch(vesw, cases, statements);

  // return 10101
  h.mkReturn(mkInt64Const(be, 10101));

  bool broken = h.finish(StripDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");

  // verify
  const char *exp = R"RAW_RESULT(
   define i64 @foo(i8* nest %nest.0, i32 %param1, i32 %param2, i64* %param3) #0 {
   entry:
     %param1.addr = alloca i32
     %param2.addr = alloca i32
     %param3.addr = alloca i64*
     %loc1 = alloca i64
     store i32 %param1, i32* %param1.addr
     store i32 %param2, i32* %param2.addr
     store i64* %param3, i64** %param3.addr
     store i64 0, i64* %loc1
     %loc1.ld.2 = load i64, i64* %loc1
     switch i64 %loc1.ld.2, label %default.0 [
       i64 1, label %case.0
       i64 2, label %case.0
       i64 3, label %case.1
       i64 4, label %case.1
     ]
   case.0:                               ; preds = %entry, %entry
     %loc1.ld.0 = load i64, i64* %loc1
     %div.0 = sdiv i64 %loc1.ld.0, 123
     store i64 %div.0, i64* %loc1
     br label %epilog.0
   case.1:                               ; preds = %entry, %entry
     %loc1.ld.1 = load i64, i64* %loc1
     %mul.0 = mul i64 987, %loc1.ld.1
     store i64 %mul.0, i64* %loc1
     br label %epilog.0
   default.0:                            ; preds = %entry
     store i64 456, i64* %loc1
     br label %epilog.0
   epilog.0:                             ; preds = %default.0, %case.1, %case.0
     ret i64 10101
   }
    )RAW_RESULT";

  bool isOK = h.expectValue(func->function(), exp);
  EXPECT_TRUE(isOK && "Function does not have expected contents");
}

}
