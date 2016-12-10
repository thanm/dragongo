//===- llvm/tools/dragongo/unittests/BackendCore/BackendTreeIntegrity.cpp -===//
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

TEST(BackendCoreTests, CheckTreeIntegrity1) {
  // Add the same instruction to more than one Bexpression
  LLVMContext C;
  Location loc;
  std::unique_ptr<Llvm_backend> be(new Llvm_backend(C));
  Bfunction *func = mkFunci32o64(be.get(), "foo");
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *loc1 = be->local_variable(func, "loc1", bi64t, true, loc);

  // Create "3 + loc1"
  Bexpression *bleft = mkInt64Const(be.get(), 9);
  Bexpression *bright = be->var_expression(loc1, VE_rvalue, loc);
  Bexpression *addop = be->binary_expression(OPERATOR_PLUS, bleft, bright, loc);

  // Create "(3 + loc1) + (3 + loc1)" reusing same expr
  Bexpression *naddop = be->binary_expression(OPERATOR_PLUS, addop, addop, loc);

  std::pair<bool, std::string> result = be->check_tree_integrity(naddop, false);
  EXPECT_EQ(false, result.first);
  EXPECT_TRUE(containstokens(result.second, "inst has multiple parents"));

  // To avoid double free when expressions are deleted
  be->detachBexpression(naddop);
  delete naddop->value();
  delete naddop;

  Bstatement *es = be->expression_statement(func, addop);
  Bblock *block = mkBlockFromStmt(be.get(), func, es);
  be->function_set_body(func, block);
}

TEST(BackendCoreTests, CheckTreeIntegrity2) {

  // Add the same Expression to more than one statement
  LLVMContext C;
  std::unique_ptr<Llvm_backend> be(new Llvm_backend(C));
  Location loc;
  Bfunction *func = mkFunci32o64(be.get(), "foo");
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *loc1 = be->local_variable(func, "loc1", bi64t, true, loc);

  // Create "loc1", then supply to more than one statement
  Bexpression *ve = be->var_expression(loc1, VE_lvalue, loc);
  Bstatement *es1 = be->expression_statement(func, ve);
  Bblock *block = mkBlockFromStmt(be.get(), func, es1);
  Bstatement *es2 = be->expression_statement(func, ve);
  addStmtToBlock(be.get(), block, es2);

  std::pair<bool, std::string> result = be->check_tree_integrity(block, false);
  EXPECT_EQ(false, result.first);
  EXPECT_TRUE(containstokens(result.second, "expression has multiple parents"));

  Bstatement::destroy(block);

  Bexpression *ve3= be->var_expression(loc1, VE_lvalue, loc);
  Bstatement *es3 = be->expression_statement(func, ve3);
  Bblock *block2 = mkBlockFromStmt(be.get(), func, es3);

  be->function_set_body(func, block2);
}

}
