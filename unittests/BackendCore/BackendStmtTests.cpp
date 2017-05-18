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
#include "gtest/gtest.h"

using namespace llvm;
using namespace goBackendUnitTests;

namespace {

TEST(BackendStmtTests, TestInitStmt) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Btype *bi64t = be->integer_type(false, 64);
  Location loc;

  // local variable with init
  Bvariable *loc1 = be->local_variable(func, "loc1", bi64t, true, loc);
  Bstatement *is = be->init_statement(func, loc1, mkInt64Const(be, 10));
  ASSERT_TRUE(is != nullptr);
  h.addStmt(is);
  EXPECT_EQ(repr(is), "store i64 10, i64* %loc1");

  // error handling
  Bvariable *loc2 = be->local_variable(func, "loc2", bi64t, true, loc);
  Bstatement *bad = be->init_statement(func, loc2, be->error_expression());
  ASSERT_TRUE(bad != nullptr);
  EXPECT_EQ(bad, be->error_statement());
  Bstatement *notsobad = be->init_statement(func, loc2, nullptr);
  h.addStmt(notsobad);

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendStmtTests, TestAssignmentStmt) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Btype *bi64t = be->integer_type(false, 64);
  Location loc;

  // assign a constant to a variable
  Bvariable *loc1 = h.mkLocal("loc1", bi64t);
  Bexpression *ve1 = be->var_expression(loc1, VE_lvalue, loc);
  Bstatement *as =
      be->assignment_statement(func, ve1, mkInt64Const(be, 123), loc);
  ASSERT_TRUE(as != nullptr);
  h.addStmt(as);

  // assign a variable to a variable
  Bvariable *loc2 = h.mkLocal("loc2", bi64t);
  Bexpression *ve2 = be->var_expression(loc2, VE_lvalue, loc);
  Bexpression *ve3 = be->var_expression(loc1, VE_rvalue, loc);
  Bstatement *as2 = be->assignment_statement(func, ve2, ve3, loc);
  ASSERT_TRUE(as2 != nullptr);
  h.addStmt(as2);

  const char *exp = R"RAW_RESULT(
    store i64 0, i64* %loc1
      store i64 123, i64* %loc1
      store i64 0, i64* %loc2
      %loc1.ld.0 = load i64, i64* %loc1
      store i64 %loc1.ld.0, i64* %loc2
   )RAW_RESULT";
  bool isOK = h.expectBlock(exp);
  EXPECT_TRUE(isOK && "Block does not have expected contents");

  // error handling
  Bvariable *loc3 = h.mkLocal("loc3", bi64t);
  Bexpression *ve4 = be->var_expression(loc3, VE_lvalue, loc);
  Bstatement *badas =
      be->assignment_statement(func, ve4, be->error_expression(), loc);
  ASSERT_TRUE(badas != nullptr);
  EXPECT_EQ(badas, be->error_statement());

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendStmtTests, TestReturnStmt) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *loc1 = h.mkLocal("loc1", bi64t, mkInt64Const(be, 10));

  // return loc1
  Location loc;
  Bexpression *ve1 = be->var_expression(loc1, VE_rvalue, loc);
  Bstatement *ret = h.mkReturn(ve1);

  const char *exp = R"RAW_RESULT(
     %loc1.ld.0 = load i64, i64* %loc1
     ret i64 %loc1.ld.0
   )RAW_RESULT";
  std::string reason;
  bool equal = difftokens(exp, repr(ret), reason);
  EXPECT_EQ("pass", equal ? "pass" : reason);

  // error handling
  std::vector<Bexpression *> bvals;
  bvals.push_back(be->error_expression());
  Bstatement *bret = be->return_statement(func, bvals, loc);
  EXPECT_EQ(bret, be->error_statement());

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendStmtTests, TestLabelGotoStmts) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  // loc1 = 10
  Location loc;
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *loc1 = h.mkLocal("loc1", bi64t, mkInt64Const(be, 10));

  // goto labeln
  Blabel *lab1 = be->label(func, "foolab", loc);
  Bstatement *gots = be->goto_statement(lab1, loc);
  h.addStmt(gots);

  // dead stmt: loc1 = 11
  h.mkAssign(be->var_expression(loc1, VE_lvalue, loc),
             mkInt64Const(be, 11));

  // labeldef
  Bstatement *ldef = be->label_definition_statement(lab1);
  h.addStmt(ldef);

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendStmtTests, TestIfStmt) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Location loc;
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *loc1 = h.mkLocal("loc1", bi64t);

  // loc1 = 123
  Bexpression *ve1 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *c123 = mkInt64Const(be, 123);
  Bstatement *as1 = be->assignment_statement(func, ve1, c123, loc);

  // loc1 = 987
  Bexpression *ve2 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *c987 = mkInt64Const(be, 987);
  Bstatement *as2 = be->assignment_statement(func, ve2, c987, loc);

  // loc1 = 456
  Bexpression *ve3 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *c456 = mkInt64Const(be, 456);
  Bstatement *as3 = be->assignment_statement(func, ve3, c456, loc);

  // if true b1 else b2
  Bexpression *trueval = be->boolean_constant_expression(true);
  Bstatement *ifst = h.mkIf(trueval, as1, as2, FcnTestHarness::NoAppend);

  // if true if
  Bexpression *tv2 = be->boolean_constant_expression(true);
  h.mkIf(tv2, ifst, as3);

  // return 10101
  h.mkReturn(mkInt64Const(be, 10101));

  bool broken = h.finish(StripDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");

  // verify
  const char *exp = R"RAW_RESULT(
    define i64 @foo(i8* nest %nest.0, i32 %param1, i32 %param2, i64* %param3) #0 {
    entry:
      %param1.addr = alloca i32
      %param2.addr = alloca i32
      %param3.addr = alloca i64*
      %loc1 = alloca i64
      store i32 %param1, i32* %param1.addr
      store i32 %param2, i32* %param2.addr
      store i64* %param3, i64** %param3.addr
      store i64 0, i64* %loc1
      br i1 true, label %then.0, label %else.0
    then.0:                                           ; preds = %entry
      br i1 true, label %then.1, label %else.1
    fallthrough.0:                 ; preds = %else.0, %fallthrough.1
      ret i64 10101
    else.0:                                           ; preds = %entry
      store i64 456, i64* %loc1
      br label %fallthrough.0
    then.1:                                           ; preds = %then.0
      store i64 123, i64* %loc1
      br label %fallthrough.1
    fallthrough.1:                             ; preds = %else.1, %then.1
      br label %fallthrough.0
    else.1:                                           ; preds = %then.0
      store i64 987, i64* %loc1
      br label %fallthrough.1
    }
    )RAW_RESULT";

  bool isOK = h.expectValue(func->function(), exp);
  EXPECT_TRUE(isOK && "Function does not have expected contents");
}

TEST(BackendStmtTests, TestSwitchStmt) {
  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Location loc;
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *loc1 = h.mkLocal("loc1", bi64t);

  // loc1 = loc1 / 123
  Bexpression *ve1 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *ve1r = be->var_expression(loc1, VE_rvalue, loc);
  Bexpression *c123 = mkInt64Const(be, 123);
  Bexpression *div = be->binary_expression(OPERATOR_DIV, ve1r, c123, loc);
  Bstatement *as1 = be->assignment_statement(func, ve1, div, loc);

  // loc1 = 987 * loc1
  Bexpression *ve2 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *ve2r = be->var_expression(loc1, VE_rvalue, loc);
  Bexpression *c987 = mkInt64Const(be, 987);
  Bexpression *mul = be->binary_expression(OPERATOR_MULT, c987, ve2r, loc);
  Bstatement *as2 = be->assignment_statement(func, ve2, mul, loc);

  // loc1 = 456
  Bexpression *ve3 = be->var_expression(loc1, VE_lvalue, loc);
  Bexpression *c456 = mkInt64Const(be, 456);
  Bstatement *as3 = be->assignment_statement(func, ve3, c456, loc);

  // Set up switch statements
  std::vector<Bstatement*> statements = {as1, as2, as3};
  std::vector<std::vector<Bexpression*> > cases = {
    { mkInt64Const(be, 1), mkInt64Const(be, 2) },
    { mkInt64Const(be, 3), mkInt64Const(be, 4) } };
  cases.push_back(std::vector<Bexpression*>()); // default

  // switch
  Bexpression *vesw = be->var_expression(loc1, VE_rvalue, loc);
  h.mkSwitch(vesw, cases, statements);

  // return 10101
  h.mkReturn(mkInt64Const(be, 10101));

  bool broken = h.finish(StripDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");

  // verify
  const char *exp = R"RAW_RESULT(
   define i64 @foo(i8* nest %nest.0, i32 %param1, i32 %param2, i64* %param3) #0 {
   entry:
     %param1.addr = alloca i32
     %param2.addr = alloca i32
     %param3.addr = alloca i64*
     %loc1 = alloca i64
     store i32 %param1, i32* %param1.addr
     store i32 %param2, i32* %param2.addr
     store i64* %param3, i64** %param3.addr
     store i64 0, i64* %loc1
     %loc1.ld.2 = load i64, i64* %loc1
     switch i64 %loc1.ld.2, label %default.0 [
       i64 1, label %case.0
       i64 2, label %case.0
       i64 3, label %case.1
       i64 4, label %case.1
     ]
   case.0:                               ; preds = %entry, %entry
     %loc1.ld.0 = load i64, i64* %loc1
     %div.0 = sdiv i64 %loc1.ld.0, 123
     store i64 %div.0, i64* %loc1
     br label %epilog.0
   case.1:                               ; preds = %entry, %entry
     %loc1.ld.1 = load i64, i64* %loc1
     %mul.0 = mul i64 987, %loc1.ld.1
     store i64 %mul.0, i64* %loc1
     br label %epilog.0
   default.0:                            ; preds = %entry
     store i64 456, i64* %loc1
     br label %epilog.0
   epilog.0:                             ; preds = %default.0, %case.1, %case.0
     ret i64 10101
   }
    )RAW_RESULT";

  bool isOK = h.expectValue(func->function(), exp);
  EXPECT_TRUE(isOK && "Function does not have expected contents");
}

static Bstatement *CreateDeferStmt(Llvm_backend *be,
                                   FcnTestHarness &h,
                                   Bfunction *func,
                                   Bvariable *loc1)
{
  Location loc;
  Btype *bi8t = be->integer_type(false, 8);

  // Declare checkdefer, deferreturn
  Btype *befty = mkFuncTyp(be, L_PARM, be->pointer_type(bi8t), L_END);
  bool is_decl = true; bool is_inl = false;
  bool is_vis = true; bool is_split = true;
  Bfunction *bchkfcn = be->function(befty, "checkdefer", "checkdefer",
                                    is_vis, is_decl, is_inl, is_split,
                                    false, h.newloc());
  Bfunction *bdefretfcn = be->function(befty, "deferreturn", "deferreturn",
                                       is_vis, is_decl, is_inl, is_split,
                                       false, h.newloc());

  // Materialize call to deferreturn
  Bexpression *retfn = be->function_code_expression(bdefretfcn, h.newloc());
  std::vector<Bexpression *> args1;
  Bexpression *ve1 = be->var_expression(loc1, VE_rvalue, h.newloc());
  Bexpression *adve1 = be->address_expression(ve1, h.newloc());
  args1.push_back(adve1);
  Bexpression *undcall = be->call_expression(func, retfn, args1,
                                             nullptr, h.newloc());

  // Materialize call to checkdefer
  Bexpression *ckfn = be->function_code_expression(bchkfcn, h.newloc());
  std::vector<Bexpression *> args2;
  Bexpression *ve2 = be->var_expression(loc1, VE_rvalue, h.loc());
  Bexpression *adve2 = be->address_expression(ve2, h.loc());
  args2.push_back(adve2);
  Bexpression *ckdefcall = be->call_expression(func, ckfn, args2,
                                               nullptr, h.loc());

  // Defer statement based on the calls above.
  Bstatement *defer = be->function_defer_statement(func,
                                                   undcall,
                                                   ckdefcall,
                                                   h.newloc());
  return defer;
}

TEST(BackendStmtTests, TestDeferStmt) {
  FcnTestHarness h;
  Llvm_backend *be = h.be();
  BFunctionType *befty = mkFuncTyp(be, L_END);
  Bfunction *func = h.mkFunction("foo", befty);
  Btype *bi8t = be->integer_type(false, 8);
  Bvariable *loc1 = h.mkLocal("x", bi8t);

  Bstatement *defer = CreateDeferStmt(be, h, func, loc1);
  h.addStmt(defer);

  bool broken = h.finish(StripDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");

  const char *exp = R"RAW_RESULT(
define void @foo(i8* nest %nest.0) #0 personality i32 (i32, i32, i64, i8*, i8*)* @__gccgo_personality_v0 {
entry:
  %x = alloca i8
  store i8 0, i8* %x
  br label %finish.0

finish.0:                                         ; preds = %catch.0, %entry
  invoke void @checkdefer(i8* nest undef, i8* %x)
          to label %cont.0 unwind label %pad.0

pad.0:                                            ; preds = %finish.0
  %ex.0 = landingpad { i8*, i32 }
          catch i8* null
  br label %catch.0

catch.0:                                          ; preds = %pad.0
  call void @deferreturn(i8* nest undef, i8* %x)
  br label %finish.0

cont.0:                                           ; preds = %finish.0
  ret void
}
   )RAW_RESULT";

  bool isOK = h.expectValue(func->function(), exp);
  EXPECT_TRUE(isOK && "Function does not have expected contents");
}

TEST(BackendStmtTests, TestExceptionHandlingStmt) {
  FcnTestHarness h;
  Llvm_backend *be = h.be();
  BFunctionType *befty = mkFuncTyp(be, L_END);
  Bfunction *func = h.mkFunction("baz", befty);
  Btype *bi64t = be->integer_type(false, 64);
  Bvariable *loc1 = h.mkLocal("x", bi64t);
  BFunctionType *befty2 = mkFuncTyp(be,
                                    L_PARM, bi64t,
                                    L_RES, bi64t,
                                    L_END);

  bool is_decl = true; bool is_inl = false;
  bool is_vis = true; bool is_split = true;
  const char *fnames[] = { "plark", "plix", "ohstopit" };
  Bfunction *fcns[4];
  Bexpression *calls[4];
  for (unsigned ii = 0; ii < 3; ++ii)  {
    fcns[ii] = be->function(befty, fnames[ii], fnames[ii],
                                    is_vis, is_decl, is_inl, is_split,
                                    false, h.newloc());
    Bexpression *pfn = be->function_code_expression(fcns[ii], h.newloc());
    std::vector<Bexpression *> args;
    calls[ii] = be->call_expression(func, pfn, args,
                                    nullptr, h.newloc());
  }
  fcns[3] = be->function(befty2, "id", "id",
                         is_vis, is_decl, is_inl, is_split,
                         false, h.newloc());
  Bexpression *idfn = be->function_code_expression(fcns[3], h.newloc());
  std::vector<Bexpression *> iargs;
  iargs.push_back(mkInt64Const(be, 99));
  calls[3] = be->call_expression(func, idfn, iargs,
                                 nullptr, h.newloc());

  // body:
  // x = id(99)
  // plark()
  // x = 123
  Bexpression *ve1 = be->var_expression(loc1, VE_lvalue, h.newloc());
  Bstatement *as1 =
      be->assignment_statement(func, ve1, calls[3], h.newloc());
  Bblock *bb1 = mkBlockFromStmt(be, func, as1);
  addStmtToBlock(be, bb1, h.mkExprStmt(calls[0], FcnTestHarness::NoAppend));
  Bexpression *ve2 = be->var_expression(loc1, VE_lvalue, h.newloc());
  Bstatement *as2 =
      be->assignment_statement(func, ve2, mkInt64Const(be, 123), h.newloc());
  addStmtToBlock(be, bb1, as2);
  Bstatement *body = be->block_statement(bb1);

  // catch:
  // plix()
  Bstatement *catchst = h.mkExprStmt(calls[1], FcnTestHarness::NoAppend);

  // finally:
  // ohstopit()
  Bstatement *finally = h.mkExprStmt(calls[2], FcnTestHarness::NoAppend);

  // Now exception statement
  Bstatement *est =
      be->exception_handler_statement(body, catchst, finally, h.newloc());

  h.addStmt(est);

  bool broken = h.finish(StripDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");

  const char *exp = R"RAW_RESULT(
define void @baz(i8* nest %nest.0) #0 personality i32 (i32, i32, i64, i8*, i8*)* @__gccgo_personality_v0 {
entry:
  %x = alloca i64
  store i64 0, i64* %x
  %call.0 = invoke i64 @id(i8* nest undef, i64 99)
          to label %cont.0 unwind label %pad.0

pad.0:                                            ; preds = %cont.0, %entry
  %ex.0 = landingpad { i8*, i32 }
          catch i8* null
  br label %catch.0

finally.0:                                        ; preds = %catchpad.0, %cont.2, %cont.1
  call void @ohstopit(i8* nest undef)
  ret void

cont.0:                                           ; preds = %entry
  store i64 %call.0, i64* %x
  invoke void @plark(i8* nest undef)
          to label %cont.1 unwind label %pad.0

cont.1:                                           ; preds = %cont.0
  store i64 123, i64* %x
  br label %finally.0

catch.0:                                          ; preds = %pad.0
  invoke void @plix(i8* nest undef)
          to label %cont.2 unwind label %catchpad.0

catchpad.0:                                       ; preds = %catch.0
  %ex2.0 = landingpad { i8*, i32 }
          catch i8* null
  br label %finally.0

cont.2:                                           ; preds = %catch.0
  br label %finally.0
}
   )RAW_RESULT";

  bool isOK = h.expectValue(func->function(), exp);
  EXPECT_TRUE(isOK && "Function does not have expected contents");
}

static Bstatement *mkMemReturn(Llvm_backend *be,
                                 Bfunction *func,
                                 Bvariable *rtmp,
                                 Bexpression *val)
{
  // Store value to tmp
  Location loc;
  Bexpression *ve = be->var_expression(rtmp, VE_lvalue, loc);
  Bstatement *as = be->assignment_statement(func, ve, val, loc);
  Bblock *block = mkBlockFromStmt(be, func, as);

  // Load from temp and return
  Bexpression *ve2 = be->var_expression(rtmp, VE_rvalue, loc);
  std::vector<Bexpression *> vals;
  vals.push_back(ve2);
  Bstatement *rst = be->return_statement(func, vals, loc);
  addStmtToBlock(be, block, rst);
  return be->block_statement(block);
}

TEST(BackendStmtTests, TestExceptionHandlingStmtWithReturns) {
  FcnTestHarness h;
  Llvm_backend *be = h.be();
  Btype *bi64t = be->integer_type(false, 64);
  BFunctionType *befty = mkFuncTyp(be,
                                    L_PARM, bi64t,
                                    L_RES, bi64t,
                                    L_END);
  Bfunction *func = h.mkFunction("baz", befty);
  Bvariable *rtmp = h.mkLocal("ret", bi64t);

  bool is_decl = true; bool is_inl = false;
  bool is_vis = true; bool is_split = true;
  Bfunction *sfn = be->function(befty, "splat", "splat",
                                is_vis, is_decl, is_inl, is_split,
                                false, h.newloc());
  Bexpression *splfn = be->function_code_expression(sfn, h.newloc());

  // body:
  // if splat(99) == 88 {
  //   return 22
  // } else {
  //   return parm
  // }
  Bstatement *body = nullptr;
  {
    // call to splat
    std::vector<Bexpression *> iargs;
    iargs.push_back(mkInt64Const(be, 99));
    Bexpression *splcall = be->call_expression(func, splfn, iargs,
                                               nullptr, h.newloc());
    Bexpression *eq = be->binary_expression(OPERATOR_EQEQ,
                                          splcall,
                                          mkInt64Const(be, 88),
                                          h.loc());
    Bvariable *p0 = func->getNthParamVar(0);
    Bexpression *ve1 = be->var_expression(p0, VE_rvalue, h.newloc());

    Bstatement *retparm = mkMemReturn(be, func, rtmp, ve1);
    Bstatement *ret22 = mkMemReturn(be, func, rtmp, mkInt64Const(be, 22));
    Bblock *thenblock = mkBlockFromStmt(be, func, ret22);
    Bblock *elseblock = mkBlockFromStmt(be, func, retparm);
    Bstatement *ifst = be->if_statement(func, eq,
                                        thenblock, elseblock, h.newloc());
    body = ifst;
  }

  // catch:
  // return splat(13)
  Bstatement *catchst = nullptr;
  {
    std::vector<Bexpression *> args;
    args.push_back(mkInt64Const(be, 13));
    Bexpression *splcall = be->call_expression(func, splfn, args,
                                               nullptr, h.newloc());
    catchst = mkMemReturn(be, func, rtmp, splcall);
  }

  // finally:
  // if splat(987) == 2 {
  //   return 9
  // }
  Bstatement *finally = nullptr;
  {
    std::vector<Bexpression *> args;
    args.push_back(mkInt64Const(be, 987));
    Bexpression *splcall = be->call_expression(func, splfn, args,
                                               nullptr, h.newloc());
    Bexpression *eq = be->binary_expression(OPERATOR_EQEQ,
                                            splcall,
                                            mkInt64Const(be, 2),
                                            h.loc());
    Bstatement *ret9 = mkMemReturn(be, func, rtmp, mkInt64Const(be, 9));
    Bblock *thenblock = mkBlockFromStmt(be, func, ret9);
    Bblock *elseblock = nullptr;
    Bstatement *ifst = be->if_statement(func, eq,
                                      thenblock, elseblock, h.newloc());
    finally = ifst;
  }

  // Now exception statement
  Bstatement *est =
      be->exception_handler_statement(body, catchst, finally, h.newloc());
  h.addStmt(est);

  bool broken = h.finish(StripDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");

  const char *exp = R"RAW_RESULT(
define i64 @baz(i8* nest %nest.0, i64 %p0) #0 personality i32 (i32, i32, i64, i8*, i8*)* @__gccgo_personality_v0 {
entry:
  %p0.addr = alloca i64
  %ret = alloca i64
  store i64 %p0, i64* %p0.addr
  store i64 0, i64* %ret
  %call.0 = invoke i64 @splat(i8* nest undef, i64 99)
          to label %cont.0 unwind label %pad.0

pad.0:                                            ; preds = %entry
  %ex.0 = landingpad { i8*, i32 }
          catch i8* null
  br label %catch.0

finally.0:                                        ; preds = %catchpad.0, %cont.1, %fallthrough.0, %else.0, %then.0
  %call.2 = call i64 @splat(i8* nest undef, i64 987)
  %icmp.1 = icmp eq i64 %call.2, 2
  %zext.1 = zext i1 %icmp.1 to i8
  %trunc.1 = trunc i8 %zext.1 to i1
  br i1 %trunc.1, label %then.1, label %else.1

cont.0:                                           ; preds = %entry
  %icmp.0 = icmp eq i64 %call.0, 88
  %zext.0 = zext i1 %icmp.0 to i8
  %trunc.0 = trunc i8 %zext.0 to i1
  br i1 %trunc.0, label %then.0, label %else.0

then.0:                                           ; preds = %cont.0
  store i64 22, i64* %ret
  br label %finally.0

fallthrough.0:                                    ; No predecessors!
  br label %finally.0

else.0:                                           ; preds = %cont.0
  %p0.ld.0 = load i64, i64* %p0.addr
  store i64 %p0.ld.0, i64* %ret
  br label %finally.0

catch.0:                                          ; preds = %pad.0
  %call.1 = invoke i64 @splat(i8* nest undef, i64 13)
          to label %cont.1 unwind label %catchpad.0

catchpad.0:                                       ; preds = %catch.0
  %ex2.0 = landingpad { i8*, i32 }
          catch i8* null
  br label %finally.0

cont.1:                                           ; preds = %catch.0
  store i64 %call.1, i64* %ret
  br label %finally.0

then.1:                                           ; preds = %finally.0
  store i64 9, i64* %ret
  %ret.ld.3 = load i64, i64* %ret
  ret i64 %ret.ld.3

fallthrough.1:                                    ; preds = %else.1
  %ret.ld.1 = load i64, i64* %ret
  ret i64 %ret.ld.1

else.1:                                           ; preds = %finally.0
  br label %fallthrough.1
}
   )RAW_RESULT";

  bool isOK = h.expectValue(func->function(), exp);
  EXPECT_TRUE(isOK && "Function does not have expected contents");
}

}
