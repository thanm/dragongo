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
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace goBackendUnitTests;

namespace {

TEST(BackendExprTests, MakeBoolConstExpr) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  // Boolean constants
  Bexpression *trueval = be->boolean_constant_expression(true);
  ASSERT_TRUE(trueval != nullptr);
  EXPECT_EQ(llvm::ConstantInt::getTrue(C), trueval->value());
  Bexpression *falseval = be->boolean_constant_expression(false);
  ASSERT_TRUE(falseval != nullptr);
  EXPECT_EQ(llvm::ConstantInt::getFalse(C), falseval->value());
}

TEST(BackendExprTests, MakeIntConstExpr) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  // Integer constants, signed and unsigned
  Btype *bi64t = be->integer_type(false, 64);
  ASSERT_TRUE(bi64t != nullptr);
  static const int64_t i64tvals[] = {-9223372036854775807, 0, 1, 17179869184,
                                     9223372036854775807};
  for (auto val : i64tvals) {
    mpz_t mpz_val;
    memset(&mpz_val, '0', sizeof(mpz_val));
    mpz_init_set_si(mpz_val, val);
    Bexpression *beval = be->integer_constant_expression(bi64t, mpz_val);
    ASSERT_TRUE(beval != nullptr);
    EXPECT_EQ(beval->value(), llvm::ConstantInt::getSigned(bi64t->type(), val));
    mpz_clear(mpz_val);
  }

  Btype *bu64t = be->integer_type(true, 64);
  ASSERT_TRUE(bu64t != nullptr);
  static const uint64_t u64tvals[] = {0, 1, 9223372036854775807ull,
                                      17293822569102704639ull};
  for (auto val : u64tvals) {
    mpz_t mpz_val;
    memset(&mpz_val, '0', sizeof(mpz_val));
    mpz_init_set_ui(mpz_val, val);
    Bexpression *beval = be->integer_constant_expression(bu64t, mpz_val);
    ASSERT_TRUE(beval != nullptr);
    EXPECT_EQ(beval->value(), llvm::ConstantInt::get(bu64t->type(), val));
    mpz_clear(mpz_val);
  }
}

TEST(BackendExprTests, MakeFloatConstExpr) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  // Float constants
  Btype *bf32t = be->float_type(32);
  ASSERT_TRUE(bf32t != nullptr);
  static const float f32vals[] = {3.402823466e+38F, 0.0f, 1.1f,
                                  1.175494351e-38F};
  for (auto val : f32vals) {
    mpfr_t mpfr_val;

    mpfr_init(mpfr_val);
    mpfr_set_flt(mpfr_val, val, GMP_RNDN);
    Bexpression *beval = be->float_constant_expression(bf32t, mpfr_val);
    ASSERT_TRUE(beval != nullptr);
    EXPECT_EQ(beval->value(),
              llvm::ConstantFP::get(bf32t->type(), static_cast<double>(val)));

    mpfr_set_flt(mpfr_val, -val, GMP_RNDN);
    Bexpression *nbeval = be->float_constant_expression(bf32t, mpfr_val);
    ASSERT_TRUE(nbeval != nullptr);
    EXPECT_EQ(nbeval->value(),
              llvm::ConstantFP::get(bf32t->type(), static_cast<double>(-val)));

    mpfr_clear(mpfr_val);
  }

  // Double constants
  Btype *bf64t = be->float_type(64);
  ASSERT_TRUE(bf64t != nullptr);
  static const double f64vals[] = {1.7976931348623158e+308, 0.0f, 1.1f,
                                   2.2250738585072014e-308};
  for (auto val : f64vals) {
    mpfr_t mpfr_val;

    mpfr_init(mpfr_val);
    mpfr_set_d(mpfr_val, val, GMP_RNDN);
    Bexpression *beval = be->float_constant_expression(bf64t, mpfr_val);
    ASSERT_TRUE(beval != nullptr);
    EXPECT_EQ(beval->value(), llvm::ConstantFP::get(bf64t->type(), val));

    mpfr_set_d(mpfr_val, -val, GMP_RNDN);
    Bexpression *nbeval = be->float_constant_expression(bf64t, mpfr_val);
    ASSERT_TRUE(nbeval != nullptr);
    EXPECT_EQ(nbeval->value(), llvm::ConstantFP::get(bf64t->type(), -val));

    mpfr_clear(mpfr_val);
  }
}

TEST(BackendExprTests, MakeZeroValueExpr) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  // Zero value expressions for various types
  Btype *bt = be->bool_type();
  ASSERT_TRUE(bt != nullptr);
  Bexpression *bzero = be->zero_expression(bt);
  ASSERT_TRUE(bzero != nullptr);
  EXPECT_EQ(llvm::ConstantInt::getFalse(C), bzero->value());
  Btype *pbt = be->pointer_type(bt);
  Bexpression *bpzero = be->zero_expression(pbt);
  ASSERT_TRUE(bpzero != nullptr);
  Btype *bi32t = be->integer_type(false, 32);
  Btype *s2t = mkTwoFieldStruct(be.get(), pbt, bi32t);
  Bexpression *bszero = be->zero_expression(s2t);
  ASSERT_TRUE(bszero != nullptr);

  // Error handling
  EXPECT_EQ(be->zero_expression(be->error_type()), be->error_expression());
}

TEST(BackendExprTests, TestConversionExpressions) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  // We need way more test cases than this...
  // ... however at the moment only trivial converts are supported

  Btype *bt = be->bool_type();
  ASSERT_TRUE(bt != nullptr);
  Bexpression *bzero = be->zero_expression(bt);
  Bexpression *bcon = be->convert_expression(bt, bzero, Location());
  ASSERT_TRUE(bcon != nullptr);
  EXPECT_EQ(bzero->value(), bcon->value());

  // Error handling
  Bexpression *econ =
      be->convert_expression(be->error_type(), bzero, Location());
  EXPECT_EQ(econ, be->error_expression());
}

TEST(BackendExprTests, MakeVarExpressions) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  Bfunction *func = mkFunci32o64(be.get(), "foo");
  Btype *bi64t = be->integer_type(false, 64);
  Location loc;
  Bvariable *loc1 = be->local_variable(func, "loc1", bi64t, true, loc);

  // We should get a distinct value  two separate values when creating
  // var expressions.
  Bexpression *ve1 = be->var_expression(loc1, VE_rvalue, loc);
  EXPECT_EQ(repr(ve1), "%loc1 = alloca i64");
  Bstatement *es = be->expression_statement(ve1);
  Bblock *block = mkBlockFromStmt(be.get(), func, es);
  Bexpression *ve2 = be->var_expression(loc1, VE_rvalue, loc);
  EXPECT_EQ(repr(ve2), "%loc1 = alloca i64");
  EXPECT_NE(ve1, ve2);
  addExprToBlock(be.get(), block, ve2);

  // Same here.
  Bexpression *ve3 = be->var_expression(loc1, VE_lvalue, loc);
  EXPECT_EQ(repr(ve3), "%loc1 = alloca i64");
  addExprToBlock(be.get(), block, ve3);
  Bexpression *ve4 = be->var_expression(loc1, VE_lvalue, loc);
  EXPECT_EQ(repr(ve4), "%loc1 = alloca i64");
  EXPECT_NE(ve3, ve4);
  addExprToBlock(be.get(), block, ve4);

  be->function_set_body(func, block);
}

TEST(BackendExprTests, TestCompareOps) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  Operator optotest[] = {OPERATOR_EQEQ, OPERATOR_NOTEQ, OPERATOR_LT,
                         OPERATOR_LE,   OPERATOR_GT,    OPERATOR_GE};

  Bfunction *func = mkFunci32o64(be.get(), "foo");

  Bexpression *beic = mkInt64Const(be.get(), 9);
  Bexpression *beic2 = mkInt64Const(be.get(), 3);
  Bexpression *beuc = mkUint64Const(be.get(), 9);
  Bexpression *beuc2 = mkUint64Const(be.get(), 3);
  Bexpression *befc = mkFloat64Const(be.get(), 9.0);
  Bexpression *befc2 = mkFloat64Const(be.get(), 3.0);
  std::vector<std::pair<Bexpression *, Bexpression *>> valtotest;
  valtotest.push_back(std::make_pair(beic, beic2));
  valtotest.push_back(std::make_pair(beuc, beuc2));
  valtotest.push_back(std::make_pair(befc, befc2));

  Location loc;
  Btype *boolt = be->bool_type();
  Bvariable *loc1 = be->local_variable(func, "loc1", boolt, true, loc);
  Bexpression *trueval = be->boolean_constant_expression(true);
  Bstatement *is = be->init_statement(loc1, trueval);
  Bblock *block = mkBlockFromStmt(be.get(), func, is);

  for (unsigned tidx = 0; tidx < valtotest.size(); ++tidx) {
    Bexpression *bleft = valtotest[tidx].first;
    Bexpression *bright = valtotest[tidx].second;
    for (auto op : optotest) {
      Bexpression *cmp = be->binary_expression(op, bleft, bright, Location());
      Bstatement *es = be->expression_statement(cmp);
      addStmtToBlock(be.get(), block, es);
    }
  }

  const char *exp = R"RAW_RESULT(
    store i1 true, i1* %loc1
    %icmp.0 = icmp eq i64 9, 3
    %icmp.1 = icmp ne i64 9, 3
    %icmp.2 = icmp slt i64 9, 3
    %icmp.3 = icmp sle i64 9, 3
    %icmp.4 = icmp sgt i64 9, 3
    %icmp.5 = icmp sge i64 9, 3
    %icmp.6 = icmp eq i64 9, 3
    %icmp.7 = icmp ne i64 9, 3
    %icmp.8 = icmp ult i64 9, 3
    %icmp.9 = icmp ule i64 9, 3
    %icmp.10 = icmp ugt i64 9, 3
    %icmp.11 = icmp uge i64 9, 3
    %fcmp.0 = fcmp oeq double 9.000000e+00, 3.000000e+00
    %fcmp.1 = fcmp one double 9.000000e+00, 3.000000e+00
    %fcmp.2 = fcmp olt double 9.000000e+00, 3.000000e+00
    %fcmp.3 = fcmp ole double 9.000000e+00, 3.000000e+00
    %fcmp.4 = fcmp ogt double 9.000000e+00, 3.000000e+00
    %fcmp.5 = fcmp oge double 9.000000e+00, 3.000000e+00
    )RAW_RESULT";

  std::string reason;
  bool equal = difftokens(tokenize(exp), tokenize(repr(block)), reason);
  EXPECT_EQ("pass", equal ? "pass" : reason);

  be->function_set_body(func, block);
}

TEST(BackendExprTests, TestArithOps) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  Operator optotest[] = {OPERATOR_PLUS};

  Bfunction *func = mkFunci32o64(be.get(), "foo");

  Bexpression *beic = mkInt64Const(be.get(), 9);
  Bexpression *beic2 = mkInt64Const(be.get(), 3);
  Bexpression *befc = mkFloat64Const(be.get(), 9.0);
  Bexpression *befc2 = mkFloat64Const(be.get(), 3.0);
  std::vector<std::pair<Bexpression *, Bexpression *>> valtotest;
  valtotest.push_back(std::make_pair(beic, beic2));
  valtotest.push_back(std::make_pair(befc, befc2));

  Location loc;
  Btype *boolt = be->bool_type();
  Bvariable *loc1 = be->local_variable(func, "loc1", boolt, true, loc);
  Bexpression *trueval = be->boolean_constant_expression(true);
  Bstatement *is = be->init_statement(loc1, trueval);
  Bblock *block = mkBlockFromStmt(be.get(), func, is);

  for (unsigned tidx = 0; tidx < valtotest.size(); ++tidx) {
    Bexpression *bleft = valtotest[tidx].first;
    Bexpression *bright = valtotest[tidx].second;
    for (auto op : optotest) {
      Bexpression *cmp = be->binary_expression(op, bleft, bright, Location());
      Bstatement *es = be->expression_statement(cmp);
      addStmtToBlock(be.get(), block, es);
    }
  }

  const char *exp = R"RAW_RESULT(
    store i1 true, i1* %loc1
    %add.0 = add i64 9, 3
    %fadd.0 = fadd double 9.000000e+00, 3.000000e+00
  )RAW_RESULT";

  std::string reason;
  bool equal = difftokens(tokenize(exp), tokenize(repr(block)), reason);
  EXPECT_EQ("pass", equal ? "pass" : reason);

  be->function_set_body(func, block);
}

TEST(BackendExprTests, TestMoreArith) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  // var x int64, y = 10, z = 11, w = 12
  Bfunction *func = mkFunci32o64(be.get(), "foo");
  Btype *bi64t = be->integer_type(false, 64);
  Location loc;
  Bvariable *x = be->local_variable(func, "x", bi64t, true, loc);
  Bvariable *y = be->local_variable(func, "y", bi64t, true, loc);
  Bvariable *z = be->local_variable(func, "z", bi64t, true, loc);
  Bvariable *w = be->local_variable(func, "w", bi64t, true, loc);
  Bexpression *beic9 = mkInt64Const(be.get(), 9);
  Bstatement *isy = be->init_statement(y, beic9);
  Bblock *block = mkBlockFromStmt(be.get(), func, isy);
  Bexpression *beic10 = mkInt64Const(be.get(), 10);
  Bstatement *isz = be->init_statement(z, beic10);
  addStmtToBlock(be.get(), block, isz);
  Bexpression *beic11 = mkInt64Const(be.get(), 11);
  Bstatement *isw = be->init_statement(w, beic11);
  addStmtToBlock(be.get(), block, isw);

  // x = y + z + w
  Bexpression *vey = be->var_expression(y, VE_rvalue, loc);
  Bexpression *vez = be->var_expression(z, VE_rvalue, loc);
  Bexpression *vew = be->var_expression(w, VE_rvalue, loc);
  Bexpression *ypz = be->binary_expression(OPERATOR_PLUS, vey, vez, loc);
  Bexpression *ypzpw = be->binary_expression(OPERATOR_PLUS, ypz, vew, loc);
  Bexpression *vex = be->var_expression(x, VE_lvalue, loc);
  Bstatement *as = be->assignment_statement(vex, ypzpw, loc);
  addStmtToBlock(be.get(), block, as);

  const char *exp = R"RAW_RESULT(
  store i64 9, i64* %y
  store i64 10, i64* %z
  store i64 11, i64* %w
  %y.ld.0 = load i64, i64* %y
  %z.ld.0 = load i64, i64* %z
  %add.0 = add i64 %y.ld.0, %z.ld.0
  %w.ld.0 = load i64, i64* %w
  %add.1 = add i64 %add.0, %w.ld.0
  store i64 %add.1, i64* %x
  )RAW_RESULT";

  std::string reason;
  bool equal = difftokens(tokenize(exp), tokenize(repr(block)), reason);
  EXPECT_EQ("pass", equal ? "pass" : reason);

  be->function_set_body(func, block);
}

TEST(BackendExprTests, TestAddrAndIndirection) {
  LLVMContext C;

  std::unique_ptr<Llvm_backend> be(new Llvm_backend(C));

  // var y int64 = 10
  Bfunction *func = mkFunci32o64(be.get(), "foo");
  Btype *bi64t = be->integer_type(false, 64);
  Location loc;
  Bvariable *y = be->local_variable(func, "y", bi64t, true, loc);
  Bexpression *beic11 = mkInt64Const(be.get(), 10);
  Bstatement *isy = be->init_statement(y, beic11);
  Bblock *block = mkBlockFromStmt(be.get(), func, isy);

  // var x *int64 = nil
  Btype *bpi64t = be->pointer_type(bi64t);
  Bvariable *x = be->local_variable(func, "x", bpi64t, true, loc);
  Bstatement *isx = be->init_statement(x, be->zero_expression(bpi64t));
  addStmtToBlock(be.get(), block, isx);

  {
    // x = &y
    Bexpression *vex = be->var_expression(x, VE_lvalue, loc);
    Bexpression *vey = be->var_expression(y, VE_rvalue, loc);
    Bexpression *ady = be->address_expression(vey, loc);
    Bstatement *as = be->assignment_statement(vex, ady, loc);
    addStmtToBlock(be.get(), block, as);
  }

  {
    // y = **&x
    Bexpression *vey = be->var_expression(y, VE_lvalue, loc);
    Bexpression *vex = be->var_expression(x, VE_rvalue, loc);
    Bexpression *adx = be->address_expression(vex, loc);
    bool knValid = false;
    Bexpression *indx1 = be->indirect_expression(bpi64t, adx, knValid, loc);
    Bexpression *indx2 = be->indirect_expression(bi64t, indx1, knValid, loc);
    Bstatement *as = be->assignment_statement(vey, indx2, loc);
    addStmtToBlock(be.get(), block, as);
  }

  {
    // *x = 3
    Bexpression *vex = be->var_expression(x, VE_lvalue, loc);
    Bexpression *indx = be->indirect_expression(bpi64t, vex, false, loc);
    Bexpression *beic3 = mkInt64Const(be.get(), 3);
    Bstatement *as = be->assignment_statement(indx, beic3, loc);
    addStmtToBlock(be.get(), block, as);
  }

  // return 10101
  std::vector<Bexpression *> vals;
  vals.push_back(mkInt64Const(be.get(), 10101));
  Bstatement *ret = be->return_statement(func, vals, loc);
  addStmtToBlock(be.get(), block, ret);

  const char *exp = R"RAW_RESULT(
    store i64 10, i64* %y
    store i64* null, i64** %x
    store i64* %y, i64** %x
    %x.deref.ld.0 = load i64*, i64** %x
    %.ld.0 = load i64, i64* %x.deref.ld.0
    store i64 %.ld.0, i64* %y
    %x.deref.ld.1 = load i64*, i64** %x
    store i64 3, i64* %x.deref.ld.1
    ret i64 10101
    )RAW_RESULT";

  std::string reason;
  bool equal = difftokens(tokenize(exp), tokenize(repr(block)), reason);
  EXPECT_EQ("pass", equal ? "pass" : reason);

  be->function_set_body(func, block);

  bool broken = llvm::verifyModule(be->module(), &dbgs());
  EXPECT_FALSE(broken && "Module failed to verify.");
}
}
