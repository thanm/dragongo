//===-- go-llvm-bstatement.h - decls for gofrontend 'Bstatement' class ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines Bstatement and related classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVMGOFRONTEND_GO_LLVM_BSTATEMENT_H
#define LLVMGOFRONTEND_GO_LLVM_BSTATEMENT_H

// Currently these need to be included before backend.h
#include "go-linemap.h"
#include "go-location.h"
#include "go-llvm-btype.h"
//#include "go-llvm-bexpression.h"
#include "go-llvm-bnode.h"

#include "backend.h"

namespace llvm {
class Function;
class Instruction;
class Type;
class Value;
class raw_ostream;
}

// In the current gofrontend implementation, there is an extremely
// fluzzy/fluid line between Bexpression's, Bblocks, and Bstatements.
// For example, you can take a Bblock and turn it into a statement via
// Backend::block_statement. You can also combine an existing
// Bstatement with a Bexpression to form a second Bexpression via
// Backend::compound_expression. This is most likely due to the fact
// that the first backend (gcc) selected as a target for gofrontend
// uses a tree intermediate representation, where blocks, expressions,
// and statements are all simply tree fragments. With LLVM on the
// other hand, it is not so straightforward to have chunks of IR
// morphing back end forth between statements, expressions, and
// blocks.

class Bstatement : public Bnode {
 public:
  // no public constructor, use BnodeBuilder instead
  virtual ~Bstatement();

  // Function this statement is part of
  Bfunction *function() const { return function_; }

  // dump with source line info
  void srcDump(Linemap *);

  Bexpression *getExprStmtExpr();

  Bexpression *getIfStmtCondition();
  Bstatement *getIfStmtTrueBlock();
  Bstatement *getIfStmtFalseBlock();

  Bexpression *getSwitchStmtValue();
  unsigned     getSwitchStmtNumCases();
  std::vector<Bexpression *> getSwitchStmtNthCase(unsigned idx);
  Bstatement *getSwitchStmtNthStmt(unsigned idx);

  LabelId getGotoStmtTargetLabel();

  LabelId getLabelStmtDefinedLabel();

  // Returns any child statements for this statement.
  std::vector<Bstatement *> getChildStmts();

 protected:
  friend class BnodeBuilder;
  Bstatement(NodeFlavor fl, Bfunction *func,
             const std::vector<Bnode *> &kids, Location loc);

 private:
  Bexpression *getNthChildAsExpr(NodeFlavor fl, unsigned cidx);
  Bstatement *getNthChildAsStmt(NodeFlavor fl, unsigned cidx);

 private:
  Bfunction *function_;
};

typedef unsigned LabelId;

class Blabel {
public:
  Blabel(const Bfunction *function, LabelId lab, Location loc)
      : function_(const_cast<Bfunction *>(function)),
        lab_(lab), location_(loc) {}
  LabelId label() const { return lab_; }
  Bfunction *function() { return function_; }
  Location location() const { return location_; }

private:
  Bfunction *function_;
  LabelId lab_;
  Location location_;
};

// A Bblock is essentially just a compound statement

class Bblock : public Bstatement {
 public:
  // no public constructor; use BnodeBuilder::mkBlock to create a
  // block, BnodeBuilder::addStatementToBlock to add stmts to it.
  virtual ~Bblock() { }

  // Local variables for this block
  const std::vector<Bvariable *> &vars() const { return vars_; }

  // Exposed for unit testing the tree integrity checker. Not for general use.
  void clearStatements();

 private:
  friend class BnodeBuilder;
  Bblock(Bfunction *func,
         const std::vector<Bvariable *> &vars,
         Location loc);

 private:
  std::vector<Bvariable *> vars_;
};

#endif // LLVMGOFRONTEND_GO_LLVM_BSTATEMENT_H
