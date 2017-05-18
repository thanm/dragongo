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
#include "go-llvm-linemap.h"
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

// Class Bstatement encapsulates the IR for a statement, storing zero
// or more component Bexpressions, along with state related to
// statement control flow (if / switch / goto, etc). The specific
// contents/children of a given Bstatement are determined by its
// NodeFlavor (value of Bnode::flavor()).
//
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
  void srcDump(Llvm_linemap *);

  // If this is an expression statement (flavor N_ExprStmt), return
  // a pointer to the Bexpression it contains.
  Bexpression *getExprStmtExpr();

  // If this is a return statement (flavor N_ReturnStmt), return
  // a pointer to the Bexpression it contains.
  Bexpression *getReturnStmtExpr();

  // If this is an if statement (flavor N_IfStmt), return
  // components of the "if" (condition, true block, false block).
  Bexpression *getIfStmtCondition();
  Bstatement *getIfStmtTrueBlock();
  Bstatement *getIfStmtFalseBlock();

  // If this is a switch statement (flavor N_SwitchStmt), return
  // the Bexpresson for the value we're switching on, along
  // with info on the number of cases, Bexpressions for each case,
  // and Bstatement for each case.
  Bexpression *getSwitchStmtValue();
  unsigned     getSwitchStmtNumCases();
  std::vector<Bexpression *> getSwitchStmtNthCase(unsigned idx);
  Bstatement *getSwitchStmtNthStmt(unsigned idx);

  // If this is a defer statement (flavor N_DeferStmt), return
  // components of the defer.
  Bexpression *getDeferStmtUndeferCall();
  Bexpression *getDeferStmtDeferCall();

  // If this is an exception handling statement (flavor N_ExcepStmt), return
  // components of the statement.
  Bstatement *getExcepStmtBody();
  Bstatement *getExcepStmtOnException();
  Bstatement *getExcepStmtFinally();

  // If this is a goto statement (flavor N_GotoStmt), return
  // the target label for the goto.
  LabelId getGotoStmtTargetLabel();

  // If this is a label statement (flavor N_LabelStmt), return
  // the ID of the label defined by this stmt.
  LabelId getLabelStmtDefinedLabel();

  // Generic hook for collecting all statements that are children of
  // this statement. Used when walking statements to assign
  // instructions to LLVM blocks.
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

// Back end label class. Encapsulates a label referred to by a label
// definition statement and/or some set of goto statements.

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

  // Temporary vars can be tacked into an existing block via a
  // the backend temporary_variable() method-- allow for this here.
  void addTemporaryVariable(Bvariable *var);

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
