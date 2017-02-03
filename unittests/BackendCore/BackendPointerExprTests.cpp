//=- llvm/tools/dragongo/unittests/BackendCore/BackendPointeerExprTests.cpp -=//
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
  %deref.ld.0 = load %CPT.0*, %CPT.0** %cast
  %cast = bitcast %CPT.0* %deref.ld.0 to %CPT.0**
  %deref.ld.1 = load %CPT.0*, %CPT.0** %cast
  %cast = bitcast %CPT.0* %deref.ld.1 to %CPT.0**
  %.ld.1 = load %CPT.0*, %CPT.0** %cast
  %icmp.2 = icmp eq %CPT.0* %cpv1.ld.1, %.ld.1
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

TEST(BackendExprTests, CreatePointerOffsetExprs) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();
  Location loc;

  Bvariable *p0 = func->getBvarForValue(func->getNthArgValue(0));
  Bvariable *p3 = func->getBvarForValue(func->getNthArgValue(2));

  {
    // ptr_offset(p3, 5) = 9
    Bexpression *ve = be->var_expression(p3, VE_rvalue, loc);
    Bexpression *cfive = mkInt32Const(be, 5);
    Bexpression *poe1 = be->pointer_offset_expression(ve, cfive, loc);
    Bexpression *cnine = mkInt64Const(be, 9);
    h.mkAssign(poe1, cnine);
  }

  {
    // p0 = deref(ptr_offset((uint32*)p3, 7))
    Btype *bi32t = be->integer_type(false, 32);
    Bexpression *ve = be->var_expression(p0, VE_lvalue, loc);
    Bexpression *ver = be->var_expression(p3, VE_rvalue, loc);
    Btype *bpi32t = be->pointer_type(bi32t);
    Bexpression *bcon = be->convert_expression(bpi32t, ver, loc);
    Bexpression *cseven = mkInt32Const(be, 7);
    Bexpression *poe2 = be->pointer_offset_expression(bcon, cseven, loc);
    Bexpression *der = be->indirect_expression(bi32t, poe2, false, loc);
    h.mkAssign(ve, der);
  }

  const char *exp = R"RAW_RESULT(
      %param3.ld.0 = load i64*, i64** %param3.addr
      %ptroff.0 = getelementptr i64, i64* %param3.ld.0, i32 5
      store i64 9, i64* %ptroff.0
      %cast = bitcast i64** %param3.addr to i32**
      %.ld.0 = load i32*, i32** %cast
      %ptroff.1 = getelementptr i32, i32* %.ld.0, i32 7
      %.ptroff.ld.0 = load i32, i32* %ptroff.1
      store i32 %.ptroff.ld.0, i32* %param1.addr
  )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

}
