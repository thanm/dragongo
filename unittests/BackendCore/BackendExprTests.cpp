//===- llvm/tools/dragongo/unittests/BackendCore/BackendExprTests.cpp ------===//
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
  Btype *s2t = mkBackendStruct(be.get(), pbt, "f1", bi32t, "f2", nullptr);
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

  // We should get a distinct Bexpression each time we create a new
  // var expression.
  Bexpression *ve1 = be->var_expression(loc1, VE_rvalue, loc);
  EXPECT_EQ(repr(ve1->value()), "%loc1 = alloca i64");
  Bstatement *es = be->expression_statement(func, ve1);
  Bblock *block = mkBlockFromStmt(be.get(), func, es);
  Bexpression *ve2 = be->var_expression(loc1, VE_rvalue, loc);
  EXPECT_EQ(repr(ve2->value()), "%loc1 = alloca i64");
  EXPECT_NE(ve1, ve2);
  addExprToBlock(be.get(), func, block, ve2);

  // Same here.
  Bexpression *ve3 = be->var_expression(loc1, VE_lvalue, loc);
  EXPECT_EQ(repr(ve3->value()), "%loc1 = alloca i64");
  addExprToBlock(be.get(), func, block, ve3);
  Bexpression *ve4 = be->var_expression(loc1, VE_lvalue, loc);
  EXPECT_EQ(repr(ve4->value()), "%loc1 = alloca i64");
  EXPECT_NE(ve3, ve4);
  addExprToBlock(be.get(), func, block, ve4);

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
      Bstatement *es = be->expression_statement(func, cmp);
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
  bool equal = difftokens(exp, repr(block), reason);
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
      Bstatement *es = be->expression_statement(func, cmp);
      addStmtToBlock(be.get(), block, es);
    }
  }

  const char *exp = R"RAW_RESULT(
    store i1 true, i1* %loc1
    %add.0 = add i64 9, 3
    %fadd.0 = fadd double 9.000000e+00, 3.000000e+00
  )RAW_RESULT";

  std::string reason;
  bool equal = difftokens(exp, repr(block), reason);
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
  bool equal = difftokens(exp, repr(block), reason);
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
    // y = *x
    Bexpression *vey = be->var_expression(y, VE_lvalue, loc);
    Bexpression *vex = be->var_expression(x, VE_rvalue, loc);
    bool knValid = false;
    Bexpression *indx1 = be->indirect_expression(bi64t, vex, knValid, loc);
    Bstatement *as = be->assignment_statement(vey, indx1, loc);
    addStmtToBlock(be.get(), block, as);
  }

  {
    // *x = 3
    Bexpression *vex = be->var_expression(x, VE_lvalue, loc);
    Bexpression *indx = be->indirect_expression(bi64t, vex, false, loc);
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
    %x.ld.0 = load i64*, i64** %x
    %.ld.0 = load i64, i64* %x.ld.0
    store i64 %.ld.0, i64* %y
    %x.ld.1 = load i64*, i64** %x
    store i64 3, i64* %x.ld.1
    ret i64 10101
    )RAW_RESULT";

  std::string reason;
  bool equal = difftokens(exp, repr(block), reason);
  EXPECT_EQ("pass", equal ? "pass" : reason);

  be->function_set_body(func, block);

  bool broken = llvm::verifyModule(be->module(), &dbgs());
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, TestStructFieldExprs) {
  LLVMContext C;

  std::unique_ptr<Llvm_backend> be(new Llvm_backend(C));

  //
  // type X struct {
  //    f1 *bool
  //    f2 int32
  // }
  // var loc1 X
  //
  Location loc;
  Bfunction *func = mkFunci32o64(be.get(), "foo");
  Btype *bt = be->bool_type();
  Btype *pbt = be->pointer_type(bt);
  Btype *bi32t = be->integer_type(false, 32);
  Btype *s2t = mkBackendStruct(be.get(), pbt, "f1", bi32t, "f2", nullptr);
  Bvariable *loc1 = be->local_variable(func, "loc1", s2t, true, loc);
  Bexpression *bszero = be->zero_expression(s2t);
  Bstatement *is = be->init_statement(loc1, bszero);
  Bblock *block = mkBlockFromStmt(be.get(), func, is);

  // var loc2 *X = &loc1
  Btype *ps2t = be->pointer_type(s2t);
  Bvariable *loc2 = be->local_variable(func, "loc2", ps2t, true, loc);
  Bexpression *bl1vex = be->var_expression(loc1, VE_rvalue, loc);
  Bexpression *adl1 = be->address_expression(bl1vex, loc);
  Bstatement *isx = be->init_statement(loc2, adl1);
  addStmtToBlock(be.get(), block, isx);

  // x := loc1.f2
  Bvariable *x = be->local_variable(func, "x", bi32t, true, loc);
  Bexpression *vex = be->var_expression(x, VE_lvalue, loc);
  Bexpression *sex = be->var_expression(loc1, VE_rvalue, loc);
  Bexpression *fex = be->struct_field_expression(sex, 1, loc);
  Bstatement *as = be->assignment_statement(vex, fex, loc);
  addStmtToBlock(be.get(), block, as);

  // var b2 bool
  // loc1.b = &b2
  Bvariable *b2 = be->local_variable(func, "b2", bt, true, loc);
  Bexpression *lvex = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *bfex = be->struct_field_expression(lvex, 0, loc);
  Bexpression *b2ex = be->var_expression(b2, VE_rvalue, loc);
  Bexpression *adb2 = be->address_expression(b2ex, loc);
  Bstatement *as2 = be->assignment_statement(bfex, adb2, loc);
  addStmtToBlock(be.get(), block, as2);

  // var b2 bool
  // loc2.f2 = 2 (equivalent to (*loc2).f2 = 2)
  Bexpression *lvexi = be->var_expression(loc2, VE_lvalue, loc);
  bool knValid = false;
  Bexpression *lindx = be->indirect_expression(s2t, lvexi, knValid, loc);
  Bexpression *bfex2 = be->struct_field_expression(lindx, 1, loc);
  Bexpression *bc2 = mkInt32Const(be.get(), 2);
  Bstatement *as3 = be->assignment_statement(bfex2, bc2, loc);
  addStmtToBlock(be.get(), block, as3);

  // return 10101
  std::vector<Bexpression *> vals;
  vals.push_back(mkInt64Const(be.get(), 10101));
  Bstatement *ret = be->return_statement(func, vals, loc);
  addStmtToBlock(be.get(), block, ret);

  const char *exp = R"RAW_RESULT(
   store { i1*, i32 } zeroinitializer, { i1*, i32 }* %loc1
   store { i1*, i32 }* %loc1, { i1*, i32 }** %loc2
   %field.0 = getelementptr { i1*, i32 }, { i1*, i32 }* %loc1, i32 0, i32 1
   %loc1.field.ld.0 = load i32, i32* %field.0
   store i32 %loc1.field.ld.0, i32* %x
   %field.1 = getelementptr { i1*, i32 }, { i1*, i32 }* %loc1, i32 0, i32 0
   store i1* %b2, i1** %field.1
   %loc2.ld.0 = load { i1*, i32 }*, { i1*, i32 }** %loc2
   %field.2 = getelementptr { i1*, i32 }, { i1*, i32 }* %loc2.ld.0, i32 0, i32 1
   store i32 2, i32* %field.2
   ret i64 10101
  )RAW_RESULT";

  std::string reason;
  bool equal = difftokens(exp, repr(block), reason);
  EXPECT_EQ("pass", equal ? "pass" : reason);

  be->function_set_body(func, block);
  bool broken = llvm::verifyModule(be->module(), &dbgs());
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, CreateArrayConstructionExprs) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();

  // var aa [4]int64 = { 4, 3, 2, 1 }
  Location loc;
  Bexpression *val4 = mkInt64Const(be, int64_t(4));
  Btype *bi64t = be->integer_type(false, 64);
  Btype *at4 = be->array_type(bi64t, val4);
  std::vector<unsigned long> indexes1 = { 0, 1, 2, 3 };
  std::vector<Bexpression *> vals1;
  for (int64_t v : {4, 3, 2, 1})
    vals1.push_back(mkInt64Const(be, v));
  Bexpression *arcon1 =
      be->array_constructor_expression(at4, indexes1, vals1, loc);
  h.mkLocal("aa", at4, arcon1);

  // var ab [4]int64 = { 2:3 }
  std::vector<unsigned long> indexes2 = { 2 };
  std::vector<Bexpression *> vals2;
  vals2.push_back(mkInt64Const(be, int64_t(3)));
  Bexpression *arcon2 =
    be->array_constructor_expression(at4, indexes2, vals2, loc);
  h.mkLocal("ab", at4, arcon2);

  // var ac [4]int64 = { 1:z }
  Bvariable *z = h.mkLocal("z", bi64t);
  std::vector<unsigned long> indexes3 = { 1 };
  std::vector<Bexpression *> vals3;
  vals3.push_back(be->var_expression(z, VE_rvalue, loc));
  Bexpression *arcon3 =
      be->array_constructor_expression(at4, indexes3, vals3, loc);
  h.mkLocal("ac", at4, arcon3);

  const char *exp = R"RAW_RESULT(
    store [4 x i64] [i64 4, i64 3, i64 2, i64 1], [4 x i64]* %aa
    store [4 x i64] [i64 0, i64 0, i64 3, i64 0], [4 x i64]* %ab
    store i64 0, i64* %z
    %index.0 = getelementptr [4 x i64], [4 x i64]* %ac, i32 0, i32 0
    store i64 0, i64* %index.0
    %index.1 = getelementptr [4 x i64], [4 x i64]* %ac, i32 0, i32 1
    %z.ld.0 = load i64, i64* %z
    store i64 %z.ld.0, i64* %index.1
    %index.2 = getelementptr [4 x i64], [4 x i64]* %ac, i32 0, i32 2
    store i64 0, i64* %index.2
    %index.3 = getelementptr [4 x i64], [4 x i64]* %ac, i32 0, i32 3
    store i64 0, i64* %index.3
  )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, CreateStructConstructionExprs) {
  LLVMContext C;

  std::unique_ptr<Llvm_backend> be(new Llvm_backend(C));
  Bfunction *func = mkFunci32o64(be.get(), "foo");

  // type X struct {
  //    f1 *int32
  //    f2 int32
  // }
  // func foo(param1, param2 int32) int64 {
  // var loc1 X = { nil, 101 }
  // var loc2 X = { &param1, loc1.f2 }

  // var loc1 X = { nil, 101 }
  Location loc;
  Btype *bi32t = be->integer_type(false, 32);
  Btype *pbi32t = be->pointer_type(bi32t);
  Btype *s2t = mkBackendStruct(be.get(), pbi32t, "f1", bi32t, "f2", nullptr);
  std::vector<Bexpression *> vals1;
  vals1.push_back(be->zero_expression(pbi32t));
  vals1.push_back(mkInt32Const(be.get(), int32_t(101)));
  Bexpression *scon1 =
      be->constructor_expression(s2t, vals1, loc);
  Bvariable *loc1 = be->local_variable(func, "loc1", s2t, true, loc);
  Bstatement *is1 = be->init_statement(loc1, scon1);
  Bblock *block = mkBlockFromStmt(be.get(), func, is1);

  // var loc2 X = { &param1, loc1.f2 }
  Bvariable *p1 = func->getBvarForValue(func->getNthArgValue(0));
  Bexpression *ve1 = be->var_expression(p1, VE_rvalue, loc);
  Bexpression *adp = be->address_expression(ve1, loc);
  Bexpression *ve2 = be->var_expression(loc1, VE_rvalue, loc);
  Bexpression *fex = be->struct_field_expression(ve2, 1, loc);
  std::vector<Bexpression *> vals2;
  vals2.push_back(adp);
  vals2.push_back(fex);
  Bexpression *scon2 = be->constructor_expression(s2t, vals2, loc);
  Bvariable *loc2 = be->local_variable(func, "loc2", s2t, true, loc);
  Bstatement *is2 = be->init_statement(loc2, scon2);
  addStmtToBlock(be.get(), block, is2);

  // return 10101
  std::vector<Bexpression *> vals;
  vals.push_back(mkInt64Const(be.get(), 10101));
  Bstatement *ret = be->return_statement(func, vals, loc);
  addStmtToBlock(be.get(), block, ret);

  const char *exp = R"RAW_RESULT(
    store { i32*, i32 } { i32* null, i32 101 }, { i32*, i32 }* %loc1
    %field.1 = getelementptr { i32*, i32 }, { i32*, i32 }* %loc2, i32 0, i32 0
    store i32* %param1.addr, i32** %field.1
    %field.2 = getelementptr { i32*, i32 }, { i32*, i32 }* %loc2, i32 0, i32 1
    %field.0 = getelementptr { i32*, i32 }, { i32*, i32 }* %loc1, i32 0, i32 1
    %loc1.field.ld.0 = load i32, i32* %field.0
    store i32 %loc1.field.ld.0, i32* %field.2
    ret i64 10101
  )RAW_RESULT";

  std::string reason;
  bool equal = difftokens(exp, repr(block), reason);
  EXPECT_EQ("pass", equal ? "pass" : reason);

  be->function_set_body(func, block);
  bool broken = llvm::verifyModule(be->module(), &dbgs());
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, CreateArrayIndexingExprs) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();

  // var aa [4]int64 = { 4, 3, 2, 1 }
  Location loc;
  Bexpression *val4 = mkInt64Const(be, int64_t(4));
  Btype *bi64t = be->integer_type(false, 64);
  Btype *at4 = be->array_type(bi64t, val4);
  std::vector<unsigned long> indexes1 = { 0, 1, 2, 3 };
  std::vector<Bexpression *> vals1;
  for (int64_t v : {4, 3, 2, 1})
    vals1.push_back(mkInt64Const(be, v));
  Bexpression *arcon1 =
    be->array_constructor_expression(at4, indexes1, vals1, loc);
  Bvariable *aa = h.mkLocal("aa", at4, arcon1);

  // aa[1]
  Bexpression *bi32one = mkInt32Const(be, 1);
  Bexpression *vea1 = be->var_expression(aa, VE_rvalue, loc);
  Bexpression *aa1 = be->array_index_expression(vea1, bi32one, loc);

  // aa[3]
  Bexpression *bi64three = mkInt64Const(be, 3);
  Bexpression *vea2 = be->var_expression(aa, VE_rvalue, loc);
  Bexpression *aa2 = be->array_index_expression(vea2, bi64three, loc);

  // aa[aa[3]]
  Bexpression *vea3 = be->var_expression(aa, VE_rvalue, loc);
  Bexpression *aa3 = be->array_index_expression(vea3, aa2, loc);

  // aa[aa[1]]
  Bexpression *vea4 = be->var_expression(aa, VE_lvalue, loc);
  Bexpression *aa4 = be->array_index_expression(vea4, aa1, loc);

  // aa[aa[1]] = aa[aa[5]]
  h.mkAssign(aa4, aa3);

  const char *exp = R"RAW_RESULT(
    store [4 x i64] [i64 4, i64 3, i64 2, i64 1], [4 x i64]* %aa
    %index.1 = getelementptr [4 x i64], [4 x i64]* %aa, i32 0, i64 3
    %aa.index.ld.0 = load i64, i64* %index.1
    %index.2 = getelementptr [4 x i64], [4 x i64]* %aa, i32 0, i64 %aa.index.ld.0
    %aa.index.ld.2 = load i64, i64* %index.2
    %index.0 = getelementptr [4 x i64], [4 x i64]* %aa, i32 0, i32 1
    %aa.index.ld.1 = load i64, i64* %index.0
    %index.3 = getelementptr [4 x i64], [4 x i64]* %aa, i32 0, i64 %aa.index.ld.1
    store i64 %aa.index.ld.2, i64* %index.3
  )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, CreateComplexIndexingAndFieldExprs) {

  FcnTestHarness h("foo");

  // Create type that incorporates structures, arrays, and pointers:
  //
  //   type sA struct {
  //      x, y int64
  //   }
  //   type asA [4]*sA
  //   type sB struct {
  //      y  bool
  //      ar asA
  //      n  bool
  //   }
  //   type psB *sB
  //   type t [10]psB
  //
  Llvm_backend *be = h.be();
  Btype *bi64t = be->integer_type(false, 64);
  Btype *sA = mkBackendStruct(be, bi64t, "x", bi64t, "y", nullptr);
  Btype *psA = be->pointer_type(sA);
  Bexpression *val4 = mkInt64Const(be, int64_t(4));
  Btype *asA = be->array_type(psA, val4);
  Btype *bt = be->bool_type();
  Btype *sB = mkBackendStruct(be, bt, "y", asA, "ar", bt, "n", nullptr);
  Btype *psB = be->pointer_type(sB);
  Bexpression *val10 = mkInt64Const(be, int64_t(10));
  Btype *t = be->array_type(psB, val10);
  Location loc;

  // var t1 t
  Bvariable *t1 = h.mkLocal("t1", t);

  // t1[7].ar[3].x = 5
  {
    Bexpression *vt = be->var_expression(t1, VE_lvalue, loc);
    Bexpression *bi32sev = mkInt32Const(be, 7);
    Bexpression *ti7 = be->array_index_expression(vt, bi32sev, loc);
    bool knValid = true;
    Bexpression *iti7 = be->indirect_expression(sB, ti7, knValid, loc);
    Bexpression *far = be->struct_field_expression(iti7, 1, loc);
    Bexpression *bi32three = mkInt32Const(be, 3);
    Bexpression *ar3 = be->array_index_expression(far, bi32three, loc);
    Bexpression *iar3 = be->indirect_expression(sA, ar3, knValid, loc);
    Bexpression *fx = be->struct_field_expression(iar3, 0, loc);
    Bexpression *bi64five = mkInt64Const(be, 5);
    h.mkAssign(fx, bi64five);

  const char *exp = R"RAW_RESULT(
    store [10 x { i1, [4 x { i64, i64 }*], i1 }*] zeroinitializer,
      [10 x { i1, [4 x { i64, i64 }*], i1 }*]* %t1
    %index.0 = getelementptr [10 x { i1, [4 x { i64, i64 }*], i1 }*],
        [10 x { i1, [4 x { i64, i64 }*], i1 }*]* %t1, i32 0, i32 7
    %t1.index.ld.0 = load { i1, [4 x { i64, i64 }*], i1 }*,
        { i1, [4 x { i64, i64 }*], i1 }** %index.0
    %field.0 = getelementptr { i1, [4 x { i64, i64 }*], i1 },
        { i1, [4 x { i64, i64 }*], i1 }* %t1.index.ld.0, i32 0, i32 1
    %index.1 = getelementptr [4 x { i64, i64 }*],
         [4 x { i64, i64 }*]* %field.0, i32 0, i32 3
    %.field.index.ld.0 = load { i64, i64 }*,
          { i64, i64 }** %index.1
    %field.1 = getelementptr { i64, i64 },
      { i64, i64 }* %.field.index.ld.0, i32 0, i32 0
    store i64 5, i64* %field.1
  )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  }

  h.newBlock();

  // q := t1[0].ar[0].y
  {
    Bexpression *vt = be->var_expression(t1, VE_rvalue, loc);
    Bexpression *bi32zero = mkInt32Const(be, 0);
    Bexpression *ti0 = be->array_index_expression(vt, bi32zero, loc);
    bool knValid = true;
    Bexpression *iti0 = be->indirect_expression(sB, ti0, knValid, loc);
    Bexpression *far = be->struct_field_expression(iti0, 1, loc);
    Bexpression *ar3 = be->array_index_expression(far, bi32zero, loc);
    Bexpression *iar3 = be->indirect_expression(sA, ar3, knValid, loc);
    Bexpression *fx = be->struct_field_expression(iar3, 1, loc);
    h.mkLocal("q", bi64t, fx);

  const char *exp = R"RAW_RESULT(
      %index.2 = getelementptr [10 x { i1, [4 x { i64, i64 }*], i1 }*],
           [10 x { i1, [4 x { i64, i64 }*], i1 }*]* %t1, i32 0, i32 0
      %t1.index.ld.1 = load { i1, [4 x { i64, i64 }*], i1 }*,
           { i1, [4 x { i64, i64 }*], i1 }** %index.2
      %field.2 = getelementptr { i1, [4 x { i64, i64 }*], i1 },
           { i1, [4 x { i64, i64 }*], i1 }* %t1.index.ld.1, i32 0, i32 1
      %index.3 = getelementptr [4 x { i64, i64 }*],
         [4 x { i64, i64 }*]* %field.2, i32 0, i32 0
      %.field.index.ld.1 = load { i64, i64 }*,
         { i64, i64 }** %index.3
      %field.3 = getelementptr { i64, i64 },
          { i64, i64 }* %.field.index.ld.1, i32 0, i32 1
      %.field.ld.0 = load i64, i64* %field.3
      store i64 %.field.ld.0, i64* %q
  )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  }

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, CreateFunctionCodeExpression) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();
  Location loc;

  // Assign function address to local variable
  Bexpression *fp = be->function_code_expression(func, loc);
  h.mkLocal("fploc", fp->btype(), fp);

  // Cast function to pointer-sized int and store to local
  Btype *bt = be->bool_type();
  Btype *pbt = be->pointer_type(bt);
  Btype *uintptrt = be->integer_type(true, be->type_size(pbt));
  h.mkLocal("ui", uintptrt, be->convert_expression(uintptrt, fp, loc));

  const char *exp = R"RAW_RESULT(
    store i64 (i32, i32)* @foo, i64 (i32, i32)** %fploc
    store i64 ptrtoint (i64 (i32, i32)* @foo to i64), i64* %ui
  )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, CreateNilPointerExpression) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bexpression *npe = be->nil_pointer_expression();

  const char *exp = R"RAW_RESULT(
    i64* null
  )RAW_RESULT";

  bool isOK = h.expectValue(npe->value(), exp);
  EXPECT_TRUE(isOK && "Value does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

}
