//===- llvm/tools/dragongo/unittests/BackendCore/BackendVarTests.cpp ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TestUtils.h"
#include "go-llvm-backend.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace goBackendUnitTests;

namespace {

TEST(BackendVarTests, MakeLocalVar) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));
  Bfunction *func1 = mkFunci32o64(be.get(), "foo");
  Bfunction *func2 = mkFunci32o64(be.get(), "bar");

  // Manufacture some locals
  Location loc;
  Btype *bi64t = be->integer_type(false, 64);
  Btype *bst = mkBackendThreeFieldStruct(be.get());
  Bvariable *loc1 = be->local_variable(func1, "loc1", bi64t, true, loc);
  ASSERT_TRUE(loc1 != nullptr);
  EXPECT_TRUE(loc1 != be->error_variable());
  Bvariable *loc2 = be->local_variable(func1, "loc2", bst, false, loc);
  ASSERT_TRUE(loc2 != nullptr);
  EXPECT_TRUE(loc2 != be->error_variable());
  Bvariable *loc3 = be->local_variable(func2, "loc3", bst, false, loc);
  ASSERT_TRUE(loc3 != nullptr);
  EXPECT_TRUE(loc3 != be->error_variable());

  // Examine resulting alloca instructions
  EXPECT_TRUE(isa<AllocaInst>(loc1->value()));
  EXPECT_TRUE(isa<AllocaInst>(loc2->value()));
  EXPECT_TRUE(loc1 != loc2 && loc1->value() != loc2->value());

  // Test var_expression created from local variable
  Bexpression *ve1 = be->var_expression(loc1, VE_lvalue, Location());
  ASSERT_TRUE(ve1 != nullptr);
  EXPECT_EQ(ve1->value(), loc1->value());

  // Test var_expression created from local variable
  Bexpression *ve2 = be->var_expression(loc1, VE_rvalue, Location());
  ASSERT_TRUE(ve2 != nullptr);
  Bstatement *es = be->expression_statement(func1, ve2);
  Bblock *block = mkBlockFromStmt(be.get(), func1, es);
  EXPECT_EQ(repr(ve2->value()), "%loc1 = alloca i64");
  EXPECT_EQ(repr(es), "%loc1.ld.0 = load i64, i64* %loc1");

  // Make sure error detection is working
  Bvariable *loce = be->local_variable(func1, "", be->error_type(), true, loc);
  EXPECT_TRUE(loce == be->error_variable());

  be->function_set_body(func1, block);
}

TEST(BackendVarTests, MakeParamVar) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));
  bool dontMakeParams = false;
  Bfunction *func = mkFunci32o64(be.get(), "foo", dontMakeParams);

  // Add params for the function
  Btype *bi32t = be->integer_type(false, 32);
  Bvariable *p1 = be->parameter_variable(func, "p1", bi32t, false, Location());
  Bvariable *p2 = be->parameter_variable(func, "p2", bi32t, false, Location());
  ASSERT_TRUE(p1 != nullptr);
  ASSERT_TRUE(p2 != nullptr);
  EXPECT_TRUE(p1 != p2);
  EXPECT_TRUE(p1 != be->error_variable());

  // Values for param variables will be the alloca instructions
  // created to capture their values
  EXPECT_TRUE(isa<AllocaInst>(p1->value()));
  EXPECT_TRUE(isa<AllocaInst>(p2->value()));

  // Test var_expression created from param variable
  Bexpression *ve1 = be->var_expression(p1, VE_lvalue, Location());
  ASSERT_TRUE(ve1 != nullptr);
  EXPECT_EQ(ve1->value(), p1->value());

  // Error handling
  Bfunction *func2 = mkFunci32o64(be.get(), "bar");
  Bvariable *p3 =
      be->parameter_variable(func2, "p3", be->error_type(), false, Location());
  EXPECT_TRUE(p3 == be->error_variable());
}

TEST(BackendVarTests, MakeGlobalVar) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  Btype *bi32t = be->integer_type(false, 32);
  Bvariable *g1 =
      be->global_variable("varname", "asmname", bi32t, false, /* is_external */
                          false,                              /* is_hidden */
                          false, /* unique_section */
                          Location());
  ASSERT_TRUE(g1 != nullptr);
  Value *g1val = g1->value();
  ASSERT_TRUE(g1val != nullptr);
  EXPECT_TRUE(isa<GlobalVariable>(g1val));
  EXPECT_EQ(g1val->getName(), "asmname");

  // Set initializer
  be->global_variable_set_init(g1, mkInt32Const(be.get(), 101));

  // Test var_expression created from global variable
  Bexpression *ve1 = be->var_expression(g1, VE_lvalue, Location());
  ASSERT_TRUE(ve1 != nullptr);
  EXPECT_EQ(ve1->value(), g1->value());

  // error case
  Bvariable *gerr =
      be->global_variable("", "", be->error_type(), false, /* is_external */
                          false,                           /* is_hidden */
                          false,                           /* unique_section */
                          Location());
  EXPECT_TRUE(gerr == be->error_variable());

  // debugging
  if (!gerr) {
    GlobalVariable *gv = cast<GlobalVariable>(g1val);
    ASSERT_TRUE(gv != nullptr);
    Module *m = gv->getParent();
    m->dump();
  }
}

TEST(BackendVarTests, MakeTemporaryVar) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));
  Bfunction *func = mkFunci32o64(be.get(), "foo");

  // var b bool = true
  Location loc;
  Btype *boolt = be->bool_type();
  Bvariable *bv = be->local_variable(func, "b", boolt, true, loc);
  Bexpression *trueval = be->boolean_constant_expression(true);
  Bstatement *is1 = be->init_statement(func, bv, trueval);
  Bblock *block = mkBlockFromStmt(be.get(), func, is1);

  // temporary var [uint64] = 99
  Btype *bu64t = be->integer_type(true, 64);
  Bstatement *inits = nullptr;
  Bexpression *con64 = mkUint64Const(be.get(), 64);
  Bvariable *tvar = be->temporary_variable(func, block, bu64t, con64,
                                           false, loc, &inits);
  ASSERT_TRUE(tvar != nullptr);
  ASSERT_TRUE(inits != nullptr);
  EXPECT_EQ(repr(inits), "store i64 64, i64* %tmpv.0");

  addStmtToBlock(be.get(), block, inits);

  be->function_set_body(func, block);
}

TEST(BackendVarTests, MakeImmutableStruct) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  Btype *bi32t = be->integer_type(false, 32);
  Btype *bst = mkTwoFieldStruct(be.get(), bi32t, bi32t);

  const bool is_common[2] = {true, false};
  const bool is_hidden[2] = {true, false};
  Location loc;
  GlobalVariable *gvar = nullptr;
  bool first = true;
  for (auto hidden : is_hidden) {
    for (auto common : is_common) {
      if (hidden && common)
        continue;
      Bvariable *ims =
          be->immutable_struct("name", "asmname", hidden, common, bst, loc);
      ASSERT_TRUE(ims != nullptr);
      Value *ival = ims->value();
      ASSERT_TRUE(ival != nullptr);
      EXPECT_TRUE(isa<GlobalVariable>(ival));
      if (first) {
        gvar = cast<GlobalVariable>(ival);
        EXPECT_EQ(ival->getName(), "asmname");
        first = false;
      }
    }
  }

  // error case
  Bvariable *gerr =
      be->immutable_struct("", "", false, false, be->error_type(), Location());
  EXPECT_TRUE(gerr == be->error_variable());

  // debugging
  if (!gerr) {
    Module *m = gvar->getParent();
    m->dump();
  }
}

TEST(BackendVarTests, MakeImplicitVariable) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  Btype *bi32t = be->integer_type(false, 32);
  Btype *bst = mkTwoFieldStruct(be.get(), bi32t, bi32t);

  const bool is_common[2] = {true, false};
  const bool is_constant[2] = {true, false};
  const bool is_hidden[2] = {true, false};
  GlobalVariable *gvar = nullptr;
  bool first = true;
  for (auto hidden : is_hidden) {
    for (auto common : is_common) {
      for (auto iscon : is_constant) {
        if (hidden && common)
          continue;
        Bvariable *ims =
            be->implicit_variable("name", "asmname", bst, hidden, iscon,
                                  common, 8);
        ASSERT_TRUE(ims != nullptr);
        Value *ival = ims->value();
        ASSERT_TRUE(ival != nullptr);
        EXPECT_TRUE(isa<GlobalVariable>(ival));
        if (first) {
          gvar = cast<GlobalVariable>(ival);
          EXPECT_EQ(ival->getName(), "asmname");
          first = false;
        }
      }
    }
  }

  // error case
  Bvariable *gerr =
      be->implicit_variable("", "", be->error_type(), false, false,
                            false, 0);
  EXPECT_TRUE(gerr == be->error_variable());
}

TEST(BackendVarTests, MakeImmutableStructReference) {
  LLVMContext C;

  std::unique_ptr<Llvm_backend> be(new Llvm_backend(C));

  Location loc;
  Btype *bi32t = be->integer_type(false, 32);
  Btype *bst = mkTwoFieldStruct(be.get(), bi32t, bi32t);
  Bvariable *ims =
      be->immutable_struct_reference("name", "asmname", bst, loc);
  ASSERT_TRUE(ims != nullptr);
  Value *ival = ims->value();
  ASSERT_TRUE(ival != nullptr);
  EXPECT_TRUE(isa<GlobalVariable>(ival));
  EXPECT_EQ(repr(ival), "@asmname = external constant { i32, i32 }");

  // error case
  Bvariable *ierr =
      be->immutable_struct_reference("name", "asmname", be->error_type(), loc);
  EXPECT_TRUE(ierr == be->error_variable());
}

TEST(BackendVarTests, ImmutableStructSetInit) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Bfunction *func = h.func();

  Location loc;
  Btype *bt = be->bool_type();
  Btype *pbt = be->pointer_type(bt);
  Btype *uintptrt = be->integer_type(true, be->type_size(pbt));
  Btype *desct = mkBackendStruct(be, uintptrt, "x", nullptr);
  Bvariable *ims = be->immutable_struct("desc", "desc",
                                        false, false, desct, loc);
  Bexpression *fp = be->function_code_expression(func, loc);
  Bexpression *confp = be->convert_expression(uintptrt, fp, loc);

  std::vector<Bexpression *> vals;
  vals.push_back(confp);
  Bexpression *scon = be->constructor_expression(desct, vals, loc);
  be->immutable_struct_set_init(ims, "", false, false,
                                desct, loc, scon);

  // Q: do we want weak_odr here?
  const char *exp = R"RAW_RESULT(
    @desc = weak_odr constant { i64 } { i64 ptrtoint
    (i64 (i32, i32, i64*)* @foo to i64) }
  )RAW_RESULT";

  bool isOK = h.expectValue(ims->value(), exp);
  EXPECT_TRUE(isOK && "Value does not have expected contents");

  // check that these don't crash
  be->immutable_struct_set_init(be->error_variable(), "", false, false,
                                desct, loc, scon);
  be->immutable_struct_set_init(ims, "", false, false,
                                desct, loc, be->error_expression());

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendVarTests, ImplicitVariableSetInit) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();

  Location loc;

  Btype *bi32t = be->integer_type(false, 32);
  Btype *bst = mkTwoFieldStruct(be, bi32t, bi32t);

  // Case 1: non-common, concrete init value.
  bool isConst = false;
  bool isHidden = false;
  bool isCommon = false;

  Bvariable *ims1 =
      be->implicit_variable("first", "v1", bst,
                            isHidden, isConst, isCommon, 8);
  std::vector<Bexpression *> vals1;
  vals1.push_back(mkInt32Const(be, 101));
  vals1.push_back(mkInt32Const(be, 202));
  Bexpression *con1 = be->constructor_expression(bst, vals1, loc);
  be->implicit_variable_set_init(ims1, "x", bst,
                                 isHidden, isConst, isCommon, con1);

  const char *exp1 = R"RAW_RESULT(
     @v1 = weak_odr global { i32, i32 } { i32 101, i32 202 }, align 8
    )RAW_RESULT";

  bool isOK1 = h.expectValue(ims1->value(), exp1);
  EXPECT_TRUE(isOK1 && "Value does not have expected contents");

  // Case 2: common, no init value.
  isConst = true;
  isCommon = true;
  Bvariable *ims2 =
      be->implicit_variable("second", "v2", bst,
                            isHidden, isConst, isCommon, 8);
  be->implicit_variable_set_init(ims2, "x", bst,
                                 isHidden, isConst, isCommon, nullptr);

  const char *exp2 = R"RAW_RESULT(
    @v2 = weak_odr constant { i32, i32 } zeroinitializer, align 8
    )RAW_RESULT";

  bool isOK2 = h.expectValue(ims2->value(), exp2);
  EXPECT_TRUE(isOK2 && "Value does not have expected contents");

  // check that these don't crash
  be->implicit_variable_set_init(be->error_variable(), "x", bst,
                                 isHidden, isConst, isCommon, nullptr);
  be->implicit_variable_set_init(be->error_variable(), "x", bst,
                                 isHidden, isConst, isCommon,
                                 be->error_expression());

  bool broken = h.finish();
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendVarTests, GlobalVarSetInitToComposite) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));
  Location loc;

  Btype *bt = be->bool_type();
  Btype *pbt = be->pointer_type(bt);
  Btype *bi32t = be->integer_type(false, 32);
  Btype *s2t = mkBackendStruct(be.get(), pbt, "f1", bi32t, "f2", nullptr);
  Bvariable *g1 =
      be->global_variable("gv", "gv", s2t, false, /* is_external */
                          false,                  /* is_hidden */
                          false, /* unique_section */
                          Location());

  std::vector<Bexpression *> vals;
  vals.push_back(be->zero_expression(pbt));
  vals.push_back(mkInt32Const(be.get(), int32_t(101)));
  Bexpression *scon =
      be->constructor_expression(s2t, vals, loc);

  // Set initializer
  be->global_variable_set_init(g1, scon);
}

}
