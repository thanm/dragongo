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
  if (includePointers() == DumpPointers)
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

static bool varExprOrConvVar(Bnode *node)
{
  Bexpression *expr = node->castToBexpression();
  if (!expr)
    return false;
  if (expr->flavor() == N_Var)
    return true;
  if (expr->flavor() == N_Conversion &&
      expr->getChildExprs()[0]->flavor() == N_Var)
    return true;
  return false;
}

bool IntegrityVisitor::shouldBeTracked(Bnode *child)
{
  Bexpression *expr = child->castToBexpression();
  if (expr && be_->moduleScopeValue(expr->value(), expr->btype()))
    return false;
  if (includeVarExprs() == IgnoreVarExprs && varExprOrConvVar(child))
    return false;
  return true;
}

void IntegrityVisitor::unsetParent(Bnode *child, Bnode *parent, unsigned slot)
{
  if (! shouldBeTracked(child))
    return;
  auto it = nparent_.find(child);
  assert(it != nparent_.end());
  parslot pps = it->second;
  Bnode *prevParent = pps.first;
  unsigned prevSlot = pps.second;
  assert(prevParent == parent);
  assert(prevSlot == slot);
  nparent_.erase(it);
}

void IntegrityVisitor::setParent(Bnode *child, Bnode *parent, unsigned slot)
{
  if (! shouldBeTracked(child))
    return;
  auto it = nparent_.find(child);
  if (it != nparent_.end()) {
    parslot pps = it->second;
    Bnode *prevParent = pps.first;
    unsigned prevSlot = pps.second;
    if (prevParent == parent && prevSlot == slot)
      return;
    const char *wh = nullptr;
    if (child->isStmt()) {
      stmtShareCount_ += 1;
      wh = "stmt";
    } else {
      exprShareCount_ += 1;
      wh = "expr";
    }
    ss_ << "error: " << wh << " has multiple parents\n";
    ss_ << "child " << wh << ":\n";
    dump(child);
    ss_ << "parent 1:\n";
    dump(prevParent);
    ss_ << "parent 2:\n";
    dump(parent);
    parslot ps = std::make_pair(parent, slot);
    sharing_.push_back(std::make_pair(child, ps));
  }
  nparent_[child] = std::make_pair(parent, slot);
}

void IntegrityVisitor::setParent(llvm::Instruction *inst,
                                 Bexpression *exprParent,
                                 unsigned slot)
{
  auto it = iparent_.find(inst);
  if (it != iparent_.end()) {
    parslot ps = it->second;
    Bnode *prevParent = ps.first;
    unsigned prevSlot = ps.second;
    if (prevParent == exprParent && prevSlot == slot)
      return;
    instShareCount_ += 1;
    ss_ << "error: instruction has multiple parents\n";
    dump(inst);
    ss_ << "parent 1:\n";
    dump(prevParent);
    ss_ << "parent 2:\n";
    dump(exprParent);
  } else {
    iparent_[inst] = std::make_pair(exprParent, slot);
  }
}

bool IntegrityVisitor::repairableSubTree(Bexpression *root)
{
  std::set<NodeFlavor> acceptable;
  acceptable.insert(N_Const);
  acceptable.insert(N_Var);
  acceptable.insert(N_Conversion);
  acceptable.insert(N_Deref);
  acceptable.insert(N_StructField);

  std::set<Bexpression *> visited;
  visited.insert(root);
  std::vector<Bexpression *> workList;
  workList.push_back(root);

  while (! workList.empty()) {
    Bexpression *e = workList.back();
    workList.pop_back();
    if (acceptable.find(e->flavor()) == acceptable.end())
      return false;
    for (auto &c : e->children()) {
      Bexpression *ce = c->castToBexpression();
      assert(ce);
      if (visited.find(ce) == visited.end()) {
        visited.insert(ce);
        workList.push_back(ce);
      }
    }
  }
  return true;
}

bool IntegrityVisitor::repair(Bnode *node)
{
  std::set<Bexpression *> visited;
  for (auto &p : sharing_) {
    Bexpression *child = p.first->castToBexpression();
    parslot ps = p.second;
    Bnode *parent = ps.first;
    unsigned slot = ps.second;
    assert(child);
    if (visited.find(child) == visited.end()) {
      // Repairable?
      if (!repairableSubTree(child))
        return false;
      visited.insert(child);
    }
    Bexpression *childClone = be_->nodeBuilder().cloneSubtree(child);
    parent->replaceChild(slot, childClone);
  }
  return true;
}

void IntegrityVisitor::visit(Bnode *node)
{
  unsigned idx = 0;
  for (auto &child : node->children()) {
    visit(child);
    setParent(child, node, idx++);
  }
  Bexpression *expr = node->castToBexpression();
  if (expr) {
    idx = 0;
    for (auto inst : expr->instructions())
      setParent(inst, expr, idx++);
  }
}

bool IntegrityVisitor::examine(Bnode *node)
{
  // Walk the tree to see what sort of sharing we have.
  visit(node);

  // Inst sharing and statement sharing are not repairable.
  if (instShareCount_ != 0 || stmtShareCount_ != 0)
    return false;

  if (exprShareCount_ == 0)
    return true;

  if (doRepairs() != RepairSharing)
    return false;

  // Attempt repair..
  if (repair(node))
    return true;

  // Repair failed -- return failure
  return false;
}
