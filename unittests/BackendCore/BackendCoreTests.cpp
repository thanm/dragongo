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

using namespace llvm;

namespace {

TEST(BackendCoreTests, MakeABackend) {
  LLVMContext C;

  std::unique_ptr<Backend> makeit(go_get_backend(C));

}

}
