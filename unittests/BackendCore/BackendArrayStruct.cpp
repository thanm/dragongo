//===- llvm/tools/dragongo/unittests/BackendCore/BackendArrayStruct.cpp ---===//
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

TEST(BackendArrayStructTests, TestStructFieldExprs) {
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
      %cast.0 = bitcast { i8*, i32 }* %loc1 to i8*
      %copy.0 = call i8* @__builtin_memcpy(i8* %cast.0, i8* bitcast ({ i8*,
        i32 }* @const.0 to i8*), i64 8, i32 8, i1 false)
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

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendArrayStructTests, CreateArrayConstructionExprs) {

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
    %cast.0 = bitcast [4 x i64]* %aa to i8*
    %copy.0 = call i8* @__builtin_memcpy(i8* %cast.0, i8* bitcast ([4 x i64]* @const.0 to i8*), i64 8, i32 8, i1 false)
    %cast.2 = bitcast [4 x i64]* %ab to i8*
    %copy.1 = call i8* @__builtin_memcpy(i8* %cast.2, i8* bitcast ([4 x i64]* @const.1 to i8*), i64 8, i32 8, i1 false)
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

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendArrayStructTests, CreateStructConstructionExprs) {
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
      %cast.0 = bitcast { i32*, i32 }* %loc1 to i8*
      %copy.0 = call i8* @__builtin_memcpy(i8* %cast.0, i8* bitcast ({ i32*, i32 }* @const.0 to i8*), i64 8, i32 8, i1 false)
      %field.0 = getelementptr inbounds { i32*, i32 }, { i32*, i32 }* %loc1, i32 0, i32 1
      %loc1.field.ld.0 = load i32, i32* %field.0
      %field.1 = getelementptr inbounds { i32*, i32 }, { i32*, i32 }* %loc2, i32 0, i32 0
      store i32* %param1.addr, i32** %field.1
      %field.2 = getelementptr inbounds { i32*, i32 }, { i32*, i32 }* %loc2, i32 0, i32 1
      store i32 %loc1.field.ld.0, i32* %field.2
  )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendArrayStructTests, CreateStructConstructionExprs2) {
  FcnTestHarness h;
  Llvm_backend *be = h.be();

  Btype *bi32t = be->integer_type(false, 32);
  Btype *pbi32t = be->pointer_type(bi32t);
  Btype *s2t = mkBackendStruct(be, pbi32t, "f1", bi32t, "f2", nullptr);
  Btype *ps2t = be->pointer_type(s2t);
  BFunctionType *befty1 = mkFuncTyp(be,
                                    L_PARM, ps2t,
                                    L_PARM, pbi32t,
                                    L_END);
  Bfunction *func = h.mkFunction("blah", befty1);
  Location loc;

  // *p0 = { p1, 101 }
  Bvariable *p0 = func->getBvarForValue(func->getNthArgValue(0));
  Bvariable *p1 = func->getBvarForValue(func->getNthArgValue(1));
  Bexpression *ve = be->var_expression(p0, VE_lvalue, loc);
  Bexpression *dex = be->indirect_expression(s2t, ve, false, loc);
  std::vector<Bexpression *> vals;
  vals.push_back(be->var_expression(p1, VE_rvalue, loc));
  vals.push_back(mkInt32Const(be, int32_t(101)));
  Bexpression *scon =
      be->constructor_expression(s2t, vals, loc);
  h.mkAssign(dex, scon);

  const char *exp = R"RAW_RESULT(
      %p0.ld.0 = load { i32*, i32 }*, { i32*, i32 }** %p0.addr
      %p1.ld.0 = load i32*, i32** %p1.addr
      %field.0 = getelementptr inbounds { i32*, i32 }, { i32*, i32 }* %p0.ld.0, i32 0, i32 0
      store i32* %p1.ld.0, i32** %field.0
      %field.1 = getelementptr inbounds { i32*, i32 }, { i32*, i32 }* %p0.ld.0, i32 0, i32 1
      store i32 101, i32* %field.1
  )RAW_RESULT";

  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendArrayStructTests, CreateArrayIndexingExprs) {

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
      %cast.0 = bitcast [4 x i64]* %aa to i8*
      %copy.0 = call i8* @__builtin_memcpy(i8* %cast.0, i8* bitcast ([4 x i64]* @const.0 to i8*), i64 8, i32 8, i1 false)
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

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendArrayStructTests, CreateComplexIndexingAndFieldExprs) {

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
      %cast.0 = bitcast [10 x { i8, [4 x { i64, i64 }*], i8 }*]* %t1 to i8*
      %copy.0 = call i8* @__builtin_memcpy(i8* %cast.0, i8* bitcast ([10 x { i8, [4 x { i64, i64 }*], i8 }*]* @const.0 to i8*), i64 8, i32 8, i1 false)
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

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

}
