//===-- bnode.h - decls for 'Bnode' class --==================================//
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
#include "go-linemap.h"
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

// Varieties of nodes. Order is important here, since enum
// values are used to index into tables/arrays.

enum NodeFlavor {
  N_Error=0,
  N_Const,
  N_Var,
  N_Conversion,
  N_Deref,
  N_Address,
  N_UnaryOp,
  N_StructField,
  N_BinaryOp,
  N_Compound,
  N_ArrayIndex,
  N_Composite,
  N_EmptyStmt,
  N_LabelStmt,
  N_GotoStmt,
  N_InitStmt,
  N_AssignStmt,
  N_IfStmt,
  N_BlockStmt,
  N_ReturnStmt,
  N_SwitchStmt
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

class Bnode {
 public:
  NodeFlavor flavor() const { return flavor_; }
  Btype *btype() const { return btype_; }
  Location location() const { return location_; }
  unsigned id() const { return id_; }
  const char *flavstr() const;

  // debugging
  void dump();

  // dump with source line info
  void srcDump(Linemap *);

  // dump to raw_ostream
  void osdump(llvm::raw_ostream &os, unsigned ilevel = 0,
              Linemap *linemap = nullptr, bool terse = false);

  template<class Visitor>
  friend class NodeWalker;
  friend class BnodeBuilder;

 protected:
  Bnode(NodeFlavor flavor, const std::vector<Bnode *> &kids,
        Btype *type, Location loc, unsigned id);

 private:
  const std::vector<Bnode *> &children() const { return kids_; }
  void replaceChild(unsigned idx, Bnode *newchild);

  enum Flags {
    STMT=(1<<0)
  };

  bool isStmt() const { return flags_ & STMT; }

 private:
  std::vector<Bnode *> kids_;
  Btype *btype_;
  llvm::Value *value_;
  union {
    Bvariable *var;
    LabelId label;
    Operator op;
    unsigned fieldIndex;
  } u;
  Location location_;
  NodeFlavor flavor_;
  unsigned id_;
  unsigned flags_;
};

// Bnode builder class
class BnodeBuilder {
 public:
  BnodeBuilder() : idCounter_(1) { }

  Bexpression *mkError();
  Bexpression *mkConst(Btype *btype, llvm::Value *val);
  Bexpression *mkVar(Bvariable *var);
  Bexpression *mkConversion(Btype *btype, Bexpression *src,
                            Location loc);
  Bexpression *mkDeref(Bexpression *src, Location loc);
  Bexpression *mkAddress(Bexpression *src, Location loc);
  Bexpression *mkUnaryOp(Operator op, Bexpression *src, Location loc);
  Bexpression *mkBinaryOp(Operator op, Bexpression *left,
                          Bexpression *right, Location loc);
  Bexpression *mkCompound(Bstatement *st, Bexpression *expr,
                          Location loc);
  Bexpression *mkStructField(Bexpression *structval, unsigned fieldIndex,
                             Location loc);
  Bexpression *mkArrayIndex(Bexpression *structval, unsigned fieldIndex,
                            Location loc);
  Bexpression *mkComposite(Btype *btype,
                           const std::vector<Bexpression *> &vals,
                           Location loc);
  Bstatement *mkLabelDefStmt(Blabel *label, Location loc);
  Bstatement *mkGotoStmt(Blabel *label, Location loc);
  Bstatement *mkInitStmt(Bvariable *var, Bexpression *expr,
                         Location loc);
  Bstatement *mkAssignStmt(Bexpression *lhs, Bexpression *rhs,
                           Location loc);
  Bstatement *mkIfStmt(Bexpression *cond, Bblock *trueBlock,
                       Bblock *thenBlock, Location loc);
  Bstatement *mkBlockStmt(const std::vector<Bstatement *> &kids,
                          Location loc);
  Bstatement *mkReturnStmt(const std::vector<Bexpression *> &rvals,
                           Location loc);
  Bstatement *mkSwitchStmt(const std::vector<Bexpression *> &rvals,
                           Location loc);
 private:
  unsigned idCounter_;
};

// strictly temporary, these will go away once bexpr is a typedef
class Bexpression : public Bnode {
 public:
  Bexpression(NodeFlavor flavor, const std::vector<Bnode *> &kids,
              Btype *type, Location loc, unsigned id)
      : Bnode(flavor, kids, type, loc, id) { }
};

class Bstatement : public Bnode { };
class Bblock : public Bnode { };

template<class Visitor>
class NodeWalker {
public:
  NodeWalker(Visitor &vis) : visitor_(vis) { }

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
inline Bnode *walk_nodes(Bnode *root, Visitor &vis) {
  NodeWalker<Visitor> walker(vis);
  auto p = walker.walk(root);
  return p.second;
}

#endif // LLVMGOFRONTEND_GO_BNODE_H
