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
  Bvariable *loc2 = be->local_variable(func, "loc1", bi64t, true, loc);
  Bstatement *bad = be->init_statement(func, loc2, be->error_expression());
  ASSERT_TRUE(bad != nullptr);
  EXPECT_EQ(bad, be->error_statement());

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendStmtTests, TestAssignmentStmt) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Btype *bi64t = be->integer_type(false, 64);
  Location loc;

  // assign a constant to a variable
  Bvariable *loc1 = be->local_variable(func, "loc1", bi64t, true, loc);
  Bexpression *ve1 = be->var_expression(loc1, VE_lvalue, loc);
  Bstatement *as =
      be->assignment_statement(func, ve1, mkInt64Const(be, 123), loc);
  ASSERT_TRUE(as != nullptr);
  h.addStmt(as);

  // assign a variable to a variable
  Bvariable *loc2 = be->local_variable(func, "loc2", bi64t, true, loc);
  Bexpression *ve2 = be->var_expression(loc2, VE_lvalue, loc);
  Bexpression *ve3 = be->var_expression(loc1, VE_rvalue, loc);
  Bstatement *as2 = be->assignment_statement(func, ve2, ve3, loc);
  ASSERT_TRUE(as2 != nullptr);
  h.addStmt(as2);

  const char *exp = R"RAW_RESULT(
            store i64 123, i64* %loc1
            %loc1.ld.0 = load i64, i64* %loc1
            store i64 %loc1.ld.0, i64* %loc2
   )RAW_RESULT";
  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  // error handling
  Bvariable *loc3 = be->local_variable(func, "loc3", bi64t, true, loc);
  Bexpression *ve4 = be->var_expression(loc3, VE_lvalue, loc);
  Bstatement *badas =
      be->assignment_statement(func, ve4, be->error_expression(), loc);
  ASSERT_TRUE(badas != nullptr);
  EXPECT_EQ(badas, be->error_statement());

  bool broken = h.finish();
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

  bool broken = h.finish();
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

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendStmtTests, TestIfStmt) {
  LLVMContext C;
  std::unique_ptr<Llvm_backend> be(new Llvm_backend(C, nullptr));

  Location loc;
  Btype *bi64t = be->integer_type(false, 64);
  Bfunction *func = mkFunci32o64(be.get(), "foo");
  Bvariable *loc1 = be->local_variable(func, "loc1", bi64t, true, loc);

  // loc1 = 123
  Bexpression *ve1 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *c123 = mkInt64Const(be.get(), 123);
  Bstatement *as1 = be->assignment_statement(func, ve1, c123, loc);
  Bblock *b1 = mkBlockFromStmt(be.get(), func, as1);

  // loc1 = 987
  Bexpression *ve2 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *c987 = mkInt64Const(be.get(), 987);
  Bstatement *as2 = be->assignment_statement(func, ve2, c987, loc);
  Bblock *b2 = mkBlockFromStmt(be.get(), func, as2);

  // loc1 = 456
  Bexpression *ve3 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *c456 = mkInt64Const(be.get(), 456);
  Bstatement *as3 = be->assignment_statement(func, ve3, c456, loc);
  Bblock *b3 = mkBlockFromStmt(be.get(), func, as3);

  // if true b1 else b2
  Bexpression *trueval = be->boolean_constant_expression(true);
  Bstatement *ifst = be->if_statement(func, trueval, b1, b2, Location());
  Bblock *ib = mkBlockFromStmt(be.get(), func, ifst);

  // if true if
  Bexpression *tv2 = be->boolean_constant_expression(true);
  Bstatement *ifst2 = be->if_statement(func, tv2, ib, b3, Location());
  Bblock *eb = mkBlockFromStmt(be.get(), func, ifst2);

  std::vector<Bexpression *> vals;
  vals.push_back(mkInt64Const(be.get(), 10101));
  Bstatement *ret = be->return_statement(func, vals, loc);
  addStmtToBlock(be.get(), eb, ret);

  // set function body
  bool ok = be->function_set_body(func, eb);
  EXPECT_TRUE(ok);

  bool broken = llvm::verifyModule(be->module(), &dbgs());
  EXPECT_FALSE(broken && "Module failed to verify.");
}

}
