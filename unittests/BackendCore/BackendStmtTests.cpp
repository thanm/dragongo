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

TEST(BackendStmtTests, TestInitStmt) {
  LLVMContext C;
  std::unique_ptr<Backend> be(go_get_backend(C));

  // func:  foo(i1, i2 int32) int64 { }
  Bfunction *func = mkFunci32o64(be.get(), "foo");
  Btype *bi64t = be->integer_type(false, 64);
  Location loc;

  // local variable with init
  Bvariable *loc1 = be->local_variable(func, "loc1", bi64t, true, loc);
  Bstatement *is = be->init_statement(loc1, mkInt64Const(be.get(), 10));
  Bblock *block = mkBlockFromStmt(be.get(), func, is);

  ASSERT_TRUE(is != nullptr);
  EXPECT_EQ(repr(is), "store i64 10, i64* %loc1");

  // error handling
  Bvariable *loc2 = be->local_variable(func, "loc1", bi64t, true, loc);
  Bstatement *bad = be->init_statement(loc2, be->error_expression());
  ASSERT_TRUE(bad != nullptr);
  EXPECT_EQ(bad, be->error_statement());

  be->function_set_body(func, block);
}

TEST(BackendStmtTests, TestAssignmentStmt) {
  LLVMContext C;
  std::unique_ptr<Backend> be(go_get_backend(C));

  // func:  foo(i1, i2 int32) int64 { }
  Bfunction *func = mkFunci32o64(be.get(), "foo");
  Btype *bi64t = be->integer_type(false, 64);
  Location loc;

  // assign a constant to a variable
  Bvariable *loc1 = be->local_variable(func, "loc1", bi64t, true, loc);
  Bexpression *ve1 = be->var_expression(loc1, true, loc);
  Bstatement *as = be->assignment_statement(ve1,
                                            mkInt64Const(be.get(), 123), loc);
  ASSERT_TRUE(as != nullptr);
  EXPECT_EQ(repr(as), "store i64 123, i64* %loc1");
  Bblock *block = mkBlockFromStmt(be.get(), func, as);

  // assign a variable to a variable
  Bvariable *loc2 = be->local_variable(func, "loc2", bi64t, true, loc);
  Bexpression *ve2 = be->var_expression(loc2, true, loc);
  Bexpression *ve3 = be->var_expression(loc1, false, loc);
  Bstatement *as2 = be->assignment_statement(ve2, ve3, loc);
  ASSERT_TRUE(as2 != nullptr);
  addStmtToBlock(be.get(), block, as2);
  EXPECT_EQ(repr(as2),
            "%loc1.ld = load i64, i64* %loc1 == "
            "store i64 %loc1.ld, i64* %loc2");

  // error handling
  Bvariable *loc3 = be->local_variable(func, "loc3", bi64t, true, loc);
  Bexpression *ve4 = be->var_expression(loc3, true, loc);
  Bstatement *badas = be->assignment_statement(ve4,
                                               be->error_expression(), loc);
  ASSERT_TRUE(badas != nullptr);
  EXPECT_EQ(badas, be->error_statement());

  be->function_set_body(func, block);
}

TEST(BackendStmtTests, TestReturnStmt) {
  LLVMContext C;
  std::unique_ptr<Backend> be(go_get_backend(C));

  Bfunction *func = mkFunci32o64(be.get(), "foo");
  std::vector<Bexpression*> vals;
  vals.push_back(mkInt64Const(be.get(), 99));
  Location loc;
  Bstatement *ret = be->return_statement(func, vals, loc);
  Bblock *block = mkBlockFromStmt(be.get(), func, ret);
  EXPECT_EQ(repr(ret), "ret i64 99");

  // error handling
  std::vector<Bexpression*> bvals;
  vals.push_back(be->error_expression());
  Bstatement *bret = be->return_statement(func, vals, loc);
  EXPECT_EQ(bret, be->error_statement());

  be->function_set_body(func, block);
}

TEST(BackendStmtTests, TestLabelGotoStmts) {
  LLVMContext C;
  std::unique_ptr<Backend> be(go_get_backend(C));
  Bfunction *func = mkFunci32o64(be.get(), "foo");

  // loc1 = 10
  Location loc;
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *loc1 = be->local_variable(func, "loc1", bi64t, true, loc);
  Bstatement *is = be->init_statement(loc1, mkInt64Const(be.get(), 10));
  Bblock *block = mkBlockFromStmt(be.get(), func, is);

  // goto labeln
  Blabel *lab1 = be->label(func, "foolab", loc);
  Bstatement *gots = be->goto_statement(lab1, loc);
  addStmtToBlock(be.get(), block, gots);

  // dead stmt: loc1 = 11
  Bexpression *ved = be->var_expression(loc1, true, loc);
  Bexpression *c11 = mkInt64Const(be.get(), 11);
  Bstatement *asd = be->assignment_statement(ved, c11, loc);
  addStmtToBlock(be.get(), block, asd);

  // labeldef
  Bstatement *ldef = be->label_definition_statement(lab1);
  addStmtToBlock(be.get(), block, ldef);

  bool ok = be->function_set_body(func, block);
  EXPECT_TRUE(ok);
}

TEST(BackendStmtTests, TestIfStmt) {
  LLVMContext C;
  std::unique_ptr<Backend> be(go_get_backend(C));

  Location loc;
  Btype *bi64t = be->integer_type(false, 64);
  Bfunction *func = mkFunci32o64(be.get(), "foo");
  Bvariable *loc1 = be->local_variable(func, "loc1", bi64t, true, loc);

  // loc1 = 123
  Bexpression *ve1 = be->var_expression(loc1, true, loc);
  Bexpression *c123 = mkInt64Const(be.get(), 123);
  Bstatement *as1 = be->assignment_statement(ve1, c123, loc);
  Bblock *b1 = mkBlockFromStmt(be.get(), func, as1);

  // loc1 = 987
  Bexpression *ve2 = be->var_expression(loc1, true, loc);
  Bexpression *c987 = mkInt64Const(be.get(), 987);
  Bstatement *as2 = be->assignment_statement(ve2, c987, loc);
  Bblock *b2 = mkBlockFromStmt(be.get(), func, as2);

  // loc1 = 456
  Bexpression *ve3 = be->var_expression(loc1, true, loc);
  Bexpression *c456 = mkInt64Const(be.get(), 456);
  Bstatement *as3 = be->assignment_statement(ve3, c456, loc);
  Bblock *b3 = mkBlockFromStmt(be.get(), func, as3);

  // if true b1 else b2
  Bexpression *trueval = be->boolean_constant_expression(true);
  Bstatement *ifst = be->if_statement(trueval, b1, b2, Location());
  Bblock *ib = mkBlockFromStmt(be.get(), func, ifst);

  // if true if
  Bexpression *tv2 = be->boolean_constant_expression(true);
  Bstatement *ifst2 = be->if_statement(tv2, ib, b3, Location());
  Bblock *eb = mkBlockFromStmt(be.get(), func, ifst2);

  // set function body
  bool ok = be->function_set_body(func, eb);
  EXPECT_TRUE(ok);
}

}
