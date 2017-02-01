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

#include "llvm/Support/raw_ostream.h"

namespace llvm {
class Instruction;
}

class Bstatement;
class Bexpression;
class Llvm_backend;

enum CkTreePtrDisp { DumpPointers, NoDumpPointers };
enum CkTreeVarDisp { CheckVarExprs, IgnoreVarExprs };

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
//
// It is worth noting that some sharing is allowed in Bexpression trees,
// specifically sharing of Bexpressions corresponding to module-scope
// constants. In addition, we don't try to enforce sharing for var exprs,
// since gofrontend tends to reuse them in a number of places.
//

class IntegrityVisitor {
 public:
  IntegrityVisitor(const Llvm_backend *be,
                   CkTreePtrDisp ptrDisp,
                   CkTreeVarDisp varDisp)
      : be_(be), ss_(str_), includePointers_(ptrDisp),
        includeVarExprs_(varDisp) { }

  bool visit(Bnode *n);
  std::string msg() { return ss_.str(); }

 private:
  const Llvm_backend *be_;
  std::unordered_map<llvm::Instruction *, Bnode *> iparent_;
  std::unordered_map<Bnode *, Bnode *> nparent_;
  std::string str_;
  llvm::raw_string_ostream ss_;
  CkTreePtrDisp includePointers_;
  CkTreeVarDisp includeVarExprs_;

 private:
  bool setParent(Bnode *child, Bnode *parent);
  bool setParent(llvm::Instruction *inst, Bexpression *exprParent);
  void dumpTag(const char *tag, void *ptr);
  void dump(llvm::Instruction *inst);
  void dump(Bnode *node);
};

#endif // LLVMGOFRONTEND_GO_LLVM_TREE_INTEGRITY_H
