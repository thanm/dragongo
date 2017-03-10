//==- llvm/tools/dragongo/unittests/BackendCore/BackendCABIOracleTests.cpp -==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TestUtils.h"
#include "go-llvm-cabi-oracle.h"
#include "go-llvm-backend.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace goBackendUnitTests;

namespace {

TEST(BackendCABIOracleTests, Basic) {
  LLVMContext C;
  std::unique_ptr<Llvm_backend> bep(new Llvm_backend(C, nullptr, nullptr));
  Llvm_backend *be = bep.get();

  Btype *bi8t = be->integer_type(false, 8);
  Btype *bu8t = be->integer_type(true, 8);
  Btype *bf32t = be->float_type(32);
  Btype *bf64t = be->float_type(64);
  Btype *st0 = mkBackendStruct(be, nullptr);
  Btype *st1 = mkBackendStruct(be, bi8t, "a", bu8t, "b", bf32t, "c", nullptr);
  Btype *st2 = mkBackendStruct(be, bf64t, "f1", bf64t, "f2", nullptr);

  {
    BFunctionType *befty1 = mkFuncTyp(be,
                                      L_PARM, bi8t,
                                      L_PARM, bf32t,
                                      L_PARM, st0,
                                      L_PARM, st1,
                                      L_RES, st2,
                                      L_END);
    CABIOracle cab(befty1, be->typeManager());
    const char *exp = R"RAW_RESULT(
      Return: Direct { { double, double } } sigOffset: -1
      Param 1: Direct AttrSext { i8 } sigOffset: 0
      Param 2: Direct { float } sigOffset: 1
      Param 3: Ignore { void } sigOffset: -1
      Param 4: Direct { i64 } sigOffset: 2
    )RAW_RESULT";
    std::string reason;
    bool equal = difftokens(exp, cab.toString(), reason);
    EXPECT_EQ("pass", equal ? "pass" : reason);
    EXPECT_EQ(repr(cab.getFunctionTypeForABI()),
              "{ double, double } (i8, float, i64)");
  }
}

TEST(BackendCABIOracleTests, Extended) {
  LLVMContext C;
  std::unique_ptr<Llvm_backend> bep(new Llvm_backend(C, nullptr, nullptr));
  Llvm_backend *be = bep.get();

  Btype *bi8t = be->integer_type(false, 8);
  Btype *bu8t = be->integer_type(true, 8);
  Btype *bf32t = be->float_type(32);
  Btype *bf64t = be->float_type(64);
  Btype *st0 = mkBackendStruct(be, nullptr);
  Btype *st1 = mkBackendStruct(be, bi8t, "a", bu8t, "b", bf32t, "c", nullptr);
  Btype *st2 = mkBackendStruct(be, bf64t, "f1", bf64t, "f2", nullptr);
  Btype *st3 = mkBackendStruct(be, st2, "f1", bi8t, "f2", nullptr);
  Btype *st4 = mkBackendStruct(be, bf32t, "f1", bf32t, "f2", nullptr);

  struct FcnItem {
    FcnItem(const std::vector<Btype*> &r,
            const std::vector<Btype*> &p,
            const char *d, const char *t)
        : results(r), parms(p), expDump(d), expTyp(t) { }
    std::vector<Btype*> results;
    std::vector<Btype*> parms;
    const char *expDump;
    const char *expTyp;
  };

  Btype *nt = nullptr;
  std::vector<FcnItem> items = {

    FcnItem( {  }, {  },
             "Return: Ignore { void } sigOffset: -1",
             "void ()"),

    FcnItem( { bi8t }, { },
             "Return: Direct AttrSext { i8 } sigOffset: -1",
             "i8 ()"),

    FcnItem( {  }, { bi8t },
             "Return: Ignore { void } sigOffset: -1 "
             "Param 1: Direct AttrSext { i8 } sigOffset: 0",
             "void (i8)"),

    FcnItem({ bi8t, bf64t }, { bi8t, bu8t, st0 },
            "Return: Direct { { i8, double } } sigOffset: -1 "
            "Param 1: Direct AttrSext { i8 } sigOffset: 0 "
            "Param 2: Direct AttrZext { i8 } sigOffset: 1 "
            "Param 3: Ignore { void } sigOffset: -1",
            "{ i8, double } (i8, i8)"),

    FcnItem({ st2 }, { st2, st0, st4, st1 },
            "Return: Direct { { double, double } } sigOffset: -1 "
            "Param 1: Direct { double, double } sigOffset: 0 "
            "Param 2: Ignore { void } sigOffset: -1 "
            "Param 3: Direct { <2 x float> } sigOffset: 2 "
            "Param 4: Direct { i64 } sigOffset: 3 ",
            "{ double, double } (double, double, <2 x float>, i64)"),

    FcnItem({ st3 }, { st3, st0, bu8t },
            "Return: Indirect AttrStructReturn { { { double, double }, i8 }* } sigOffset: 0 "
            "Param 1: Indirect AttrByVal { { { double, double }, i8 }* } sigOffset: 1 "
            "Param 2: Ignore { void } sigOffset: -1 "
            "Param 3: Direct AttrZext { i8 } sigOffset: 2 ",
            "void ({ { double, double }, i8 }*, "
            "{ { double, double }, i8 }*, i8)"),

  };

  for (auto &item : items) {
    std::vector<Backend::Btyped_identifier> results;
    std::vector<Backend::Btyped_identifier> params;
    for (auto &r : item.results)
      results.push_back(mkid(r));
    for (auto &p : item.parms)
      params.push_back(mkid(p));
    Btype *rt = nullptr;
    if (results.size() > 1)
      rt = be->struct_type(results);
    Btype *t = be->function_type(mkid(nt), params, results, rt, Location());
    BFunctionType *bft = t->castToBFunctionType();
    CABIOracle cab(bft, be->typeManager());
    //cab.dump();

    { std::string reason;
      bool equal = difftokens(item.expDump, cab.toString(), reason);
      EXPECT_EQ("pass", equal ? "pass" : reason);
    }
    { std::string reason;
      std::string result(repr(cab.getFunctionTypeForABI()));
      bool equal = difftokens(item.expTyp, result, reason);
      EXPECT_EQ("pass", equal ? "pass" : reason);
    }
  }
}

TEST(BackendCABIOracleTests, RecursiveCall1) {
  FcnTestHarness h;
  Llvm_backend *be = h.be();

  // type s1 struct {
  //   f1, f2 float32
  //   i1, i2, i3 int16
  // }
  // type s2 struct {
  //   k float64
  //   f1, f2 float32
  // }
  // type s3 struct {
  //   f1, f2 s1
  // }
  // type s4 struct {
  // }
  // func foo(x s1, y s2, z s4, sm1 uint8, sm2 int16, w s3) s2 {
  //   if (sm1 == 0) {
  //     return y
  //   }
  //   return foo(x, y, z, sm1-1, sm2, s3)
  // }
  //

  // Create struct types
  Btype *bf32t = be->float_type(32);
  Btype *bf64t = be->float_type(64);
  Btype *bi16t = be->integer_type(false, 16);
  Btype *bi8t = be->integer_type(false, 8);
  Btype *bu8t = be->integer_type(true, 8);
  Btype *s1 = mkBackendStruct(be, bf32t, "f1", bf32t, "f2",
                              bi16t, "i1", bi16t, "i2", bi16t, "i3", nullptr);
  Btype *s2 = mkBackendStruct(be, bf64t, "k", bf32t, "f1", bf32t, "f2",
                              nullptr);
  Btype *s3 = mkBackendStruct(be, s1, "f1", s2, "f2", nullptr);
  Btype *s4 = mkBackendStruct(be, nullptr);

  // Create function type
  BFunctionType *befty1 = mkFuncTyp(be,
                                    L_PARM, s1,
                                    L_PARM, s2,
                                    L_PARM, s4,
                                    L_PARM, bu8t,
                                    L_PARM, bi8t,
                                    L_PARM, s3,
                                    L_RES, s2,
                                    L_END);
  Bfunction *func = h.mkFunction("foo", befty1);

  // sm1 == 0
  Bvariable *p3 = func->getNthParamVar(3);
  Location loc;
  Bexpression *vex = be->var_expression(p3, VE_rvalue, loc);
  Bexpression *c0 = be->convert_expression(bu8t, mkInt32Const(be, 0), loc);
  Bexpression *eq = be->binary_expression(OPERATOR_EQEQ, vex, c0, loc);

  // call
  Bexpression *fn = be->function_code_expression(func, loc);
  std::vector<Bexpression *> args;
  Bvariable *p0 = func->getNthParamVar(0);
  args.push_back(be->var_expression(p0, VE_rvalue, loc));

  Bvariable *p1 = func->getNthParamVar(1);
  args.push_back(be->var_expression(p1, VE_rvalue, loc));

  Bvariable *p2 = func->getNthParamVar(2);
  args.push_back(be->var_expression(p2, VE_rvalue, loc));

  Bvariable *p3x = func->getNthParamVar(3);
  Bexpression *vex3 = be->var_expression(p3x, VE_rvalue, loc);
  Bexpression *c1 = be->convert_expression(bu8t, mkInt32Const(be, 1), loc);
  Bexpression *minus = be->binary_expression(OPERATOR_MINUS, vex3, c1, loc);
  args.push_back(minus);

  Bvariable *p4 = func->getNthParamVar(4);
  args.push_back(be->var_expression(p4, VE_rvalue, loc));

  Bvariable *p5 = func->getNthParamVar(5);
  args.push_back(be->var_expression(p5, VE_rvalue, loc));
  Bexpression *call = be->call_expression(fn, args, nullptr, func, h.loc());


  // return y
  std::vector<Bexpression *> rvals1;
  rvals1.push_back(be->var_expression(p1, VE_rvalue, loc));
  Bstatement *rst1 = h.mkReturn(rvals1, FcnTestHarness::NoAppend);

  // return call
  std::vector<Bexpression *> rvals2;
  rvals2.push_back(call);
  Bstatement *rst2 = h.mkReturn(rvals2, FcnTestHarness::NoAppend);

  const char *exp = R"RAW_RESULT(
     %p2.ld.0 = load {}, {}* %p2
     %p3.ld.1 = load i8, i8* %p3.addr
     %sub.0 = sub i8 %p3.ld.1, 1
     %p4.ld.0 = load i8, i8* %p4.addr
     %cast.0 = bitcast { float, float, i16, i16, i16 }* %p0.addr to { <2 x float>, i48 }*
     %field0.0 = getelementptr inbounds { <2 x float>, i48 }, { <2 x float>, i48 }* %cast.0, i32 0, i32 0
     %ld.0 = load <2 x float>, <2 x float>* %field0.0
     %field1.0 = getelementptr inbounds { <2 x float>, i48 }, { <2 x float>, i48 }* %cast.0, i32 0, i32 1
     %ld.1 = load i48, i48* %field1.0
     %cast.1 = bitcast { double, float, float }* %p1.addr to { double, <2 x float> }*
     %field0.1 = getelementptr inbounds { double, <2 x float> }, { double, <2 x float> }* %cast.1, i32 0, i32 0
     %ld.2 = load double, double* %field0.1
     %field1.1 = getelementptr inbounds { double, <2 x float> }, { double, <2 x float> }* %cast.1, i32 0, i32 1
     %ld.3 = load <2 x float>, <2 x float>* %field1.1
     %call.0 = call { double, <2 x float> } @foo(<2 x float> %ld.0, i48 %ld.1, double %ld.2, <2 x float> %ld.3, i8 %sub.0, i8 %p4.ld.0, { { float, float, i16, i16, i16 }, { double, float, float } }* %p5)
     %cast.2 = bitcast { double, float, float }* %sret.0 to { double, <2 x float> }*
     store { double, <2 x float> } %call.0, { double, <2 x float> }* %cast.2
     %.ld.0 = load { double, float, float }, { double, float, float }* %sret.0
     store { double, float, float } %.ld.0, { double, float, float }* %sretv.1
     %cast.4 = bitcast { double, float, float }* %sretv.1 to { double, <2 x float> }*
     %ld.5 = load { double, <2 x float> }, { double, <2 x float> }* %cast.4
     ret { double, <2 x float> } %ld.5
    )RAW_RESULT";

  bool isOK = h.expectStmt(rst2, exp);
  EXPECT_TRUE(isOK && "Statement does not have expected contents");

  // if statement
  h.mkIf(eq, rst1, rst2);

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");

}

}
