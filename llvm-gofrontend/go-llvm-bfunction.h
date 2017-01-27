//===-- go-llvm-bfunction.h - decls for gofrontend 'Bfunction' class ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines Bfunction and related classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVMGOFRONTEND_GO_LLVM_BFUNCTION_H
#define LLVMGOFRONTEND_GO_LLVM_BFUNCTION_H

// Currently these need to be included before backend.h
#include "go-linemap.h"
#include "go-location.h"
#include "go-llvm-btype.h"
#include "go-llvm-bexpression.h"

#include "namegen.h"
#include "backend.h"

namespace llvm {
class Argument;
class BasicBlock;
class Function;
class Instruction;
class Value;
class raw_ostream;
}

// Class Bfunction wraps llvm::Function

class Bfunction : public NameGen {
public:
  Bfunction(llvm::Function *f, BFunctionType *fcnType,
            const std::string &asmName);
  ~Bfunction();

  llvm::Function *function() const { return function_; }
  BFunctionType *fcnType() const { return fcnType_; }
  const std::string &asmName() const { return asmName_; }

  enum SplitStackDisposition { YesSplit, NoSplit };
  void setSplitStack(SplitStackDisposition disp) { splitStack_ = disp; }
  SplitStackDisposition splitStack() const { return splitStack_; }

  // Add a local variable
  Bvariable *local_variable(const std::string &name,
                            Btype *btype,
                            bool is_address_taken,
                            Location location);

  // Add a parameter variable
  Bvariable *parameter_variable(const std::string &name,
                                Btype *btype,
                                bool is_address_taken,
                                Location location);

  // Record a new Bblock for this function.
  void addBlock(Bblock *block) { blocks_.push_back(block); }

  // Create and return a new block
  Bblock *newBlock(Bfunction *function);

  // Create a new label
  Blabel *newLabel();

  // Create a new label definition statement
  Bstatement *newLabelDefStatement(Blabel *label);

  // Create a new goto statement
  Bstatement *newGotoStatement(Blabel *label, Location location);

  // Create code to spill function arguments to entry block, insert
  // allocas for local variables.
  void genProlog(llvm::BasicBlock *entry);

  // Map back from an LLVM value (argument, alloca) to the Bvariable
  // we created to wrap it. Exposed for unit testing.
  Bvariable *getBvarForValue(llvm::Value *val);

  // Return Nth argument as llvm value. Exposed for unit testing.
  llvm::Value *getNthArgValue(unsigned argIdx);

private:

  // Record an alloca() instruction, to be added to entry block
  void addAlloca(llvm::Instruction *inst) { allocas_.push_back(inst); }

  // Return Nth argument
  llvm::Argument *getNthArg(unsigned argIdx);

  // Return alloca inst holding argument value (create if needed)
  llvm::Instruction *argValue(llvm::Argument *arg);

  // Number of parameter vars registered so far
  unsigned paramsCreated() { return argToVal_.size(); }

 private:
  std::vector<llvm::Instruction *> allocas_;
  std::vector<llvm::Argument *> arguments_;
  std::vector<Bblock *> blocks_;
  std::unordered_map<llvm::Value *, Bvariable *> valueVarMap_;
  std::unordered_map<llvm::Argument *, llvm::Instruction *> argToVal_;
  std::vector<Bstatement *> labelmap_;
  std::vector<Blabel *> labels_;
  llvm::Function *function_;
  BFunctionType *fcnType_;
  std::string asmName_;
  unsigned labelCount_;
  SplitStackDisposition splitStack_;
};

#endif // LLVMGOFRONTEND_GO_LLVM_BFUNCTION_H
