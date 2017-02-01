//===-- go-llvm-genblocks.cpp - definition of GenBlock classes ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of GenBlockVisitor class and related helper routines.
//
//===----------------------------------------------------------------------===//

#include "go-llvm-genblocks.h"

llvm::BasicBlock *genblocks(llvm::LLVMContext &context,
                            Llvm_backend *be,
                            Bfunction *func,
                            Bstatement *code_stmt)
{
  GenBlocksVisitor gb(context, be, func);
  simple_walk_nodes(code_stmt, gb);
  //return gb.getBlock(code_stmt);
  return nullptr;
}
