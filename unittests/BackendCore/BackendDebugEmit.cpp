//===- llvm/tools/dragongo/unittests/BackendCore/BackendDebugEmit.cpp -----===//
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

using namespace goBackendUnitTests;

namespace {

// Insure that dbg.declare is emitted for a used-defined local
// variable. Remark: I worry that this unit test may be too brittle
// (vulnerable to spurious failures if other things in the bridge are
// changed). Perhaps there is some other way to verify this
// functionality.

TEST(BackendDebugEmit, TestSimpleDecl) {
  FcnTestHarness h;
  Llvm_backend *be = h.be();
  BFunctionType *befty = mkFuncTyp(be, L_END);
  Bfunction *func = h.mkFunction("foo", befty);

  Btype *bu32t = be->integer_type(true, 32);
  h.mkLocal("x", bu32t);

  const char *exp = R"RAW_RESULT(
      define void @foo(i8* nest %nest.0) #0 {
      entry:
        %x = alloca i32
        store i32 0, i32* %x
        call void @llvm.dbg.declare(metadata i32* %x, metadata !3,
                                    metadata !11), !dbg !12
        ret void
      }
  )RAW_RESULT";

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");

  bool isOK = h.expectValue(func->function(), exp);
  EXPECT_TRUE(isOK && "Function does not have expected contents");
}

// This test is designed to make sure that debug meta-data generation
// handles corner clases like vars with zero size (empty struct).

// working propery
TEST(BackendDebugEmit, MoreComplexVarDecls) {

  FcnTestHarness h;
  Llvm_backend *be = h.be();

  Btype *bi32t = be->integer_type(false, 32);
  Btype *set = mkBackendStruct(be, nullptr); // struct with no fields
  Bexpression *val10 = mkInt64Const(be, int64_t(10));
  Btype *beat = be->array_type(set, val10);

  BFunctionType *befty1 = mkFuncTyp(be,
                                    L_RES, set,
                                    L_PARM, set,
                                    L_PARM, beat,
                                    L_PARM, bi32t,
                                    L_PARM, set,
                                    L_PARM, beat,
                                    L_END);
  Bfunction *func = h.mkFunction("foo", befty1);
  BFunctionType *befty2 = mkFuncTyp(be,
                                    L_RES, set,
                                    L_END);
  Bfunction *func2 = mkFuncFromType(be, "bar", befty2);

  h.mkLocal("la", set);
  h.mkLocal("lb", beat);
  h.mkLocal("lc", bi32t);

  Location loc;
  std::vector<Bvariable *> vlist;
  vlist.push_back(be->local_variable(func, "n1", set, false, loc));
  vlist.push_back(be->local_variable(func, "n2", beat, false, loc));
  vlist.push_back(be->local_variable(func, "n3", bi32t, false, loc));
  h.newBlock(&vlist);

  Bexpression *fn2 = be->function_code_expression(func2, loc);
  std::vector<Bexpression *> noargs;
  Bexpression *call2 =
      be->call_expression(func2, fn2, noargs, nullptr,  h.loc());
  h.addStmt(be->init_statement(func, vlist[0], call2));
  h.addStmt(be->init_statement(func, vlist[1], be->zero_expression(beat)));
  h.addStmt(be->init_statement(func, vlist[2], be->zero_expression(bi32t)));

  // return foo(f1, f2, 4, f1, f2)
  Bexpression *fn = be->function_code_expression(func, loc);
  Bvariable *p0 = func->getNthParamVar(0);
  Bvariable *p1 = func->getNthParamVar(1);
  std::vector<Bexpression *> args;
  args.push_back(be->var_expression(p0, VE_rvalue, loc));
  args.push_back(be->var_expression(p1, VE_rvalue, loc));
  args.push_back(mkInt32Const(be, 4));
  args.push_back(be->var_expression(p0, VE_rvalue, loc));
  args.push_back(be->var_expression(p1, VE_rvalue, loc));
  Bexpression *call = be->call_expression(func, fn, args, nullptr, h.loc());
  std::vector<Bexpression *> rvals;
  rvals.push_back(call);
  h.mkReturn(rvals);

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");

  std::string fdump = repr(func->function());
  std::vector<std::string> tokens = tokenize(fdump);
  unsigned declcount = 0;
  for (auto t : tokens)
    if (t == "@llvm.dbg.declare(metadata")
      declcount += 1;

  // five formals and six locals => 11 var decls
  EXPECT_EQ(declcount, 11u);
  if (declcount != 11)
    std::cerr << fdump;
}

}
