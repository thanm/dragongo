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

void IntegrityVisitor::dumpTag(const char *tag, void *ptr) {
  ss_ << tag << ": ";
  if (includePointers_ == DumpPointers)
    ss_ << ptr;
  ss_ << "\n";
}

void IntegrityVisitor::dump(Bnode *node) {
  dumpTag(node->isStmt() ? "stmt" : "expr", (void*) node);
  node->osdump(ss_, 0, be_->linemap(), false);
}

void IntegrityVisitor::dump(llvm::Instruction *inst) {
  dumpTag("inst", (void*) inst);
  inst->print(ss_);
  ss_ << "\n";
}

bool IntegrityVisitor::setParent(Bnode *child, Bnode *parent)
{
  Bexpression *expr = child->castToBexpression();
  if (expr && be_->moduleScopeValue(expr->value(), expr->btype()))
    return true;
  if (child->flavor() == N_Var && includeVarExprs_ == IgnoreVarExprs)
    return true;
  auto it = nparent_.find(child);
  if (it != nparent_.end()) {
    const char *wh = (child->isStmt() ? "stmt" : "expr");
    ss_ << "error: " << wh << " has multiple parents\n";
    ss_ << "child " << wh << ":\n";
    dump(child);
    ss_ << "parent 1:\n";
    dump(it->second);
    ss_ << "parent 2:\n";
    dump(parent);
    return false;
  }
  nparent_[child] = parent;
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

bool IntegrityVisitor::visit(Bnode *node)
{
  bool rval = true;
  for (auto &child : node->children()) {
    rval = (visit(child) && rval);
    rval = (setParent(child, node) && rval);
  }
  Bexpression *expr = node->castToBexpression();
  if (expr) {
    for (auto inst : expr->instructions())
      rval = (setParent(inst, expr) && rval);
  }
  return rval;
}
