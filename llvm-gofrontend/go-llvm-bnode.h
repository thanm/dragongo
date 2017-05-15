//===-- go-llvm-bnode.h - decls for 'Bnode' class -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines Bnode class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVMGOFRONTEND_BNODE_H
#define LLVMGOFRONTEND_BNODE_H

// Currently these need to be included before backend.h
#include "go-llvm-linemap.h"
#include "go-location.h"
#include "go-llvm-btype.h"

#include "backend.h"

namespace llvm {
class Instruction;
class Value;
class raw_ostream;
}

// Opaque labelID handle for goto/label nodes.
typedef unsigned LabelId;

class Bexpression;
class Binstructions;
class SwitchDescriptor;
class IntegrityVisitor;
class Llvm_backend;

// Use when deleting a Bnode subtree. Controls whether to delete just
// the Bnode objects, just the LLVM instructions they contain, or both.
//
enum WhichDel {
  DelInstructions, // delete only instructions
  DelWrappers,     // delete only wrappers
  DelBoth          // delete wrappers and instructions
};

// Varieties of nodes. Order is important here, since enum
// values are used to index into tables/arrays.

enum NodeFlavor {
  N_Error=0,

  N_FirstExpr,
  N_Const=N_FirstExpr,
  N_Var,
  N_FcnAddress,
  N_Conversion,
  N_Deref,
  N_Address,
  N_UnaryOp,
  N_StructField,
  N_BinaryOp,
  N_Compound,
  N_ArrayIndex,
  N_Composite,
  N_Call,
  N_LastExpr = N_Call,

  N_FirstStmt,
  N_EmptyStmt=N_FirstStmt,
  N_LabelStmt,
  N_GotoStmt,
  N_ExprStmt,
  N_ReturnStmt,
  N_DeferStmt,
  N_IfStmt,
  N_ExcepStmt,
  N_BlockStmt,
  N_SwitchStmt,
  N_LastStmt=N_SwitchStmt
};

enum VisitDisp {
  ContinueWalk, StopWalk
};

enum VisitOrder {
  PreOrder, PostOrder
};

enum VisitWhich {
  VisitExprs, VisitStmts, VisitAll
};

// Class Bnode -- this class is not directly used or referred to in
// gofrontend, but it acts as an abstract base for the Bexpression and
// Bstatement classes. A given Bnode has zero or more Bnode children;
// the number and type (expr or stmt) children are determined by
// the Bnode flavor.

class Bnode {
 public:
  virtual ~Bnode() { }
  NodeFlavor flavor() const { return flavor_; }
  Location location() const { return location_; }
  unsigned id() const { return id_; }
  const char *flavstr() const;
  LabelId label() const;

  // debugging
  void dump();

  // dump with source line info
  void srcDump(Llvm_linemap *);

  // dump to raw_ostream
  void osdump(llvm::raw_ostream &os, unsigned ilevel = 0,
              Llvm_linemap *linemap = nullptr, bool terse = false);

  // Delete some or all or this Bnode and its component
  // pieces. Deallocates just the Bnode, its contained instructions,
  // or both (depending on setting of 'which'). This is used mainly in
  // unit testing.
  static void destroy(Bnode *node, WhichDel which = DelWrappers);

  // Cast to Bexpression. Returns NULL if not correct flavor.
  Bexpression *castToBexpression() const;

  // Cast to Bstatement. Returns NULL if not correct flavor.
  Bstatement *castToBstatement() const;

  // Cast to Bblock. Returns NULL if not correct flavor.
  Bblock *castToBblock() const;

  template<class Visitor> friend class SimpleNodeWalker;
  template<class Visitor> friend class UpdatingNodeWalker;
  friend class BnodeBuilder;
  friend class IntegrityVisitor;

  // TODO: hide this once GenBlocksVisitor is working
  const std::vector<Bnode *> &children() const { return kids_; }

 protected:
  Bnode(NodeFlavor flavor, const std::vector<Bnode *> &kids, Location loc);
  Bnode(const Bnode &src);
  SwitchDescriptor *getSwitchCases();

  // mainly for unit testing, not for general use.
  void removeAllChildren();

 private:
  void replaceChild(unsigned idx, Bnode *newchild);
  bool isStmt() const {
    return (flavor() >= N_FirstStmt && flavor() <= N_LastStmt);
  }

 private:
  std::vector<Bnode *> kids_;
  union {
    Bvariable *var;
    Bfunction *func; // filled in only for fcn constants
    SwitchDescriptor *swcases;
    LabelId label;
    Operator op;
    unsigned fieldIndex;
  } u;
  Location location_;
  NodeFlavor flavor_;
  unsigned id_;
  unsigned flags_;
};

// This helper class handles construction for all Bnode objects.
// Notes on storage allocation: ideally once an LLVM function has been
// constructed and sent off to the back end for a given Go function,
// we would want to delete all of the Bexpression's used by that
// function before moving on to the next function. Putting this into
// practice is tricky, however, since some Bexpressions (for example,
// var exprs and function addresses) wind up being held over and
// reused else where (for example, in emitted GC descriptors). For the
// moment we don't try to free Bexpressions at intermediate points
// in the compilation, but we do free Bstatements.

class BnodeBuilder {
 public:
  BnodeBuilder(Llvm_backend *be);
  ~BnodeBuilder();

  // Deletes all allocated Bstatements (also switch descriptors)
  void freeStmts();

  // expressions
  Bexpression *mkError(Btype *errortype);
  Bexpression *mkConst(Btype *btype, llvm::Value *val);
  Bexpression *mkVoidValue(Btype *voidType);
  Bexpression *mkVar(Bvariable *var, Location loc);
  Bexpression *mkConversion(Btype *btype, llvm::Value *val,
                            Bexpression *src, Location loc);
  Bexpression *mkDeref(Btype *typ, llvm::Value *val,
                       Bexpression *src, Location loc);
  Bexpression *mkAddress(Btype *typ, llvm::Value *val,
                         Bexpression *src, Location loc);
  Bexpression *mkFcnAddress(Btype *typ, llvm::Value *val,
                            Bfunction *func, Location loc);
  Bexpression *mkUnaryOp(Operator op, Btype *typ, llvm::Value *val,
                         Bexpression *src, Location loc);
  Bexpression *mkBinaryOp(Operator op, Btype *typ, llvm::Value *val,
                          Bexpression *left, Bexpression *right, Location loc);
  Bexpression *mkBinaryOp(Operator op, Btype *typ, llvm::Value *val,
                          Bexpression *left, Bexpression *right,
                          Binstructions &instructions, Location loc);
  Bexpression *mkCompound(Bstatement *st, Bexpression *expr,
                          Location loc);
  Bexpression *mkStructField(Btype *type, llvm::Value *value,
                             Bexpression *structval, unsigned fieldIndex,
                             Location loc);
  Bexpression *mkArrayIndex(Btype *typ,
                            llvm::Value *val,
                            Bexpression *arval,
                            Bexpression *index,
                            Location loc);
  Bexpression *mkComposite(Btype *btype, llvm::Value *value,
                           const std::vector<Bexpression *> &vals,
                           Binstructions &instructions,
                           Location loc);
  Bexpression *mkCall(Btype *btype, llvm::Value *value,
                      const std::vector<Bexpression *> &vals,
                      Binstructions &instructions,
                      Location loc);

  // statements
  Bstatement *mkErrorStmt();
  Bstatement *mkLabelDefStmt(Bfunction *func, Blabel *label, Location loc);
  Bstatement *mkGotoStmt(Bfunction *func, Blabel *label, Location loc);
  Bstatement *mkExprStmt(Bfunction *func, Bexpression *expr, Location loc);
  Bstatement *mkReturn(Bfunction *func, Bexpression *returnVal, Location loc);
  Bstatement *mkIfStmt(Bfunction *func,
                       Bexpression *cond, Bblock *trueBlock,
                       Bblock *thenBlock, Location loc);
  Bstatement *mkDeferStmt(Bfunction *func,
                          Bexpression *undefer,
                          Bexpression *defer,
                          Location loc);
  Bstatement *mkExcepStmt(Bfunction *func,
                          Bstatement *body,
                          Bstatement *onexception,
                          Bstatement *finally,
                          Location loc);
  Bstatement *mkSwitchStmt(Bfunction *func,
                           Bexpression *swvalue,
                           const std::vector<std::vector<Bexpression *> > &vals,
                           const std::vector<Bstatement *> &stmts,
                           Location loc);

  // block
  Bblock *mkBlock(Bfunction *func,
                  const std::vector<Bvariable *> &vars,
                  Location loc);
  void addStatementToBlock(Bblock *block, Bstatement *st);

  // Free up this expr (it is garbage). Does not free up children.
  void freeExpr(Bexpression *expr);

  // Clone an expression subtree.
  Bexpression *cloneSubtree(Bexpression *expr);

  // Inform the builder that we're about to extract all of the
  // children of the specified node and incorporate them into a new
  // node (after which the old node will be thrown away). Returns
  std::vector<Bexpression *> extractChildenAndDestroy(Bexpression *expr);

 private:
  void appendInstIfNeeded(Bexpression *rval, llvm::Value *val);
  Bexpression *archive(Bexpression *expr);
  Bstatement *archive(Bstatement *stmt);
  Bblock *archive(Bblock *bb);
  Bexpression *cloneSub(Bexpression *expr,
                        std::map<llvm::Value *, llvm::Value *> &vm);
  void checkTreeInteg(Bnode *node);

 private:
  std::unique_ptr<Bstatement> errorStatement_;
  std::vector<Bexpression *> earchive_;
  std::vector<Bstatement *> sarchive_;
  std::vector<SwitchDescriptor*> swcases_;
  std::unique_ptr<IntegrityVisitor> integrityVisitor_;
};

// This class helps automate walking of a Bnode subtree; it invokes
// callbacks in the supplied visitor object at useful points during
// the walk that is instigated by 'simple_walk_nodes' below.

template<class Visitor>
class SimpleNodeWalker {
public:
  SimpleNodeWalker(Visitor &vis) : visitor_(vis) { }

  void walk(Bnode *node) {
    assert(node);

    // pre-node hook
    visitor_.visitNodePre(node);

    // walk children
    for (unsigned idx = 0; idx < node->children().size(); ++idx) {
      Bnode *child = node->children()[idx];
      walk(child);
    }

    // post-node hook
    visitor_.visitNodePost(node);
  }

 private:
  Visitor &visitor_;
};

template<class Visitor>
inline void simple_walk_nodes(Bnode *root, Visitor &vis) {
  SimpleNodeWalker<Visitor> walker(vis);
  walker.walk(root);
}

// A more complicated node walker that allows for replacement of child
// nodes, plus stopping the walk if the visitor so decides.

template<class Visitor>
class UpdatingNodeWalker {
public:
  UpdatingNodeWalker(Visitor &vis) : visitor_(vis) { }

  std::pair<VisitDisp, Bnode *> walk(Bnode *node) {
    assert(node);

    // pre-node hook
    auto pairPre = visitor_.visitNodePre(node);
    if (pairPre.second != node)
      node = pairPre.second;
    if (pairPre.first == StopWalk)
      return std::make_pair(StopWalk, node);

    std::vector<Bnode *> children = node->children();
    for (unsigned idx = 0; idx < children.size(); ++idx) {
      Bnode *child = children[idx];

      // pre-child hook
      auto pairPre = visitor_.visitChildPre(node, child);
      if (pairPre.second != child) {
        node->replaceChild(idx, pairPre.second);
        child = pairPre.second;
      }
      if (pairPre.first == StopWalk)
        return std::make_pair(StopWalk, node);

      // walk child
      auto pairChild = walk(child);
      if (pairChild.second != child) {
        node->replaceChild(idx, pairChild.second);
        child = pairChild.second;
      }
      if (pairChild.first == StopWalk)
        return std::make_pair(StopWalk, node);

      // post-child hook
      auto pairPost = visitor_.visitChildPost(node, child);
      if (pairPost.second != child)
        node->replaceChild(idx, pairPost.second);
      if (pairPost.first == StopWalk)
        return std::make_pair(StopWalk, node);
    }

    // post-node hook
    auto pairPost = visitor_.visitNodePost(node);
    if (pairPost.second != node)
      node = pairPost.second;
    if (pairPost.first == StopWalk)
      return std::make_pair(StopWalk, node);

    return std::make_pair(ContinueWalk, node);
  }

 private:
  Visitor &visitor_;
};

template<class Visitor>
inline Bnode *update_walk_nodes(Bnode *root, Visitor &vis) {
  UpdatingNodeWalker<Visitor> walker(vis);
  auto p = walker.walk(root);
  return p.second;
}

// Helper contained class for recording the child indices of switch
// cases and switch statements within the containing Bnode.  Here 'st'
// is the child index of the first case value, 'len' is the number of
// matching values for the case, and 'stmt' is the child index of the
// corresponding statement. Example:
//
// switch q {             Switch Bnode children:
//   case 2, 3, 4:          0: expr q
//     return x + y         1: expr 2
//   default:               2: expr 3
//     return x / y         3: expr 4
//   case 5:                4: expr 5
//     return 9             5: stmt return x + y
//  }                       6: stmt return x / y
//                          7: stmt return 9
//  Case descriptors:
//     1, 3, 5
//     4, 0, 6
//     5, 1, 7

struct SwitchCaseDesc {
  unsigned st, len, stmt;
  SwitchCaseDesc(unsigned s, unsigned l, unsigned st)
      : st(s), len(l), stmt(st) { }
};

// Stores child index info about a switch.

class SwitchDescriptor {
 public:
  SwitchDescriptor(const std::vector<std::vector<Bexpression *> > &vals);
  const std::vector<SwitchCaseDesc> &cases() const { return cases_; }

 private:
  std::vector<SwitchCaseDesc> cases_;
};

#endif // LLVMGOFRONTEND_GO_BNODE_H
