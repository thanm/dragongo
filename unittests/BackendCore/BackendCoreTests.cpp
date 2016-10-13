//===- llvm/tools/dragongo/unittests/BackendCore/BackendCoreTests.cpp -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "gtest/gtest.h"

#include "go-llvm-backend.h"

// Currently these need to be included before backend.h
#include "go-location.h"
#include "go-linemap.h"

#include "backend.h"
#include "go-llvm.h"

using namespace llvm;

namespace {

TEST(BackendCoreTests, MakeBackend) {
  LLVMContext C;

  std::unique_ptr<Backend> makeit(go_get_backend(C));

}

TEST(BackendCoreTests, ScalarTypes) {
  LLVMContext C;

  std::unique_ptr<Backend> backend(go_get_backend(C));

  Btype *et = backend->error_type();
  ASSERT_TRUE(et != NULL);
  Btype *vt = backend->void_type();
  ASSERT_TRUE(vt != NULL);
  ASSERT_TRUE(vt != et);
  Btype *bt = backend->bool_type();
  ASSERT_TRUE(bt != NULL);

  std::vector<bool> isuns = {false, true};
  std::vector<int> ibits = {8, 16, 32, 64, 128};
  for (auto uns : isuns) {
    for (auto nbits : ibits) {
      Btype *it = backend->integer_type(uns, nbits);
      ASSERT_TRUE(it != NULL);
      ASSERT_TRUE(it->type()->isIntegerTy());
    }
  }

  std::vector<int> fbits = {32, 64, 128};
  for (auto nbits : fbits) {
    Btype *ft = backend->float_type(nbits);
    ASSERT_TRUE(ft != NULL);
    ASSERT_TRUE(ft->type()->isFloatingPointTy());
  }
}

//
// Create this struct using backend interfaces:
//
//    struct {
//       bool f1;
//       float *f2;
//       uint64_t f3;
//    }

static Btype *mkBackendThreeFieldStruct(Backend *be)
{
  Btype *pfloat = be->pointer_type(be->float_type(32));
  Btype *u64 = be->integer_type(true, 64);
  std::vector<Backend::Btyped_identifier> fields = {
    Backend::Btyped_identifier("f1", be->bool_type(), Location()),
    Backend::Btyped_identifier("f2", pfloat, Location()),
    Backend::Btyped_identifier("f3", u64, Location())
  };
  return be->struct_type(fields);
}

//
// Create this struct using LLVM interfaces:
//
//    struct {
//       bool f1;
//       float *f2;
//       uint64_t f3;
//    }

static StructType *mkLlvmThreeFieldStruct(LLVMContext &context)
{
  SmallVector<Type *, 3> smv(3);
  smv[0] = Type::getInt1Ty(context);
  smv[1] = PointerType::get(Type::getFloatTy(context), 0);
  smv[2] = IntegerType::get(context, 64);
  return StructType::get(context, smv);
}

TEST(BackendCoreTests, StructTypes) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  // Empty struct
  std::vector<Backend::Btyped_identifier> nofields;
  Btype *emptyst = be->struct_type(nofields);
  SmallVector<Type *, 3> smv_empty(0);
  Type *llvm_emptyst = StructType::get(C, smv_empty);
  ASSERT_TRUE(llvm_emptyst != NULL);
  ASSERT_TRUE(emptyst != NULL);
  ASSERT_EQ(llvm_emptyst, emptyst->type());

  // Three-field struct
  Btype *best = mkBackendThreeFieldStruct(be.get());
  Type *llst = mkLlvmThreeFieldStruct(C);
  ASSERT_TRUE(best != NULL);
  ASSERT_TRUE(llst != NULL);
  ASSERT_EQ(llst, best->type());
}

static Type *mkTwoFieldLLvmStruct(LLVMContext &context, Type *t1, Type *t2) {
  SmallVector<Type *, 2> smv(2);
  smv[0] = t1;
  smv[1] = t2;
  return StructType::get(context, smv);
}

TEST(BackendCoreTests, ComplexTypes) {
  LLVMContext C;

  Type *ft = Type::getFloatTy(C);
  Type *dt = Type::getDoubleTy(C);

  std::unique_ptr<Backend> be(go_get_backend(C));
  Btype *c32 = be->complex_type(64);
  ASSERT_TRUE(c32 != NULL);
  ASSERT_EQ(c32->type(), mkTwoFieldLLvmStruct(C, ft, ft));
  Btype *c64 = be->complex_type(128);
  ASSERT_TRUE(c64 != NULL);
  ASSERT_EQ(c64->type(), mkTwoFieldLLvmStruct(C, dt, dt));
}

}
