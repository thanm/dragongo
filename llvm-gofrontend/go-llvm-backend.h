//===-- go-llvm-backend.h - Backend class public interfaces  --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Public interfaces for LLVM implementation of Backend class.
//
//===----------------------------------------------------------------------===//

#ifndef GO_LLVM_BACKEND_H
#define GO_LLVM_BACKEND_H

#include "go-llvm.h"

#include "llvm/IR/LLVMContext.h"

class Backend;

extern Backend *go_get_backend(llvm::LLVMContext &Context);

#endif // !defined(GO_LLVM_BACKEND_H)
