//===-- go-llvm-bexpression.cpp - implementation of 'Bexpression' class ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for class Bexpression.
//
//===----------------------------------------------------------------------===//

#include "go-llvm-btype.h"
#include "go-llvm-bexpression.h"
#include "go-system.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"

static void indent(llvm::raw_ostream &os, unsigned ilevel) {
  for (unsigned i = 0; i < ilevel; ++i)
    os << " ";
}

Bexpression::Bexpression(NodeFlavor fl, const std::vector<Bnode *> &kids,
                         llvm::Value *val, Btype *typ, Location loc)
    : Bnode(fl, kids, loc)
    , value_(val)
    , btype_(typ)
{
}

Bexpression::Bexpression(const Bexpression &src)
    : Bnode(src)
    , value_(src.value_)
    , btype_(src.btype_)
{
}

Bexpression::~Bexpression()
{
}

bool Bexpression::varExprPending() const
{
  return varContext_.pending();
}

const VarContext &Bexpression::varContext() const
{
  return varContext_;
}

void Bexpression::setVarExprPending(bool lvalue, unsigned addrLevel)
{
  varContext_.setPending(lvalue, addrLevel);
}

void Bexpression::setVarExprPending(const VarContext &src)
{
  assert(src.pending());
  varContext_ = src;
}

void Bexpression::resetVarExprContext()
{
  varContext_.reset();
}

Bexpression *Bexpression::cloneConstant() const {
  assert(flavor() == N_Const ||
         flavor() == N_Composite ||
         flavor() == N_FcnAddress);
  assert(llvm::isa<llvm::Constant>(value()));
  Bexpression *rval = new Bexpression(*this);
  return rval;
}

bool Bexpression::compositeInitPending() const
{
  return flavor() == N_Composite && value() == nullptr;
}

const std::vector<Bexpression *> Bexpression::getChildExprs() const
{
  std::vector<Bexpression *> rval;
  for (auto &k : children()) {
    Bexpression *e = k->castToBexpression();
    assert(e);
    rval.push_back(e);
  }
  return rval;
}

void Bexpression::srcDump(Llvm_linemap *linemap)
{
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os, 0, linemap, false);
  std::cerr << os.str();
}

void Bexpression::dumpInstructions(llvm::raw_ostream &os, unsigned ilevel,
                                   Llvm_linemap *linemap, bool terse) const {
  bool hitValue = false;
  for (auto inst : instructions()) {
    indent(os, ilevel);
    char c = ' ';
    if (inst == value()) {
      c = '*';
      hitValue = true;
    }
    if (! terse)
      os << c;
    inst->print(os);
    os << "\n";
  }
  if (!terse && !hitValue) {
    indent(os, ilevel);
    if (value())
      value()->print(os);
    else
      os << "<nil value>";
    os << "\n";
  }
}
