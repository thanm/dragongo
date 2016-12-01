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
  StmtCleanup cl(be.get());
  ASSERT_TRUE(is != nullptr);
  cl.add(is);
  EXPECT_EQ(repr(is), "store i64 10, i64* %loc1");

  // error handling
  Bvariable *loc2 = be->local_variable(func, "loc1", bi64t, true, loc);
  Bstatement *bad = be->init_statement(loc2, be->error_expression());
  ASSERT_TRUE(bad != nullptr);
  cl.add(bad);
  EXPECT_EQ(bad, be->error_statement());
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
  Bexpression *ve1 = be->var_expression(loc1, loc);
  Bstatement *as = be->assignment_statement(ve1,
                                            mkInt64Const(be.get(), 123), loc);
  ASSERT_TRUE(as != nullptr);
  StmtCleanup cl(be.get());
  cl.add(as);
  EXPECT_EQ(repr(as), "store i64 123, i64* %loc1");

  // error handling
  Bvariable *loc2 = be->local_variable(func, "loc2", bi64t, true, loc);
  Bexpression *ve2 = be->var_expression(loc2, loc);
  Bstatement *badas = be->assignment_statement(ve2,
                                               be->error_expression(), loc);
  ASSERT_TRUE(badas != nullptr);
  EXPECT_EQ(badas, be->error_statement());
}

TEST(BackendStmtTests, TestReturnStmt) {
  LLVMContext C;
  std::unique_ptr<Backend> be(go_get_backend(C));

  Bfunction *func = mkFunci32o64(be.get(), "foo");
  std::vector<Bexpression*> vals;
  vals.push_back(mkInt64Const(be.get(), 99));
  Location loc;
  Bstatement *ret = be->return_statement(func, vals, loc);
  StmtCleanup cl(be.get());
  cl.add(ret);
  EXPECT_EQ(repr(ret), "ret i64 99");

  // error handling
  std::vector<Bexpression*> bvals;
  vals.push_back(be->error_expression());
  Bstatement *bret = be->return_statement(func, vals, loc);
  EXPECT_EQ(bret, be->error_statement());
}

}
