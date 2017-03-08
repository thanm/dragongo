//===-- go-llvm-cabi-irbuilders.h - IR builder helper classes -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Assorted helper classes for IR building.
//
//===----------------------------------------------------------------------===//

#ifndef LLVMGOFRONTEND_GO_LLVM_IRBUILDER_H
#define LLVMGOFRONTEND_GO_LLVM_IRBUILDER_H

#include "go-llvm-bexpression.h"

#include "llvm/IR/IRBuilder.h"

// Generic "no insert" builder
typedef llvm::IRBuilder<> LIRBuilder;

class BexprInserter {
 public:
  BexprInserter() : expr_(nullptr) { }
  void setDest(Bexpression *expr) { assert(!expr_); expr_ = expr; }

  void InsertHelper(llvm::Instruction *I, const llvm::Twine &Name,
                    llvm::BasicBlock *BB,
                    llvm::BasicBlock::iterator InsertPt) const {
    assert(expr_);
    expr_->appendInstruction(I);
    I->setName(Name);
  }

 private:
    mutable Bexpression *expr_;
};

// Builder that appends to a specified Bexpression

class BexprLIRBuilder :
    public llvm::IRBuilder<llvm::ConstantFolder, BexprInserter> {
  typedef llvm::IRBuilder<llvm::ConstantFolder, BexprInserter> IRBuilderBase;
 public:
  BexprLIRBuilder(llvm::LLVMContext &context, Bexpression *expr) :
      IRBuilderBase(context, llvm::ConstantFolder()) {
    setDest(expr);
  }
};

class BinstructionsInserter {
 public:
  BinstructionsInserter() : insns_(nullptr) { }
  void setDest(Binstructions *insns) { assert(!insns_); insns_ = insns; }

  void InsertHelper(llvm::Instruction *I, const llvm::Twine &Name,
                    llvm::BasicBlock *BB,
                    llvm::BasicBlock::iterator InsertPt) const {
    assert(insns_);
    insns_->appendInstruction(I);
    I->setName(Name);
  }

 private:
    mutable Binstructions *insns_;
};

// Builder that appends to a specified Binstructions object

class BinstructionsLIRBuilder :
    public llvm::IRBuilder<llvm::ConstantFolder, BinstructionsInserter> {
  typedef llvm::IRBuilder<llvm::ConstantFolder,
                          BinstructionsInserter> IRBuilderBase;
 public:
  BinstructionsLIRBuilder(llvm::LLVMContext &context, Binstructions *insns) :
      IRBuilderBase(context, llvm::ConstantFolder()) {
    setDest(insns);
  }
};

// Some of the methods in the LLVM IRBuilder class (ex: CreateMemCpy) assume that
// you are appending to an existing basic block (which is typically
// not what we want to do in many cases in the bridge code).
//
// This builder works around this issue by creating a dummy basic block
// to capture any instructions generated, then when the builder is
// destroyed it detaches the instructions from the block so that
// they can be returned in a list.

class BlockLIRBuilder : public LIRBuilder {
 public:
  BlockLIRBuilder(llvm::Function *func)
      : LIRBuilder(func->getContext(), llvm::ConstantFolder()),
        dummyBlock_(llvm::BasicBlock::Create(func->getContext(), "", func))
  {
    SetInsertPoint(dummyBlock_.get());
  }

  ~BlockLIRBuilder() {
    assert(dummyBlock_->getInstList().empty());
    dummyBlock_->removeFromParent();
  }

  std::vector<llvm::Instruction*> instructions() {
    std::vector<llvm::Instruction*> rv;
    for (auto &i : dummyBlock_->getInstList())
      rv.push_back(&i);
    for (auto &i : rv)
      i->removeFromParent();
    return rv;
  }

 private:
  std::unique_ptr<llvm::BasicBlock> dummyBlock_;
};

#endif // LLVMGOFRONTEND_GO_LLVM_IRBUILDER_H
