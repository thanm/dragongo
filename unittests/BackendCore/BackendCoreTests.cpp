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

TEST(BackendCoreTests, CoreTypes) {
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

}
