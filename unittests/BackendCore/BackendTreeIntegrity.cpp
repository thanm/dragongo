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

TEST(BackendTreeIntegrity, CheckTreeIntegrity1) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();
  Location loc;

  // So that we can test the checker itself
  be->disableIntegrityChecks();

  // Create "2 + x"
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *xv = h.mkLocal("x", bi64t);
  Bexpression *vex = be->var_expression(xv, VE_rvalue, loc);
  Bexpression *bl1 = mkInt64Const(be, 2);
  Bexpression *badd1 = be->binary_expression(OPERATOR_PLUS, bl1, vex, loc);
  Bstatement *es = be->expression_statement(func, badd1);
  h.addStmt(es);

  // Create "4"
  Bexpression *b4 = mkInt64Const(be, 4);
  Bstatement *es2 = be->expression_statement(func, b4);
  h.addStmt(es2);

  // Mangle the IR so that we have a some instructions
  // parented by more than one Bexpression. Warning to our viewers at
  // home -- don't do this.
  for (auto inst : badd1->instructions())
    b4->appendInstruction(inst);

  std::pair<bool, std::string> result =
      be->checkTreeIntegrity(h.block(), NoDumpPointers, CheckVarExprs);
  EXPECT_EQ(false, result.first);
  EXPECT_TRUE(containstokens(result.second,
                             "instruction has multiple parents"));

  // Undo the mangling to avoid asserts later on
  b4->clear();

  h.finish(PreserveDebugInfo);
}

TEST(BackendTreeIntegrity, CheckTreeIntegrity2) {

  // Add the same Expression to more than one statement
  LLVMContext C;
  std::unique_ptr<Llvm_backend> be(new Llvm_backend(C, nullptr, nullptr));
  be->disableIntegrityChecks();

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

  std::pair<bool, std::string> result =
      be->checkTreeIntegrity(block, NoDumpPointers, CheckVarExprs);
  EXPECT_EQ(false, result.first);
  EXPECT_TRUE(containstokens(result.second, "expr has multiple parents"));

  Bexpression *ve3= be->var_expression(loc1, VE_lvalue, loc);
  Bstatement *es3 = be->expression_statement(func, ve3);
  Bblock *block2 = mkBlockFromStmt(be.get(), func, es3);

  be->disableDebugMetaDataGeneration();

  be->function_set_body(func, block2);
}

TEST(BackendTreeIntegrity, CheckTreeIntegrity3) {

  // Same statement with more than one parent.
  LLVMContext C;
  std::unique_ptr<Llvm_backend> be(new Llvm_backend(C, nullptr, nullptr));
  be->disableIntegrityChecks();
  Location loc;
  Bfunction *func = mkFunci32o64(be.get(), "foo");

  // Create expr stmt, add to block more than once
  Bexpression *b2 = mkInt64Const(be.get(), 2);
  Bstatement *es = be->expression_statement(func, b2);
  Bblock *block = mkBlockFromStmt(be.get(), func, es);
  addStmtToBlock(be.get(), block, es);

  std::pair<bool, std::string> result =
      be->checkTreeIntegrity(block, NoDumpPointers, CheckVarExprs);
  EXPECT_EQ(false, result.first);
  EXPECT_TRUE(containstokens(result.second, "stmt has multiple parents"));

  Bexpression *b3 = mkInt64Const(be.get(), 3);
  Bstatement *es2 = be->expression_statement(func, b3);
  Bblock *block2 = mkBlockFromStmt(be.get(), func, es2);

  be->disableDebugMetaDataGeneration();

  be->function_set_body(func, block2);
}

}
