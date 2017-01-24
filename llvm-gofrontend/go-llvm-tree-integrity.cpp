//===-- go-llvm-tree-integrity.cpp - tree integrity utils impl ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for class IntegrityVisitor.
//
//===----------------------------------------------------------------------===//

#include "go-llvm-bexpression.h"
#include "go-llvm-bstatement.h"
#include "go-llvm-tree-integrity.h"
#include "go-llvm.h"

#include "llvm/IR/Instruction.h"

IntegrityVisitor::eors IntegrityVisitor::makeExprParent(Bexpression *expr) {
  Bstatement *stmt = nullptr;
  eors rval(std::make_pair(expr, stmt));
  return rval;
}

IntegrityVisitor::eors IntegrityVisitor::makeStmtParent(Bstatement *stmt) {
  Bexpression *expr = nullptr;
  eors rval(std::make_pair(expr, stmt));
  return rval;
}

void IntegrityVisitor::dumpTag(const char *tag, void *ptr) {
  ss_ << tag << ": ";
  if (includePointers_)
    ss_ << ptr;
  ss_ << "\n";
}

void IntegrityVisitor::dump(Bexpression *expr) {
  dumpTag("expr", (void*) expr);
  expr->osdump(ss_, 0, be_->linemap(), false);
}

void IntegrityVisitor::dump(Bstatement *stmt) {
  dumpTag("stmt", (void*) stmt);
  stmt->osdump(ss_, 0, be_->linemap(), false);
}

void IntegrityVisitor::dump(llvm::Instruction *inst) {
  dumpTag("inst", (void*) inst);
  inst->print(ss_);
  ss_ << "\n";
}

void IntegrityVisitor::dump(const IntegrityVisitor::eors &pair) {
  Bexpression *expr = pair.first;
  Bstatement *stmt = pair.second;
  if (expr)
    dump(expr);
  else
    dump(stmt);
}

bool IntegrityVisitor::setParent(Bstatement *child, const eors &parent)
{
  auto it = sparent_.find(child);
  if (it != sparent_.end()) {
    ss_ << "error: statement has multiple parents\n";
    ss_ << "child stmt:\n";
    dump(child);
    ss_ << "parent 1:\n";
    dump(it->second);
    ss_ << "parent 2:\n";
    dump(parent);
    return false;
  }
  sparent_[child] = parent;
  return true;
}

bool IntegrityVisitor::setParent(Bexpression *child, const eors &parent)
{
  if (be_->moduleScopeValue(child->value(), child->btype()))
    return true;
  auto it = eparent_.find(child);
  if (it != eparent_.end()) {
    ss_ << "error: expression has multiple parents\n";
    ss_ << "child expr:\n";
    dump(child);
    ss_ << "parent 1:\n";
    dump(it->second);
    ss_ << "parent 2:\n";
    dump(parent);
    return false;
  }
  eparent_[child] = parent;
  return true;
}

bool IntegrityVisitor::setParent(llvm::Instruction *inst,
                                 Bexpression *exprParent)
{
  auto it = iparent_.find(inst);
  if (it != iparent_.end()) {
    ss_ << "error: instruction has multiple parents\n";
    dump(inst);
    ss_ << "parent 1:\n";
    dump(it->second);
    ss_ << "parent 2:\n";
    dump(exprParent);
    return false;
  }
  iparent_[inst] = exprParent;
  return true;
}

bool IntegrityVisitor::visit(Bexpression *expr)
{
  bool rval = true;
  if (expr->stmt()) {
    eors parthis = makeExprParent(expr);
    rval = (setParent(expr->stmt(), parthis) && rval);
    visit(expr->stmt());
  }
  if (expr->compositeInitPending()) {
    eors parthis = makeExprParent(expr);
    for (auto initval : expr->compositeInitContext().elementExpressions())
      rval = (setParent(initval, parthis) && rval);
  }
  for (auto inst : expr->instructions())
    rval = (setParent(inst, expr) && rval);
  return rval;
}

bool IntegrityVisitor::visit(Bstatement *stmt)
{
  bool rval = true;
  switch (stmt->flavor()) {
    case Bstatement::ST_Compound: {
      CompoundStatement *cst = stmt->castToCompoundStatement();
      for (auto st : cst->stlist())
        rval = (visit(st) && rval);
      eors parcst = makeStmtParent(cst);
      for (auto st : cst->stlist()) {
        rval = (setParent(st, parcst) && rval);
      }
      break;
    }
    case Bstatement::ST_ExprList: {
      ExprListStatement *elst = stmt->castToExprListStatement();
      for (auto expr : elst->expressions()) {
        rval = (visit(expr) && rval);
        eors stparent(std::make_pair(nullptr, elst));
        rval = (setParent(expr, stparent) && rval);
      }
      break;
    }
    case Bstatement::ST_IfPlaceholder: {
      IfPHStatement *ifst = stmt->castToIfPHStatement();
      eors parif = makeStmtParent(ifst);
      rval = (visit(ifst->cond()) && rval);
      rval = (setParent(ifst->cond(), parif) && rval);
      if (ifst->trueStmt()) {
        rval = (visit(ifst->trueStmt()) && rval);
        rval = (setParent(ifst->trueStmt(), parif) && rval);
      }
      if (ifst->falseStmt()) {
        rval = (visit(ifst->falseStmt()) && rval);
        rval = (setParent(ifst->falseStmt(), parif) && rval);
      }
      break;
    }
    case Bstatement::ST_Label:
    case Bstatement::ST_Goto:
      // not interesting
      break;
    case Bstatement::ST_SwitchPlaceholder: {
      SwitchPHStatement *swst = stmt->castToSwitchPHStatement();
      eors parsw = makeStmtParent(swst);
      rval = (visit(swst->switchValue()) && rval);
      rval = (setParent(swst->switchValue(), parsw) && rval);
      const std::vector<std::vector<Bexpression *>> &cases = swst->cases();
      for (auto &cs : cases) {
        for (auto &cv : cs) {
          rval = (visit(cv) && rval);
          rval = (setParent(cv, parsw) && rval);
        }
      }
      const std::vector<Bstatement *> &statements = swst->statements();
      for (auto st : statements) {
        rval = (visit(st) && rval);
        rval = (setParent(st, parsw) && rval);
      }
      break;
    }
  }
  return rval;
}
