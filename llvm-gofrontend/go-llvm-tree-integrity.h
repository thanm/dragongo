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
class BnodeBuilder;

enum CkTreePtrDisp { DumpPointers, NoDumpPointers };
enum CkTreeVarDisp { CheckVarExprs, IgnoreVarExprs };
enum CkTreeRepairDisp { RepairSharing, DontRepairSharing };

// Options/controls for the tree integrity checker.

struct TreeIntegCtl {
  CkTreePtrDisp ptrDisp;
  CkTreeVarDisp varDisp;
  CkTreeRepairDisp repairDisp;
  TreeIntegCtl()
      : ptrDisp(DumpPointers),
        varDisp(CheckVarExprs),
        repairDisp(DontRepairSharing) { }
  TreeIntegCtl(CkTreePtrDisp ptrs, CkTreeVarDisp vars, CkTreeRepairDisp rep)
      : ptrDisp(ptrs), varDisp(vars), repairDisp(rep) { }
};

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
// constants.
//
// In addition, we provide a mode of the checker ("IgnoreVarExprs"
// above) in which we don't try to enforce sharing for var exprs,
// since gofrontend tends to reuse them in a number of places.  The
// one place where this can cause issues is with nested functions and
// closures. Example:
//
//     func simple(x, q int) int {
//      pf := func(xx int) int {
//              qm2 := q - 2
//              qm1 := q - 1
//              return xx + qm2 + qm1
//      }
//      return pf(x)
//     }
//
// Note the references to "q" within the nested function -- in a
// non-nested function these woule be simple var expressions, however
// in the case above they will appear to the backend as loads of
// fields from the closure pointer passed via the static chain. To
// support this case there is yet another option/control that tells
// the checker to "unshare" the nodes in question (clone them to
// restore tree integrity). This unsharing/repairing is applied only
// to a whitelisted set of expression nodes (for example, cloning of
// call expressions is not allowed).

class IntegrityVisitor {
 public:
  IntegrityVisitor(Llvm_backend *be,
                   TreeIntegCtl control)
      : be_(be), ss_(str_), control_(control),
        instShareCount_(0), stmtShareCount_(0), exprShareCount_(0) { }

  bool examine(Bnode *n);
  std::string msg() { return ss_.str(); }

 private:
  Llvm_backend *be_;
  typedef std::pair<Bnode *, unsigned> parslot; // parent and child index
  std::unordered_map<llvm::Instruction *, parslot> iparent_;
  std::unordered_map<Bnode *, parslot> nparent_;
  std::vector<std::pair<Bnode *, parslot> > sharing_; // child -> parent+slot
  std::string str_;
  llvm::raw_string_ostream ss_;
  TreeIntegCtl control_;
  unsigned instShareCount_;
  unsigned stmtShareCount_;
  unsigned exprShareCount_;

 private:
  bool repair(Bnode *n);
  void visit(Bnode *n);
  bool repairableSubTree(Bexpression *root);
  bool shouldBeTracked(Bnode *child);
  void unsetParent(Bnode *child, Bnode *parent, unsigned slot);
  void setParent(Bnode *child, Bnode *parent, unsigned slot);
  void setParent(llvm::Instruction *inst, Bexpression *par, unsigned slot);
  void dumpTag(const char *tag, void *ptr);
  void dump(llvm::Instruction *inst);
  void dump(Bnode *node);
  CkTreePtrDisp includePointers() const { return control_.ptrDisp; }
  CkTreeVarDisp includeVarExprs() const { return control_.varDisp; }
  CkTreeRepairDisp doRepairs() const { return control_.repairDisp; }

  friend BnodeBuilder;
};

#endif // LLVMGOFRONTEND_GO_LLVM_TREE_INTEGRITY_H
