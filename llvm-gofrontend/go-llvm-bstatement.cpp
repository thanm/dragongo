//===-- go-llvm-bstatement.cpp - implementation of 'Bstatement' class ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for class Bstatement.
//
//===----------------------------------------------------------------------===//

#include "go-llvm-btype.h"
#include "go-llvm-bstatement.h"
#include "go-system.h"

#include "llvm/Support/raw_ostream.h"

Bstatement::Bstatement(NodeFlavor fl,
                       Bfunction *func,
                       const std::vector<Bnode *> &kids,
                       Location loc)
    : Bnode(fl, kids, loc), function_(func)
{
}

Bstatement::~Bstatement()
{
}

void Bstatement::srcDump(Llvm_linemap *linemap)
{
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os, 0, linemap, false);
  std::cerr << os.str();
}

std::vector<Bstatement *> Bstatement::getChildStmts()
{
  std::vector<Bstatement *> rval;
  for (auto &child : children()) {
    Bstatement *st = child->castToBstatement();
    if (st)
      rval.push_back(st);
  }
  return rval;
}

Bexpression *Bstatement::getNthChildAsExpr(NodeFlavor fl, unsigned cidx)
{
  assert(flavor() == fl);
  const std::vector<Bnode *> &kids = children();
  assert(cidx < kids.size());
  Bexpression *e = kids[cidx]->castToBexpression();
  assert(e);
  return e;
}

Bstatement *Bstatement::getNthChildAsStmt(NodeFlavor fl, unsigned cidx)
{
  assert(flavor() == fl);
  const std::vector<Bnode *> &kids = children();
  assert(cidx < kids.size());
  Bstatement *s = kids[cidx]->castToBstatement();
  assert(s);
  return s;
}

Bexpression *Bstatement::getExprStmtExpr()
{
  return getNthChildAsExpr(N_ExprStmt, 0);
}

Bexpression *Bstatement::getReturnStmtExpr()
{
  return getNthChildAsExpr(N_ReturnStmt, 0);
}

Bexpression *Bstatement::getIfStmtCondition()
{
  return getNthChildAsExpr(N_IfStmt, 0);
}

Bstatement *Bstatement::getIfStmtTrueBlock()
{
  return getNthChildAsStmt(N_IfStmt, 1);
}

Bstatement *Bstatement::getIfStmtFalseBlock()
{
  return getNthChildAsStmt(N_IfStmt, 2);
}

Bexpression *Bstatement::getDeferStmtUndeferCall()
{
  return getNthChildAsExpr(N_DeferStmt, 0);
}

Bexpression *Bstatement::getDeferStmtDeferCall()
{
  return getNthChildAsExpr(N_DeferStmt, 1);
}

Bexpression *Bstatement::getSwitchStmtValue()
{
  return getNthChildAsExpr(N_SwitchStmt, 0);
}

Bstatement *Bstatement::getExcepStmtBody()
{
  return getNthChildAsStmt(N_ExcepStmt, 0);
}

Bstatement *Bstatement::getExcepStmtOnException()
{
  return getNthChildAsStmt(N_ExcepStmt, 1);
}

Bstatement *Bstatement::getExcepStmtFinally()
{
  return getNthChildAsStmt(N_ExcepStmt, 2);
}

unsigned Bstatement::getSwitchStmtNumCases()
{
  assert(flavor() == N_SwitchStmt);
  SwitchDescriptor *swcases = getSwitchCases();
  return swcases->cases().size();
}

std::vector<Bexpression *> Bstatement::getSwitchStmtNthCase(unsigned idx)
{
  assert(flavor() == N_SwitchStmt);
  SwitchDescriptor *swcases = getSwitchCases();
  assert(idx < swcases->cases().size());
  const SwitchCaseDesc &cdesc = swcases->cases().at(idx);
  std::vector<Bexpression *> rval;
  const std::vector<Bnode *> &kids = children();
  for (unsigned ii = 0; ii < cdesc.len; ++ii) {
    Bexpression *e = kids[ii+cdesc.st]->castToBexpression();
    assert(e);
    rval.push_back(e);
  }
  return rval;
}

Bstatement *Bstatement::getSwitchStmtNthStmt(unsigned idx)
{
  assert(flavor() == N_SwitchStmt);
  SwitchDescriptor *swcases = getSwitchCases();
  assert(idx < swcases->cases().size());
  const SwitchCaseDesc &cdesc = swcases->cases().at(idx);
  const std::vector<Bnode *> &kids = children();
  Bstatement *st = kids[cdesc.stmt]->castToBstatement();
  assert(st);
  return st;
}

LabelId Bstatement::getGotoStmtTargetLabel()
{
  return label();
}

LabelId Bstatement::getLabelStmtDefinedLabel()
{
  return label();
}

Bblock::Bblock(Bfunction *func,
               const std::vector<Bvariable *> &vars,
               Location loc)
    : Bstatement(N_BlockStmt, func, std::vector<Bnode *>(), loc)
    , vars_(vars)
{
}

void Bblock::addTemporaryVariable(Bvariable *var)
{
  vars_.push_back(var);
}

void Bblock::clearStatements()
{
  removeAllChildren();
}
