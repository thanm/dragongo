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
#include "go-llvm-bexpression.h"

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
//
// What this means from a practical point of view is that we delay
// creation of control flow (creating llvm::BasicBlock objects and
// assigning instructions to blocks) until the front end is
// essentially done with creating all statements and expressions.
// Prior to this point when the front end creates a statement that
// creates control flow (for example an "if" statement), we create
// placeholders to record what has to be done, then come along later
// and stitch things together at the end.

class CompoundStatement;
class ExprListStatement;
class IfPHStatement;
class SwitchPHStatement;
class GotoStatement;
class LabelStatement;

// Abstract base statement class, just used to distinguish between
// the various derived statement types.

class Bstatement {
public:
  enum StFlavor {
    ST_Compound,
    ST_ExprList,
    ST_IfPlaceholder,
    ST_SwitchPlaceholder,
    ST_Goto,
    ST_Label
  };

  Bstatement(Bfunction *function, StFlavor flavor, Location location)
      : function_(function), flavor_(flavor), location_(location) {}
  virtual ~Bstatement() {}
  StFlavor flavor() const { return flavor_; }
  Location location() const { return location_; }
  Bfunction *function() const { return function_; }

  inline CompoundStatement *castToCompoundStatement();
  inline ExprListStatement *castToExprListStatement();
  inline IfPHStatement *castToIfPHStatement();
  inline SwitchPHStatement *castToSwitchPHStatement();
  inline GotoStatement *castToGotoStatement();
  inline LabelStatement *castToLabelStatement();

  // Perform deletions on the tree of Bstatements rooted at stmt.
  // Delete Bstatements/Bexpressions, instructions, or both (depending
  // on setting of 'which')
  static void destroy(Bstatement *subtree, WhichDel which = DelWrappers);

  // debugging
  void dump();

  // dump to raw_ostream
  void osdump(llvm::raw_ostream &os, unsigned ilevel, bool terse = false);

 private:
  Bfunction *function_;
  StFlavor flavor_;
  Location location_;
};

// Compound statement is simply a list of other statements.

class CompoundStatement : public Bstatement {
public:
  explicit CompoundStatement(Bfunction *function)
      : Bstatement(function, ST_Compound, Location()) {}
  std::vector<Bstatement *> &stlist() { return stlist_; }

private:
  std::vector<Bstatement *> stlist_;
};

inline CompoundStatement *Bstatement::castToCompoundStatement() {
  return (flavor_ == ST_Compound ? static_cast<CompoundStatement *>(this)
                                 : nullptr);
}

// ExprList statement contains a list of LLVM instructions.

class ExprListStatement : public Bstatement {
public:
  explicit ExprListStatement(Bfunction *function)
      : Bstatement(function, ST_ExprList, Location()) {}
  ExprListStatement(Bfunction *function, Bexpression *e)
      : Bstatement(function, ST_ExprList, Location()) {
    expressions_.push_back(e);
  }
  void appendExpression(Bexpression *e) { expressions_.push_back(e); }
  std::vector<Bexpression *> expressions() { return expressions_; }

private:
  std::vector<Bexpression *> expressions_;
};

inline ExprListStatement *Bstatement::castToExprListStatement() {
  return (flavor_ == ST_ExprList ? static_cast<ExprListStatement *>(this)
                                 : nullptr);
}

// "If" placeholder statement.

class IfPHStatement : public Bstatement {
public:
  IfPHStatement(Bfunction *function, Bexpression *cond,
                Bstatement *ifTrue, Bstatement *ifFalse,
                Location location)
      : Bstatement(function, ST_IfPlaceholder, location)
      , cond_(cond)
      , iftrue_(ifTrue)
      , iffalse_(ifFalse) {}
  Bexpression *cond() { return cond_; }
  Bstatement *trueStmt() { return iftrue_; }
  Bstatement *falseStmt() { return iffalse_; }

private:
  Bexpression *cond_;
  Bstatement *iftrue_;
  Bstatement *iffalse_;
};

inline IfPHStatement *Bstatement::castToIfPHStatement() {
  return (flavor_ == ST_IfPlaceholder ? static_cast<IfPHStatement *>(this)
                                      : nullptr);
}

// "Switch" placeholder statement.

class SwitchPHStatement : public Bstatement {
public:
  SwitchPHStatement(Bfunction *function, Bexpression *value,
                    const std::vector<std::vector<Bexpression *>> &cases,
                    const std::vector<Bstatement *> &statements,
                    Location location)
      : Bstatement(function, ST_SwitchPlaceholder, location), value_(value),
        cases_(cases), statements_(statements) {}
  Bexpression *switchValue() { return value_; }
  std::vector<std::vector<Bexpression *>> &cases() { return cases_; }
  std::vector<Bstatement *> &statements() { return statements_; }

private:
  Bexpression *value_;
  std::vector<std::vector<Bexpression *>> cases_;
  std::vector<Bstatement *> statements_;
};

inline SwitchPHStatement *Bstatement::castToSwitchPHStatement() {
  return (flavor_ == ST_SwitchPlaceholder
              ? static_cast<SwitchPHStatement *>(this)
              : nullptr);
}

// Opaque labelID handle.
typedef unsigned LabelId;

class Blabel {
public:
  Blabel(const Bfunction *function, LabelId lab)
      : function_(const_cast<Bfunction *>(function)), lab_(lab) {}
  LabelId label() const { return lab_; }
  Bfunction *function() { return function_; }

private:
  Bfunction *function_;
  LabelId lab_;
};

// A goto statement, representing an unconditional jump to
// some other labeled statement.

class GotoStatement : public Bstatement {
public:
  GotoStatement(Bfunction *function, LabelId label, Location location)
      : Bstatement(function, ST_Goto, location), label_(label) {}
  LabelId targetLabel() const { return label_; }

private:
  LabelId label_;
};

inline GotoStatement *Bstatement::castToGotoStatement() {
  return (flavor_ == ST_Goto ? static_cast<GotoStatement *>(this) : nullptr);
}

// A label statement, representing the target of some jump (conditional
// or unconditional).

class LabelStatement : public Bstatement {
public:
  LabelStatement(Bfunction *function, LabelId label)
      : Bstatement(function, ST_Label, Location()), label_(label) {}
  LabelId definedLabel() const { return label_; }

private:
  LabelId label_;
};

inline LabelStatement *Bstatement::castToLabelStatement() {
  return (flavor_ == ST_Label ? static_cast<LabelStatement *>(this) : nullptr);
}

// A Bblock , which wraps statement list. See the comment
// above on why we need to make it easy to convert between
// blocks and statements.

class Bblock : public CompoundStatement {
 public:
  explicit Bblock(Bfunction *function)
      : CompoundStatement(function) { }
};

#endif // LLVMGOFRONTEND_GO_LLVM_BSTATEMENT_H
