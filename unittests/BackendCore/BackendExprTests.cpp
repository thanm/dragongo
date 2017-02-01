//===- llvm/tools/dragongo/unittests/BackendCore/BackendExprTests.cpp -----===//
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

//using namespace llvm;
using namespace goBackendUnitTests;

namespace {

TEST(BackendExprTests, MakeBoolConstExpr) {
  llvm::LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  // Boolean constants
  Bexpression *trueval = be->boolean_constant_expression(true);
  ASSERT_TRUE(trueval != nullptr);
  EXPECT_EQ(repr(trueval->value()), "i8 1");
  Bexpression *falseval = be->boolean_constant_expression(false);
  ASSERT_TRUE(falseval != nullptr);
  EXPECT_EQ(repr(falseval->value()), "i8 0");
}

TEST(BackendExprTests, MakeIntConstExpr) {
  llvm::LLVMContext C;

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
  llvm::LLVMContext C;

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
  llvm::LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  // Zero value expressions for various types
  Btype *bt = be->bool_type();
  ASSERT_TRUE(bt != nullptr);
  Bexpression *bzero = be->zero_expression(bt);
  ASSERT_TRUE(bzero != nullptr);
  EXPECT_EQ(repr(bzero->value()), "i8 0");
  Btype *pbt = be->pointer_type(bt);
  Bexpression *bpzero = be->zero_expression(pbt);
  ASSERT_TRUE(bpzero != nullptr);
  EXPECT_EQ(repr(bpzero->value()), "i8* null");
  Btype *bi32t = be->integer_type(false, 32);
  Btype *s2t = mkBackendStruct(be.get(), pbt, "f1", bi32t, "f2", nullptr);
  Bexpression *bszero = be->zero_expression(s2t);
  ASSERT_TRUE(bszero != nullptr);

  // Error handling
  EXPECT_EQ(be->zero_expression(be->error_type()), be->error_expression());
}

TEST(BackendExprTests, TestConversionExpressions) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Location loc;

  // Trivial / no-op conversion
  Btype *bt = be->bool_type();
  ASSERT_TRUE(bt != nullptr);
  Bexpression *bzero = be->zero_expression(bt);
  Bexpression *bcon = be->convert_expression(bt, bzero, Location());
  ASSERT_TRUE(bcon != nullptr);
  EXPECT_EQ(bzero->value(), bcon->value());

  // Casting one pointer to another. This is the equivalent of
  // type S struct {
  //   f1, f2 int32
  // }
  // var x int64
  // ((*S)&x).f1 = 22
  //
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *xv = h.mkLocal("x", bi64t);
  Btype *bi32t = be->integer_type(false, 32);
  Btype *s2t = mkBackendStruct(be, bi32t, "f1", bi32t, "f2", nullptr);
  Btype *ps2t = be->pointer_type(s2t);
  Bexpression *vex = be->var_expression(xv, VE_lvalue, loc);
  Bexpression *adx = be->address_expression(vex, loc);
  Bexpression *cast = be->convert_expression(ps2t, adx, loc);
  Bexpression *dex = be->indirect_expression(s2t, cast, false, loc);
  Bexpression *fex = be->struct_field_expression(dex, 1, loc);
  h.mkAssign(fex, mkInt32Const(be, 22));

  const char *exp = R"RAW_RESULT(
      store i64 0, i64* %x
      %cast = bitcast i64* %x to { i32, i32 }*
      %field.0 = getelementptr inbounds { i32, i32 }, { i32, i32 }* %cast, i32 0, i32 1
      store i32 22, i32* %field.0
    )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  // Error handling
  Bexpression *econ =
      be->convert_expression(be->error_type(), bzero, Location());
  EXPECT_EQ(econ, be->error_expression());

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, TestMoreConversionExpressions) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();
  Location loc;

  // *(*uint32)parm3 = 5
  Btype *bi32t = be->integer_type(false, 32);
  Btype *bpi32t = be->pointer_type(bi32t);
  Bvariable *p3 = func->getBvarForValue(func->getNthArgValue(2));
  Bexpression *ve = be->var_expression(p3, VE_lvalue, loc);
  Bexpression *conv = be->convert_expression(bpi32t, ve, loc);
  Bexpression *dex = be->indirect_expression(bi32t, conv, false, loc);
  h.mkAssign(dex, mkInt32Const(be, 5));

  const char *exp = R"RAW_RESULT(
      %cast = bitcast i64** %param3.addr to i32**
      %.ld.0 = load i32*, i32** %cast
      store i32 5, i32* %.ld.0
    )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, MakeVarExpressions) {
  llvm::LLVMContext C;

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
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Operator optotest[] = {OPERATOR_EQEQ, OPERATOR_NOTEQ, OPERATOR_LT,
                         OPERATOR_LE,   OPERATOR_GT,    OPERATOR_GE};

  Btype *bi64t = be->integer_type(false, 64);
  Btype *bui64t = be->integer_type(true, 64);
  Btype *bf64t = be->float_type(64);
  Bvariable *x = h.mkLocal("x", bi64t);
  Bvariable *y = h.mkLocal("y", bui64t);
  Bvariable *z = h.mkLocal("z", bf64t);
  Bexpression *beic = mkInt64Const(be, 9);
  Bexpression *beuc = mkUint64Const(be, 9);
  Bexpression *befc = mkFloat64Const(be, 9.0);
  std::vector<std::pair<Bexpression *, Bvariable *>> valtotest;
  valtotest.push_back(std::make_pair(beic, x));
  valtotest.push_back(std::make_pair(beuc, y));
  valtotest.push_back(std::make_pair(befc, z));

  Location loc;
  for (unsigned tidx = 0; tidx < valtotest.size(); ++tidx) {
    Bexpression *bleft = valtotest[tidx].first;
    Bvariable *bv = valtotest[tidx].second;
    for (auto op : optotest) {
      Bexpression *bright = be->var_expression(bv, VE_rvalue, loc);
      Bexpression *cmp = be->binary_expression(op, bleft, bright, Location());
      Bstatement *es = be->expression_statement(func, cmp);
      h.addStmt(es);
    }
  }

  const char *exp = R"RAW_RESULT(
      store i64 0, i64* %x
      store i64 0, i64* %y
      store double 0.000000e+00, double* %z
      %x.ld.0 = load i64, i64* %x
      %icmp.0 = icmp eq i64 9, %x.ld.0
      %zext.0 = zext i1 %icmp.0 to i8
      %x.ld.1 = load i64, i64* %x
      %icmp.1 = icmp ne i64 9, %x.ld.1
      %zext.1 = zext i1 %icmp.1 to i8
      %x.ld.2 = load i64, i64* %x
      %icmp.2 = icmp slt i64 9, %x.ld.2
      %zext.2 = zext i1 %icmp.2 to i8
      %x.ld.3 = load i64, i64* %x
      %icmp.3 = icmp sle i64 9, %x.ld.3
      %zext.3 = zext i1 %icmp.3 to i8
      %x.ld.4 = load i64, i64* %x
      %icmp.4 = icmp sgt i64 9, %x.ld.4
      %zext.4 = zext i1 %icmp.4 to i8
      %x.ld.5 = load i64, i64* %x
      %icmp.5 = icmp sge i64 9, %x.ld.5
      %zext.5 = zext i1 %icmp.5 to i8
      %y.ld.0 = load i64, i64* %y
      %icmp.6 = icmp eq i64 9, %y.ld.0
      %zext.6 = zext i1 %icmp.6 to i8
      %y.ld.1 = load i64, i64* %y
      %icmp.7 = icmp ne i64 9, %y.ld.1
      %zext.7 = zext i1 %icmp.7 to i8
      %y.ld.2 = load i64, i64* %y
      %icmp.8 = icmp ult i64 9, %y.ld.2
      %zext.8 = zext i1 %icmp.8 to i8
      %y.ld.3 = load i64, i64* %y
      %icmp.9 = icmp ule i64 9, %y.ld.3
      %zext.9 = zext i1 %icmp.9 to i8
      %y.ld.4 = load i64, i64* %y
      %icmp.10 = icmp ugt i64 9, %y.ld.4
      %zext.10 = zext i1 %icmp.10 to i8
      %y.ld.5 = load i64, i64* %y
      %icmp.11 = icmp uge i64 9, %y.ld.5
      %zext.11 = zext i1 %icmp.11 to i8
      %z.ld.0 = load double, double* %z
      %fcmp.0 = fcmp oeq double 9.000000e+00, %z.ld.0
      %zext.12 = zext i1 %fcmp.0 to i8
      %z.ld.1 = load double, double* %z
      %fcmp.1 = fcmp one double 9.000000e+00, %z.ld.1
      %zext.13 = zext i1 %fcmp.1 to i8
      %z.ld.2 = load double, double* %z
      %fcmp.2 = fcmp olt double 9.000000e+00, %z.ld.2
      %zext.14 = zext i1 %fcmp.2 to i8
      %z.ld.3 = load double, double* %z
      %fcmp.3 = fcmp ole double 9.000000e+00, %z.ld.3
      %zext.15 = zext i1 %fcmp.3 to i8
      %z.ld.4 = load double, double* %z
      %fcmp.4 = fcmp ogt double 9.000000e+00, %z.ld.4
      %zext.16 = zext i1 %fcmp.4 to i8
      %z.ld.5 = load double, double* %z
      %fcmp.5 = fcmp oge double 9.000000e+00, %z.ld.5
      %zext.17 = zext i1 %fcmp.5 to i8
    )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, TestArithOps) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Operator optotest[] = {OPERATOR_PLUS,OPERATOR_MINUS};

  Btype *bi64t = be->integer_type(false, 64);
  Btype *bf64t = be->float_type(64);
  Bvariable *x = h.mkLocal("x", bi64t);
  Bvariable *y = h.mkLocal("y", bf64t);
  Bexpression *beic = mkInt64Const(be, 9);
  Bexpression *befc = mkFloat64Const(be, 9.0);
  std::vector<std::pair<Bexpression *, Bvariable *>> valtotest;
  valtotest.push_back(std::make_pair(beic, x));
  valtotest.push_back(std::make_pair(befc, y));

  Location loc;
  for (unsigned tidx = 0; tidx < valtotest.size(); ++tidx) {
    Bexpression *bleft = valtotest[tidx].first;
    Bvariable *bv = valtotest[tidx].second;
    for (auto op : optotest) {
      Bexpression *bright = be->var_expression(bv, VE_rvalue, loc);
      Bexpression *cmp = be->binary_expression(op, bleft, bright, loc);
      Bstatement *es = be->expression_statement(func, cmp);
      h.addStmt(es);
    }
  }

  const char *exp = R"RAW_RESULT(
      store i64 0, i64* %x
      store double 0.000000e+00, double* %y
      %x.ld.0 = load i64, i64* %x
      %add.0 = add i64 9, %x.ld.0
      %x.ld.1 = load i64, i64* %x
      %sub.0 = sub i64 9, %x.ld.1
      %y.ld.0 = load double, double* %y
      %fadd.0 = fadd double 9.000000e+00, %y.ld.0
      %y.ld.1 = load double, double* %y
      %fsub.0 = fsub double 9.000000e+00, %y.ld.1
  )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, TestMoreArith) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();

  // var x int64, y = 9, z = 10, w = 11
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *x = h.mkLocal("x", bi64t);
  Bvariable *y = h.mkLocal("y", bi64t, mkInt64Const(be, 9));
  Bvariable *z = h.mkLocal("z", bi64t, mkInt64Const(be, 10));
  Bvariable *w = h.mkLocal("w", bi64t, mkInt64Const(be, 11));

  // x = y + z + w
  Location loc;
  Bexpression *vey = be->var_expression(y, VE_rvalue, loc);
  Bexpression *vez = be->var_expression(z, VE_rvalue, loc);
  Bexpression *vew = be->var_expression(w, VE_rvalue, loc);
  Bexpression *ypz = be->binary_expression(OPERATOR_PLUS, vey, vez, loc);
  Bexpression *ypzpw = be->binary_expression(OPERATOR_PLUS, ypz, vew, loc);
  Bexpression *vex = be->var_expression(x, VE_lvalue, loc);
  h.mkAssign(vex, ypzpw);

  const char *exp = R"RAW_RESULT(
  store i64 0, i64* %x
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

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, TestLogicalOps) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Operator optotest[] = {OPERATOR_ANDAND, OPERATOR_OROR};

  Btype *bi64t = be->integer_type(false, 64);
  Btype *bui64t = be->integer_type(true, 64);
  Btype *bt = be->bool_type();
  Bvariable *x = h.mkLocal("x", bi64t);
  Bvariable *y = h.mkLocal("y", bui64t);
  Bvariable *z = h.mkLocal("z", bt);
  Bvariable *x2 = h.mkLocal("x2", bi64t);
  Bvariable *y2 = h.mkLocal("y2", bui64t);
  Bvariable *z2 = h.mkLocal("z2", bt);
  std::vector<std::pair<Bvariable *, Bvariable *>> valtotest;
  valtotest.push_back(std::make_pair(x, x2));
  valtotest.push_back(std::make_pair(y, y2));
  valtotest.push_back(std::make_pair(z, z2));

  Location loc;
  for (unsigned tidx = 0; tidx < valtotest.size(); ++tidx) {
    Bvariable *bvl = valtotest[tidx].first;
    Bexpression *bleft = be->var_expression(bvl, VE_rvalue, loc);
    Bvariable *bvr = valtotest[tidx].second;
    Bexpression *bright = be->var_expression(bvr, VE_rvalue, loc);
    for (auto op : optotest) {
      Bexpression *bleft = be->var_expression(bvl, VE_rvalue, loc);
      Bexpression *bright = be->var_expression(bvr, VE_rvalue, loc);
      Bexpression *cmp = be->binary_expression(op, bleft, bright, Location());
      Bstatement *es = be->expression_statement(func, cmp);
      h.addStmt(es);
    }
  }

  const char *exp = R"RAW_RESULT(
      store i64 0, i64* %x
      store i64 0, i64* %y
      store i8 0, i8* %z
      store i64 0, i64* %x2
      store i64 0, i64* %y2
      store i8 0, i8* %z2
      %x.ld.0 = load i64, i64* %x
      %x2.ld.0 = load i64, i64* %x2
      %iand.0 = and i64 %x.ld.0, %x2.ld.0
      %x.ld.1 = load i64, i64* %x
      %x2.ld.1 = load i64, i64* %x2
      %ior.0 = or i64 %x.ld.1, %x2.ld.1
      %y.ld.0 = load i64, i64* %y
      %y2.ld.0 = load i64, i64* %y2
      %iand.1 = and i64 %y.ld.0, %y2.ld.0
      %y.ld.1 = load i64, i64* %y
      %y2.ld.1 = load i64, i64* %y2
      %ior.1 = or i64 %y.ld.1, %y2.ld.1
      %z.ld.0 = load i8, i8* %z
      %z2.ld.0 = load i8, i8* %z2
      %iand.2 = and i8 %z.ld.0, %z2.ld.0
      %z.ld.1 = load i8, i8* %z
      %z2.ld.1 = load i8, i8* %z2
      %ior.2 = or i8 %z.ld.1, %z2.ld.1
    )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, TestAddrAndIndirection) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();

  // var y int64 = 10
  Bfunction *func = mkFunci32o64(be, "foo");
  Btype *bi64t = be->integer_type(false, 64);
  Location loc;
  Bvariable *y = h.mkLocal("y", bi64t, mkInt64Const(be, 10));

  // var x *int64 = nil
  Btype *bpi64t = be->pointer_type(bi64t);
  Bvariable *x = h.mkLocal("x", bpi64t);

  {
    // x = &y
    Bexpression *vex = be->var_expression(x, VE_lvalue, loc);
    Bexpression *vey = be->var_expression(y, VE_rvalue, loc);
    Bexpression *ady = be->address_expression(vey, loc);
    Bstatement *as = be->assignment_statement(func, vex, ady, loc);
    h.addStmt(as);
  }

  {
    // y = *x
    Bexpression *vey = be->var_expression(y, VE_lvalue, loc);
    Bexpression *vex = be->var_expression(x, VE_rvalue, loc);
    bool knValid = false;
    Bexpression *indx1 = be->indirect_expression(bi64t, vex, knValid, loc);
    Bstatement *as = be->assignment_statement(func, vey, indx1, loc);
    h.addStmt(as);
  }

  {
    // *x = 3
    Bexpression *vex = be->var_expression(x, VE_lvalue, loc);
    Bexpression *indx = be->indirect_expression(bi64t, vex, false, loc);
    Bexpression *beic3 = mkInt64Const(be, 3);
    Bstatement *as = be->assignment_statement(func, indx, beic3, loc);
    h.addStmt(as);
  }


  const char *exp = R"RAW_RESULT(
    store i64 10, i64* %y
    store i64* null, i64** %x
    store i64* %y, i64** %x
    %x.ld.0 = load i64*, i64** %x
    %.ld.0 = load i64, i64* %x.ld.0
    store i64 %.ld.0, i64* %y
    %x.ld.1 = load i64*, i64** %x
    store i64 3, i64* %x.ld.1
    )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, TestStructFieldExprs) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();

  //
  // type X struct {
  //    f1 *bool
  //    f2 int32
  // }
  // var loc1 X
  //
  Location loc;
  Btype *bt = be->bool_type();
  Btype *pbt = be->pointer_type(bt);
  Btype *bi32t = be->integer_type(false, 32);
  Btype *s2t = mkBackendStruct(be, pbt, "f1", bi32t, "f2", nullptr);
  Bvariable *loc1 = h.mkLocal("loc1", s2t);

  // var loc2 *X = &loc1
  Btype *ps2t = be->pointer_type(s2t);
  Bexpression *bl1vex = be->var_expression(loc1, VE_rvalue, loc);
  Bexpression *adl1 = be->address_expression(bl1vex, loc);
  Bvariable *loc2 = h.mkLocal("loc2", ps2t, adl1);

  // var x int32
  // x = loc1.f2
  Bvariable *x = h.mkLocal("x", bi32t);
  Bexpression *vex = be->var_expression(x, VE_lvalue, loc);
  Bexpression *sex = be->var_expression(loc1, VE_rvalue, loc);
  Bexpression *fex = be->struct_field_expression(sex, 1, loc);
  h.mkAssign(vex, fex);

  // var b2 bool
  // loc1.b = &b2
  Bvariable *b2 = h.mkLocal("b2", bt);
  Bexpression *lvex = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *bfex = be->struct_field_expression(lvex, 0, loc);
  Bexpression *b2ex = be->var_expression(b2, VE_rvalue, loc);
  Bexpression *adb2 = be->address_expression(b2ex, loc);
  h.mkAssign(bfex, adb2);

  // var b2 bool
  // loc2.f2 = 2 (equivalent to (*loc2).f2 = 2)
  Bexpression *lvexi = be->var_expression(loc2, VE_lvalue, loc);
  bool knValid = false;
  Bexpression *lindx = be->indirect_expression(s2t, lvexi, knValid, loc);
  Bexpression *bfex2 = be->struct_field_expression(lindx, 1, loc);
  Bexpression *bc2 = mkInt32Const(be, 2);
  h.mkAssign(bfex2, bc2);

  const char *exp = R"RAW_RESULT(
      store { i8*, i32 } zeroinitializer, { i8*, i32 }* %loc1
      store { i8*, i32 }* %loc1, { i8*, i32 }** %loc2
      store i32 0, i32* %x
      %field.0 = getelementptr inbounds { i8*, i32 }, { i8*, i32 }* %loc1, i32 0, i32 1
      %loc1.field.ld.0 = load i32, i32* %field.0
      store i32 %loc1.field.ld.0, i32* %x
      store i8 0, i8* %b2
      %field.1 = getelementptr inbounds { i8*, i32 }, { i8*, i32 }* %loc1, i32 0, i32 0
      store i8* %b2, i8** %field.1
      %loc2.ld.0 = load { i8*, i32 }*, { i8*, i32 }** %loc2
      %field.2 = getelementptr inbounds { i8*, i32 }, { i8*, i32 }* %loc2.ld.0, i32 0, i32 1
      store i32 2, i32* %field.2
  )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
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
    %z.ld.0 = load i64, i64* %z
    %index.0 = getelementptr [4 x i64], [4 x i64]* %ac, i32 0, i32 0
    store i64 0, i64* %index.0
    %index.1 = getelementptr [4 x i64], [4 x i64]* %ac, i32 0, i32 1
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
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();
  Location loc;

  // type X struct {
  //    f1 *int32
  //    f2 int32
  // }
  // func foo(param1, param2 int32) int64 {
  // var loc1 X = { nil, 101 }
  // var loc2 X = { &param1, loc1.f2 }

  // var loc1 X = { nil, 101 }
  Btype *bi32t = be->integer_type(false, 32);
  Btype *pbi32t = be->pointer_type(bi32t);
  Btype *s2t = mkBackendStruct(be, pbi32t, "f1", bi32t, "f2", nullptr);
  std::vector<Bexpression *> vals1;
  vals1.push_back(be->zero_expression(pbi32t));
  vals1.push_back(mkInt32Const(be, int32_t(101)));
  Bexpression *scon1 =
      be->constructor_expression(s2t, vals1, loc);
  Bvariable *loc1 = h.mkLocal("loc1", s2t, scon1);

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
  h.mkLocal("loc2", s2t, scon2);

  const char *exp = R"RAW_RESULT(
    store { i32*, i32 } { i32* null, i32 101 }, { i32*, i32 }* %loc1
      %field.0 = getelementptr inbounds { i32*, i32 }, { i32*, i32 }* %loc1, i32 0, i32 1
      %loc1.field.ld.0 = load i32, i32* %field.0
      %field.1 = getelementptr inbounds { i32*, i32 }, { i32*, i32 }* %loc2, i32 0, i32 0
      store i32* %param1.addr, i32** %field.1
      %field.2 = getelementptr inbounds { i32*, i32 }, { i32*, i32 }* %loc2, i32 0, i32 1
      store i32 %loc1.field.ld.0, i32* %field.2
  )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
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

  // aa[aa[1]] = aa[aa[3]]
  h.mkAssign(aa4, aa3);

  const char *exp = R"RAW_RESULT(
      store [4 x i64] [i64 4, i64 3, i64 2, i64 1], [4 x i64]* %aa
      %index.0 = getelementptr [4 x i64], [4 x i64]* %aa, i32 0, i32 1
      %aa.index.ld.1 = load i64, i64* %index.0
      %index.3 = getelementptr [4 x i64], [4 x i64]* %aa, i32 0, i64 %aa.index.ld.1
      %index.1 = getelementptr [4 x i64], [4 x i64]* %aa, i32 0, i64 3
      %aa.index.ld.0 = load i64, i64* %index.1
      %index.2 = getelementptr [4 x i64], [4 x i64]* %aa, i32 0, i64 %aa.index.ld.0
      %aa.index.ld.2 = load i64, i64* %index.2
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
      store [10 x { i8, [4 x { i64, i64 }*], i8 }*] zeroinitializer, [10 x { i8, [4 x { i64, i64 }*], i8 }*]* %t1
      %index.0 = getelementptr [10 x { i8, [4 x { i64, i64 }*], i8 }*], [10 x { i8, [4 x { i64, i64 }*], i8 }*]* %t1, i32 0, i32 7
      %t1.index.ld.0 = load { i8, [4 x { i64, i64 }*], i8 }*, { i8, [4 x { i64, i64 }*], i8 }** %index.0
      %field.0 = getelementptr inbounds { i8, [4 x { i64, i64 }*], i8 }, { i8, [4 x { i64, i64 }*], i8 }* %t1.index.ld.0, i32 0, i32 1
      %index.1 = getelementptr [4 x { i64, i64 }*], [4 x { i64, i64 }*]* %field.0, i32 0, i32 3
      %.field.index.ld.0 = load { i64, i64 }*, { i64, i64 }** %index.1
      %field.1 = getelementptr inbounds { i64, i64 }, { i64, i64 }* %.field.index.ld.0, i32 0, i32 0
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
      %index.2 = getelementptr [10 x { i8, [4 x { i64, i64 }*], i8 }*], [10 x { i8, [4 x { i64, i64 }*], i8 }*]* %t1, i32 0, i32 0
      %t1.index.ld.1 = load { i8, [4 x { i64, i64 }*], i8 }*, { i8, [4 x { i64, i64 }*], i8 }** %index.2
      %field.2 = getelementptr inbounds { i8, [4 x { i64, i64 }*], i8 }, { i8, [4 x { i64, i64 }*], i8 }* %t1.index.ld.1, i32 0, i32 1
      %index.3 = getelementptr [4 x { i64, i64 }*], [4 x { i64, i64 }*]* %field.2, i32 0, i32 0
      %.field.index.ld.1 = load { i64, i64 }*, { i64, i64 }** %index.3
      %field.3 = getelementptr inbounds { i64, i64 }, { i64, i64 }* %.field.index.ld.1, i32 0, i32 1
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

  // Local variables of pointer-to-function-descriptor type. A key
  // item to note here is that we want to verify that the backend methods
  // allow flexibility in terms of the concrete LLVM type for the
  // function descriptor. The FE sometimes creates a struct with a
  // uintptr field, and sometimes a struct with a function pointer field.

  // Function descriptor type with uintptr, e.g. { i64 }
  Btype *fdesct1 = mkFuncDescType(be);

  // Function descriptor type with function pointer, e.g. { i64 (i64, ...) }
  Btype *fdesct2 = mkBackendStruct(be, func->fcnType(), "f1", nullptr);

  // Function descriptor variable
  Bvariable *bfdv1 = h.mkLocal("fdloc1", fdesct1, mkFuncDescExpr(be, func));

  // Pointer-to-FD variables
  Btype *pfd1t = be->pointer_type(fdesct1);
  Btype *pfd2t = be->pointer_type(fdesct2);
  Bexpression *vex1 = be->var_expression(bfdv1, VE_rvalue, loc);
  Bexpression *adfd1 = be->address_expression(vex1, loc);
  Bvariable *bfpv1 = h.mkLocal("fploc1", pfd1t, adfd1);
  Bvariable *bfpv2 = h.mkLocal("fploc2", pfd2t);

  // Assignment of function descriptor pointer values. Note that the
  // types here are not going to agree strictly; this test verifies
  // that this flexibility is allowed.
  Bexpression *vex2 = be->var_expression(bfpv2, VE_lvalue, loc);
  Bexpression *rvex2 = be->var_expression(bfpv1, VE_rvalue, loc);
  h.mkAssign(vex2, rvex2);
  Bexpression *vex3 = be->var_expression(bfpv1, VE_lvalue, loc);
  Bexpression *rvex3 = be->var_expression(bfpv2, VE_rvalue, loc);
  h.mkAssign(vex3, rvex3);

  const char *exp = R"RAW_RESULT(
      store { i64 } { i64 ptrtoint (i64 (i32, i32, i64*)* @foo to i64) },
             { i64 }* %fdloc1
      store { i64 }* %fdloc1, { i64 }** %fploc1
      store { i64 (i32, i32, i64*) }* null, { i64 (i32, i32, i64*) }** %fploc2
      %fploc1.ld.0 = load { i64 }*, { i64 }** %fploc1
      %cast = bitcast { i64 }* %fploc1.ld.0 to { i64 (i32, i32, i64*) }*
      store { i64 (i32, i32, i64*) }* %cast, { i64 (i32, i32, i64*) }** %fploc2
      %fploc2.ld.0 = load { i64 (i32, i32, i64*) }*,
            { i64 (i32, i32, i64*) }** %fploc2
      %cast = bitcast { i64 (i32, i32, i64*) }* %fploc2.ld.0 to { i64 }*
      store { i64 }* %cast, { i64 }** %fploc1
  )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, CreateNilPointerExpression) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();

  // Manufacture a nil pointer expression
  Bexpression *npe = be->nil_pointer_expression();
  const char *exp1 = R"RAW_RESULT(
    i64* null
  )RAW_RESULT";
  bool isOK = h.expectValue(npe->value(), exp1);
  EXPECT_TRUE(isOK && "Value does not have expected contents");

  // Expressions involving nil pointers.
  Location loc;
  Btype *bt = be->bool_type();
  Btype *pbt = be->pointer_type(bt);
  Bvariable *b1 = h.mkLocal("b1", bt);
  Bvariable *pb1 = h.mkLocal("pb1", pbt);

  {
    // b1 = (pb1 == nil)
    Bexpression *vel = be->var_expression(b1, VE_lvalue, loc);
    Bexpression *ver = be->var_expression(pb1, VE_rvalue, loc);
    Bexpression *npe = be->nil_pointer_expression();
    Bexpression *cmp = be->binary_expression(OPERATOR_EQEQ, ver, npe, loc);
    h.mkAssign(vel, cmp);
  }

  {
    // b1 = (nil == pb1)
    Bexpression *vel = be->var_expression(b1, VE_lvalue, loc);
    Bexpression *ver = be->var_expression(pb1, VE_rvalue, loc);
    Bexpression *npe = be->nil_pointer_expression();
    Bexpression *cmp = be->binary_expression(OPERATOR_EQEQ, npe, ver, loc);
    h.mkAssign(vel, cmp);
  }

  const char *exp2 = R"RAW_RESULT(
      store i8 0, i8* %b1
      store i8* null, i8** %pb1
      %pb1.ld.0 = load i8*, i8** %pb1
      %icmp.0 = icmp eq i8* %pb1.ld.0, null
      %zext.0 = zext i1 %icmp.0 to i8
      store i8 %zext.0, i8* %b1
      %pb1.ld.1 = load i8*, i8** %pb1
      %icmp.1 = icmp eq i8* null, %pb1.ld.1
      %zext.1 = zext i1 %icmp.1 to i8
      store i8 %zext.1, i8* %b1
    )RAW_RESULT";

  bool isOK2 = h.expectBlock(exp2);
  EXPECT_TRUE(isOK2 && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, CreateStringConstantExpressions) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();

  {
    Bexpression *snil = be->string_constant_expression("");
    const char *exp = R"RAW_RESULT(
    i8* null
    )RAW_RESULT";
    bool isOK = h.expectValue(snil->value(), exp);
    EXPECT_TRUE(isOK && "Value does not have expected contents");
  }

  {
    Bexpression *sblah = be->string_constant_expression("blah");
    const char *exp = R"RAW_RESULT(
    i8* getelementptr inbounds ([5 x i8], [5 x i8]* @0, i32 0, i32 0)
    )RAW_RESULT";
    bool isOK = h.expectValue(sblah->value(), exp);
    EXPECT_TRUE(isOK && "Value does not have expected contents");
  }

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, CircularPointerExpressions1) {

  // This testpoint is intended to verify handling of expressions
  // involving circular pointer types. Go code:
  //
  //  type p *p
  //  func foo() {
  //     var cpv1, cpv2 p
  //     cpv1 = &cpv2
  //     cpv2 = &cpv1
  //     b1 := (cpv1 == *cpv2)
  //     b2 := (&cpv1 != cpv2)
  //     b3 := (&cpv1 == ***cpv2)

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Location loc;

  // Create circular pointer type
  Btype *pht = be->placeholder_pointer_type("ph", loc, false);
  Btype *cpt = be->circular_pointer_type(pht, false);
  be->set_placeholder_pointer_type(pht, cpt);
  EXPECT_EQ(pht->type(), cpt->type());

  // Local vars
  Bvariable *cpv1 = h.mkLocal("cpv1", pht);
  Bvariable *cpv2 = h.mkLocal("cpv2", pht);

  {
    // cpv1 = &cpv2
    Bexpression *ve1 = be->var_expression(cpv1, VE_lvalue, loc);
    Bexpression *ve2 = be->var_expression(cpv2, VE_rvalue, loc);
    Bexpression *adx = be->address_expression(ve2, loc);
    h.mkAssign(ve1, adx);
  }

  {
    // cpv2 = &cpv1
    Bexpression *ve1 = be->var_expression(cpv2, VE_lvalue, loc);
    Bexpression *ve2 = be->var_expression(cpv1, VE_rvalue, loc);
    Bexpression *adx = be->address_expression(ve2, loc);
    h.mkAssign(ve1, adx);
  }

  Btype *bt = be->bool_type();
  Bvariable *b1 = h.mkLocal("b1", bt);
  Bvariable *b2 = h.mkLocal("b2", bt);
  Bvariable *b3 = h.mkLocal("b3", bt);

  {
    // b1 := (cpv1 == *cpv2)
    Bexpression *ve0 = be->var_expression(b1, VE_lvalue, loc);
    Bexpression *ve1 = be->var_expression(cpv1, VE_rvalue, loc);
    Bexpression *ve2 = be->var_expression(cpv2, VE_rvalue, loc);
    Bexpression *dex = be->indirect_expression(pht, ve2, false, loc);
    Bexpression *cmp = be->binary_expression(OPERATOR_EQEQ, ve1, dex, loc);
    h.mkAssign(ve0, cmp);
  }

  {
    // b2 := (&cpv1 != cpv2)
    Bexpression *ve0 = be->var_expression(b2, VE_lvalue, loc);
    Bexpression *ve1 = be->var_expression(cpv1, VE_rvalue, loc);
    Bexpression *adx = be->address_expression(ve1, loc);
    Bexpression *ve2 = be->var_expression(cpv2, VE_rvalue, loc);
    Bexpression *cmp = be->binary_expression(OPERATOR_EQEQ, adx, ve2, loc);
    h.mkAssign(ve0, cmp);
  }

  {
    // b3 := (cpv1 == ***cpv2)
    Bexpression *ve0 = be->var_expression(b3, VE_lvalue, loc);
    Bexpression *ve1 = be->var_expression(cpv1, VE_rvalue, loc);
    Bexpression *ve2 = be->var_expression(cpv2, VE_rvalue, loc);
    Bexpression *dex1 = be->indirect_expression(pht, ve2, false, loc);
    Bexpression *dex2 = be->indirect_expression(pht, dex1, false, loc);
    Bexpression *dex3 = be->indirect_expression(pht, dex2, false, loc);
    Bexpression *cmp = be->binary_expression(OPERATOR_EQEQ, ve1, dex3, loc);
    h.mkAssign(ve0, cmp);
  }

  const char *exp = R"RAW_RESULT(
      store %CPT.0* null, %CPT.0** %cpv1
      store %CPT.0* null, %CPT.0** %cpv2
      %cast = bitcast %CPT.0** %cpv2 to %CPT.0*
      store %CPT.0* %cast, %CPT.0** %cpv1
      %cast = bitcast %CPT.0** %cpv1 to %CPT.0*
      store %CPT.0* %cast, %CPT.0** %cpv2
      store i8 0, i8* %b1
      store i8 0, i8* %b2
      store i8 0, i8* %b3
      %cpv1.ld.0 = load %CPT.0*, %CPT.0** %cpv1
      %cpv2.ld.0 = load %CPT.0*, %CPT.0** %cpv2
      %cast = bitcast %CPT.0* %cpv2.ld.0 to %CPT.0**
      %.ld.0 = load %CPT.0*, %CPT.0** %cast
      %icmp.0 = icmp eq %CPT.0* %cpv1.ld.0, %.ld.0
      %zext.0 = zext i1 %icmp.0 to i8
      store i8 %zext.0, i8* %b1
      %cast = bitcast %CPT.0** %cpv1 to %CPT.0*
      %cpv2.ld.1 = load %CPT.0*, %CPT.0** %cpv2
      %icmp.1 = icmp eq %CPT.0* %cast, %cpv2.ld.1
      %zext.1 = zext i1 %icmp.1 to i8
      store i8 %zext.1, i8* %b2
      %cpv1.ld.1 = load %CPT.0*, %CPT.0** %cpv1
      %cpv2.ld.2 = load %CPT.0*, %CPT.0** %cpv2
      %cast = bitcast %CPT.0* %cpv2.ld.2 to %CPT.0**
      %.ld.1 = load %CPT.0*, %CPT.0** %cast
      %cast = bitcast %CPT.0* %.ld.1 to %CPT.0**
      %.ld.2 = load %CPT.0*, %CPT.0** %cast
      %cast = bitcast %CPT.0* %.ld.2 to %CPT.0**
      %.ld.3 = load %CPT.0*, %CPT.0** %cast
      %icmp.2 = icmp eq %CPT.0* %cpv1.ld.1, %.ld.3
      %zext.2 = zext i1 %icmp.2 to i8
      store i8 %zext.2, i8* %b3
    )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, CircularPointerExpressions2) {

  // More tests for circular pointers, this time
  // with multiple levels. Go code:
  //
  //  type p *q
  //  type q *p
  //  func foo() {
  //     var x p
  //     var y q
  //     b1 := (x == *y)

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Location loc;

  // Create circular pointer types
  Btype *pht1 = be->placeholder_pointer_type("ph1", loc, false);
  Btype *pht2 = be->placeholder_pointer_type("ph2", loc, false);
  Btype *cpt = be->circular_pointer_type(pht1, false);
  be->set_placeholder_pointer_type(pht2, be->pointer_type(cpt));
  be->set_placeholder_pointer_type(pht1, cpt);

  // Local vars
  Bvariable *cpv1 = h.mkLocal("x", pht1);
  Bvariable *cpv2 = h.mkLocal("y", pht2);

  {
    // x = &y
    Bexpression *ve1 = be->var_expression(cpv1, VE_lvalue, loc);
    Bexpression *ve2 = be->var_expression(cpv2, VE_rvalue, loc);
    Bexpression *adx = be->address_expression(ve2, loc);
    h.mkAssign(ve1, adx);
  }

  {
    // y = &x
    Bexpression *ve1 = be->var_expression(cpv2, VE_lvalue, loc);
    Bexpression *ve2 = be->var_expression(cpv1, VE_rvalue, loc);
    Bexpression *adx = be->address_expression(ve2, loc);
    h.mkAssign(ve1, adx);
  }

  Btype *bt = be->bool_type();
  Bvariable *b1 = h.mkLocal("b1", bt);

  {
    // b1 := (x == *y)
    Bexpression *ve0 = be->var_expression(b1, VE_lvalue, loc);
    Bexpression *ve1 = be->var_expression(cpv1, VE_rvalue, loc);
    Bexpression *ve2 = be->var_expression(cpv2, VE_rvalue, loc);
    Bexpression *dex = be->indirect_expression(pht1, ve2, false, loc);
    Bexpression *cmp = be->binary_expression(OPERATOR_EQEQ, ve1, dex, loc);
    h.mkAssign(ve0, cmp);
  }

  const char *exp = R"RAW_RESULT(
      store %CPT.0* null, %CPT.0** %x
      store %CPT.0** null, %CPT.0*** %y
      %cast = bitcast %CPT.0*** %y to %CPT.0*
      store %CPT.0* %cast, %CPT.0** %x
      store %CPT.0** %x, %CPT.0*** %y
      store i8 0, i8* %b1
      %cast = bitcast %CPT.0** %x to %CPT.0****
      %x.ld.0 = load %CPT.0***, %CPT.0**** %cast
      %y.ld.0 = load %CPT.0**, %CPT.0*** %y
      %cast = bitcast %CPT.0** %y.ld.0 to %CPT.0****
      %.ld.0 = load %CPT.0***, %CPT.0**** %cast
      %icmp.0 = icmp eq %CPT.0*** %x.ld.0, %.ld.0
      %zext.0 = zext i1 %icmp.0 to i8
      store i8 %zext.0, i8* %b1
    )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, TestConditionalExpression1) {

  FcnTestHarness h;
  Llvm_backend *be = h.be();
  BFunctionType *befty1 = mkFuncTyp(be, L_END);
  Bfunction *func = h.mkFunction("foo", befty1);
  Location loc;

  // Local vars
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *pv1 = h.mkLocal("a", bi64t);
  Bvariable *pv2 = h.mkLocal("b", bi64t);

  // Two calls, no type
  Bexpression *call1 = mkCallExpr(be, func, nullptr);
  Bexpression *call2 = mkCallExpr(be, func, nullptr);
  Bexpression *vex1 = be->var_expression(pv1, VE_rvalue, loc);
  Bexpression *vex2 = be->var_expression(pv2, VE_rvalue, loc);
  Bexpression *cmp = be->binary_expression(OPERATOR_LT, vex1, vex2, loc);
  Bexpression *condex = be->conditional_expression(func, nullptr, cmp, call1,
                                                   call2, loc);
  h.mkExprStmt(condex);

  const char *exp = R"RAW_RESULT(
      define void @foo() {
      entry:
        %a = alloca i64
        %b = alloca i64
        store i64 0, i64* %a
        store i64 0, i64* %b
        %a.ld.0 = load i64, i64* %a
        %b.ld.0 = load i64, i64* %b
        %icmp.0 = icmp slt i64 %a.ld.0, %b.ld.0
        %zext.0 = zext i1 %icmp.0 to i8
        %trunc.0 = trunc i8 %zext.0 to i1
        br i1 %trunc.0, label %then.0, label %else.0
      then.0:                                           ; preds = %entry
        call void @foo()
        br label %fallthrough.0
      fallthrough.0:                                ; preds = %else.0, %then.0
        ret void
      else.0:                                           ; preds = %entry
        call void @foo()
        br label %fallthrough.0
      }
    )RAW_RESULT";

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");

  bool isOK = h.expectValue(func->function(), exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");
}

TEST(BackendExprTests, TestConditionalExpression2) {

  FcnTestHarness h;
  Llvm_backend *be = h.be();
  BFunctionType *befty1 = mkFuncTyp(be, L_END);
  Bfunction *func = h.mkFunction("foo", befty1);
  Location loc;

  // Local vars
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *pv1 = h.mkLocal("a", bi64t);

  // Call on true branch,
  Bexpression *call1 = mkCallExpr(be, func, nullptr);
  Bexpression *ve = be->var_expression(pv1, VE_rvalue, loc);
  Bexpression *cmp = be->binary_expression(OPERATOR_LT,
                                           mkInt64Const(be, int64_t(3)),
                                           mkInt64Const(be, int64_t(4)), loc);
  Bexpression *condex = be->conditional_expression(func, ve->btype(),
                                                   cmp, call1,
                                                   ve, loc);
  h.mkExprStmt(condex);

  const char *exp = R"RAW_RESULT(
      define void @foo() {
      entry:
        %a = alloca i64
        %tmpv.0 = alloca i64
        store i64 0, i64* %a
        br i1 true, label %then.0, label %else.0
      then.0:                                  ; preds = %entry
        call void @foo()
        br label %fallthrough.0
      fallthrough.0:                           ; preds = %else.0, %then.0
        %tmpv.0.ld.0 = load i64, i64* %tmpv.0
        ret void
      else.0:                                  ; preds = %entry
        %a.ld.0 = load i64, i64* %a
        store i64 %a.ld.0, i64* %tmpv.0
        br label %fallthrough.0
      }
    )RAW_RESULT";

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");

  bool isOK = h.expectValue(func->function(), exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");
}

TEST(BackendExprTests, TestCompoundExpression) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();
  Location loc;

  // var x int64 = 0
  // x = 5
  // x
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *xv = h.mkLocal("x", bi64t);
  Bexpression *vex = be->var_expression(xv, VE_lvalue, loc);
  Bstatement *st =  be->assignment_statement(func, vex,
                                             mkInt64Const(be, 5), loc);
  Bexpression *vex2 = be->var_expression(xv, VE_rvalue, loc);
  Bexpression *ce = be->compound_expression(st, vex2, loc);
  Bstatement *es = be->expression_statement(func, ce);
  h.addStmt(es);

  const char *exp = R"RAW_RESULT(
      define i64 @foo(i32 %param1, i32 %param2, i64* %param3) {
      entry:
        %param1.addr = alloca i32
        %param2.addr = alloca i32
        %param3.addr = alloca i64*
        %x = alloca i64
        store i32 %param1, i32* %param1.addr
        store i32 %param2, i32* %param2.addr
        store i64* %param3, i64** %param3.addr
        store i64 0, i64* %x
        store i64 5, i64* %x
        %x.ld.0 = load i64, i64* %x
        ret i64 0
      }
    )RAW_RESULT";

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");

  // Note that this
  bool isOK = h.expectValue(func->function(), exp);
  EXPECT_TRUE(isOK && "Function does not have expected contents");
}

TEST(BackendExprTests, TestUnaryExpression) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Location loc;

  // var x bool
  // var y bool = !x
  Btype *bt = be->bool_type();
  Bvariable *xv = h.mkLocal("x", bt);
  Bexpression *vex = be->var_expression(xv, VE_rvalue, loc);
  h.mkLocal("y", bt, be->unary_expression(OPERATOR_NOT, vex, loc));

  // var a i32
  // var b i32 = -a
  Btype *bi32t = be->integer_type(false, 32);
  Bvariable *av = h.mkLocal("a", bi32t);
  Bexpression *vea = be->var_expression(av, VE_rvalue, loc);
  h.mkLocal("b", bi32t, be->unary_expression(OPERATOR_MINUS, vea, loc));

  // var z i64
  // var w i64 = ^z
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *zv = h.mkLocal("z", bi64t);
  Bexpression *vez = be->var_expression(zv, VE_rvalue, loc);
  h.mkLocal("w", bi64t, be->unary_expression(OPERATOR_XOR, vez, loc));

  const char *exp = R"RAW_RESULT(
      store i8 0, i8* %x
      %x.ld.0 = load i8, i8* %x
      %icmp.0 = icmp ne i8 %x.ld.0, 0
      %xor.0 = xor i1 %icmp.0, true
      %zext.0 = zext i1 %xor.0 to i8
      store i8 %zext.0, i8* %y
      store i32 0, i32* %a
      %a.ld.0 = load i32, i32* %a
      %sub.0 = sub i32 0, %a.ld.0
      store i32 %sub.0, i32* %b
      store i64 0, i64* %z
      %z.ld.0 = load i64, i64* %z
      %xor.1 = xor i64 %z.ld.0, -1
      store i64 %xor.1, i64* %w
    )RAW_RESULT";

  // Note that this
  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendExprTests, TestCallArgCOnversions) {

  FcnTestHarness h;
  Llvm_backend *be = h.be();
  Btype *bi8t = be->integer_type(false, 8);
  Btype *bi32t = be->integer_type(false, 32);
  Btype *bi64t = be->integer_type(false, 64);
  BFunctionType *befty1 = mkFuncTyp(be,
                            L_PARM, be->pointer_type(bi8t),
                            L_PARM, be->pointer_type(bi32t),
                            L_PARM, be->pointer_type(bi64t),
                            L_END);
  Bfunction *func = h.mkFunction("foo", befty1);
  Location loc;

  Bexpression *nil = be->nil_pointer_expression();
  Bexpression *call1 = mkCallExpr(be, func, nil, nil, nil, nullptr);
  h.mkExprStmt(call1);

  const char *exp = R"RAW_RESULT(
     call void @foo(i8* null, i32* null, i64* null)
    )RAW_RESULT";

  // Note that this
  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

}
