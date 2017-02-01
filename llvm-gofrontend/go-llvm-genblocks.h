//===-- go-llvm-genblocks.h - decls for GenBlocks class --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines GenBlockVisitor class and related helper routines.
//
//===----------------------------------------------------------------------===//

#ifndef LLVMGOFRONTEND_GO_LLVM_GENBLOCKS_H
#define LLVMGOFRONTEND_GO_LLVM_GENBLOCKS_H

#include <string>
#include <vector>

#include "go-llvm-bstatement.h"
#include "namegen.h"

namespace llvm {
class Instruction;
class BasicBlock;
class LLVMContext;
}

class Bexpression;
class Bfunction;
class Llvm_backend;

//
// Helper class for assigning instructions to LLVM basic blocks
// and materializing control transfers.
//
class GenBlocksVisitor {
public:
  GenBlocksVisitor(llvm::LLVMContext &context,
                   Llvm_backend *be, Bfunction *function)
      : context_(context), be_(be), function_(function),
        emitOrphanedCode_(false) {}

  Bfunction *function() { return function_; }

  void visitNodePre(Bnode *node) { } // to be filled in
  void visitNodePost(Bnode *node) { } // to be filled in

 private:
  llvm::BasicBlock *mkLLVMBlock(const std::string &name,
                                unsigned expl = NameGen::ChooseVer);
  llvm::BasicBlock *getBlockForLabel(LabelId lab);

 private:
  llvm::LLVMContext &context_;
  Llvm_backend *be_;
  Bfunction *function_;
  std::map<LabelId, llvm::BasicBlock *> labelmap_;
  std::vector<llvm::BasicBlock *> blockstack_;
  bool emitOrphanedCode_;
};

// This entry point walks the specified Bnode tree and assigns
// its containing LLVM instructions to blocks.

extern llvm::BasicBlock *genblocks(llvm::LLVMContext &context,
                                   Llvm_backend *be,
                                   Bfunction *func,
                                   Bstatement *code_stmt);

#endif // LLVMGOFRONTEND_GO_LLVM_GENBLOCKS_H
