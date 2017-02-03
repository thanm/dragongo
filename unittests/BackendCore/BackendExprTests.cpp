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
      %deref.ld.0 = load i32*, i32** %cast
      store i32 5, i32* %deref.ld.0
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
    Bvariable *bvr = valtotest[tidx].second;
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

      then.0:                                           ; preds = %entry
        call void @foo()
        br label %fallthrough.0

      fallthrough.0:                                    ; preds = %else.0, %then.0
        %tmpv.0.ld.0 = load i64, i64* %tmpv.0
        ret void

      else.0:                                           ; preds = %entry
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

TEST(BackendExprTests, TestLhsConditionalExpression) {

  FcnTestHarness h;
  Llvm_backend *be = h.be();
  Btype *bi32t = be->integer_type(false, 32);
  BFunctionType *befty1 = mkFuncTyp(be,
                            L_PARM, be->pointer_type(bi32t),
                            L_PARM, be->pointer_type(bi32t),
                            L_END);
  Bfunction *func = h.mkFunction("foo", befty1);
  Location loc;

  // *(p0 == nil ? p1 : p0) = 7
  Bvariable *p0v = func->getBvarForValue(func->getNthArgValue(0));
  Bvariable *p1v = func->getBvarForValue(func->getNthArgValue(1));
  Bexpression *vex = be->var_expression(p0v, VE_rvalue, loc);
  Bexpression *npe = be->nil_pointer_expression();
  Bexpression *cmp = be->binary_expression(OPERATOR_EQEQ, vex, npe, loc);
  Bexpression *ver0 = be->var_expression(p0v, VE_rvalue, loc);
  Bexpression *ver1 = be->var_expression(p1v, VE_rvalue, loc);
  Bexpression *guard =
      be->conditional_expression(func, be->pointer_type(bi32t), cmp,
                                 ver1, ver0, loc);
  Bexpression *dex = be->indirect_expression(bi32t, guard, false, loc);
  h.mkAssign(dex, mkInt32Const(be, 7));

  const char *exp = R"RAW_RESULT(
      define void @foo(i32* %p0, i32* %p1) {
      entry:
        %p0.addr = alloca i32*
        %p1.addr = alloca i32*
        %tmpv.0 = alloca i32*
        store i32* %p0, i32** %p0.addr
        store i32* %p1, i32** %p1.addr
        %p0.ld.0 = load i32*, i32** %p0.addr
        %icmp.0 = icmp eq i32* %p0.ld.0, null
        %zext.0 = zext i1 %icmp.0 to i8
        %trunc.0 = trunc i8 %zext.0 to i1
        br i1 %trunc.0, label %then.0, label %else.0

      then.0:                                           ; preds = %entry
        %p1.ld.0 = load i32*, i32** %p1.addr
        store i32* %p1.ld.0, i32** %tmpv.0
        br label %fallthrough.0

      fallthrough.0:                                    ; preds = %else.0, %then.0
        %tmpv.0.ld.0 = load i32*, i32** %tmpv.0
        %.ld.0 = load i32, i32* %tmpv.0.ld.0
        store i32 7, i32* %tmpv.0.ld.0
        ret void

      else.0:                                           ; preds = %entry
        %p0.ld.1 = load i32*, i32** %p0.addr
        store i32* %p0.ld.1, i32** %tmpv.0
        br label %fallthrough.0
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

TEST(BackendExprTests, TestCallArgConversions) {

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
