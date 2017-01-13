//===-- go-llvm-tree-integrity.h - decls for tree integrity utils ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines IntegrityVisitor class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVMGOFRONTEND_GO_LLVM_TREE_INTEGRITY_H
#define LLVMGOFRONTEND_GO_LLVM_TREE_INTEGRITY_H

#if 0
// Currently these need to be included before backend.h
#include "go-linemap.h"
#include "go-location.h"
#include "go-llvm-btype.h"

#include "backend.h"

#endif

#include "llvm/Support/raw_ostream.h"

namespace llvm {
class Instruction;
}

class Bstatement;
class Bexpression;
class Llvm_backend;

// This visitor class detects malformed IR trees, specifically cases
// where the same Bexpression or Bstatement is pointed to by more than
// containing expr/stmt. For example suppose we have a couple of assignment
// statements
//
//      x = y
//      z = y
//
// where the Bexpression corresponding to "y" is pointed to both by
// the first assignment stmt and by the second assignment stmt.

class IntegrityVisitor {
 public:
  IntegrityVisitor(const Llvm_backend *be, bool incPtrs)
      : be_(be), ss_(str_), includePointers_(incPtrs) { }

  bool visit(Bexpression *e);
  bool visit(Bstatement *e);
  std::string msg() { return ss_.str(); }

 private:
  const Llvm_backend *be_;
  std::unordered_map<llvm::Instruction *, Bexpression *> iparent_;
  typedef std::pair<Bexpression *, Bstatement *> eors;
  std::unordered_map<Bstatement *, eors> sparent_;
  std::unordered_map<Bexpression *, eors> eparent_;
  std::string str_;
  llvm::raw_string_ostream ss_;
  bool includePointers_;

 private:
  bool setParent(Bexpression *child, const eors &parent);
  bool setParent(Bstatement *child, const eors &parent);
  bool setParent(llvm::Instruction *inst, Bexpression *exprParent);
  eors makeExprParent(Bexpression *expr);
  eors makeStmtParent(Bstatement *stmt);
  void dumpTag(const char *tag, void *ptr);
  void dump(Bexpression *expr);
  void dump(Bstatement *stmt);
  void dump(llvm::Instruction *inst);
  void dump(const eors &pair);
};

#endif // LLVMGOFRONTEND_GO_LLVM_TREE_INTEGRITY_H
