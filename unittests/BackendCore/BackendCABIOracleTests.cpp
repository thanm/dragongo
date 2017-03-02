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
    CABIOracle cab(befty1, be->typeManager(), llvm::CallingConv::X86_64_SysV);
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
            const char *t) : results(r), parms(p), exp(t) { }
    std::vector<Btype*> results;
    std::vector<Btype*> parms;
    const char *exp;
  };

  Btype *nt = nullptr;
  std::vector<FcnItem> items = {

    FcnItem( {  }, {  }, "void ()"),

    FcnItem( { bi8t }, { }, "i8 ()"),

    FcnItem( {  }, { bi8t }, "void (i8)"),

    FcnItem({ bi8t, bf64t }, { bi8t, bu8t, st0 }, "{ i8, double } (i8, i8)"),

    FcnItem({ st2 }, { st2, st0, st4, st1 },
            "{ double, double } (double, double, <2 x float>, i64)"),

    FcnItem({ st3 }, { st3, st0, bu8t },
            "{ { double, double }, i8 }* ({ { double, double }, i8 }*, i8)"),

    FcnItem({ st3 }, { st3, st0, bu8t },
            "{ { double, double }, i8 }* ({ { double, double }, i8 }*, i8)"),
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
    CABIOracle cab(bft, be->typeManager(), llvm::CallingConv::X86_64_SysV);
    cab.dump();
    EXPECT_EQ(repr(cab.getFunctionTypeForABI()), item.exp);
  }
}

}
