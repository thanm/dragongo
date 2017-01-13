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
#include "go-llvm-bstatement.h"
#include "go-system.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"

static void indent(llvm::raw_ostream &os, unsigned ilevel) {
  for (unsigned i = 0; i < ilevel; ++i)
    os << " ";
}

Bexpression::Bexpression(llvm::Value *value,
                         Btype *btype,
                         Location location)
    : value_(value), btype_(btype), stmt_(nullptr), location_(location)
{
}

Bexpression::~Bexpression()
{
}

bool Bexpression::varExprPending() const
{
  return varContext_.get() != nullptr;
}

VarContext &Bexpression::varContext() const
{
  assert(varContext_.get());
  return *varContext_.get();
}

void Bexpression::setVarExprPending(bool lvalue, unsigned addrLevel)
{
  varContext_.reset(new VarContext(lvalue, addrLevel));
}

void Bexpression::setVarExprPending(const VarContext &src)
{
  varContext_.reset(new VarContext(src));
}

void Bexpression::resetVarExprContext()
{
  varContext_.reset(nullptr);
}

bool Bexpression::compositeInitPending() const
{
  return compositeInitContext_.get() != nullptr;
}

CompositeInitContext &Bexpression::compositeInitContext() const
{
  assert(compositeInitContext_.get());
  return *compositeInitContext_.get();
}

void Bexpression::setCompositeInit(const std::vector<Bexpression *> &vals)
{
  compositeInitContext_.reset(new CompositeInitContext(vals));
}

void Bexpression::finishCompositeInit(llvm::Value *finalizedValue)
{
  assert(value_ == nullptr);
  assert(finalizedValue);
  value_ = finalizedValue;
  compositeInitContext_.reset(nullptr);
}

void Bexpression::incorporateStmt(Bstatement *newst)
{
  if (newst == nullptr)
    return;
  if (stmt() == nullptr) {
    setStmt(newst);
    return;
  }
  CompoundStatement *cs = stmt()->castToCompoundStatement();
  if (!cs) {
    cs = new CompoundStatement(stmt()->function());
    cs->stlist().push_back(stmt());
  }
  cs->stlist().push_back(newst);
  setStmt(cs);
}

// Note that we don't delete expr here; all Bexpression
// deletion is handled in the Llvm_backend destructor

void Bexpression::destroy(Bexpression *expr, WhichDel which) {
  if (which != DelWrappers)
    for (auto inst : expr->instructions())
      delete inst;
  if (which != DelInstructions) {
    if (expr->stmt())
      Bstatement::destroy(expr->stmt(), which);
  }
}

void Bexpression::dump()
{
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os, 0, nullptr, false);
  std::cerr << os.str();
}

void Bexpression::srcDump(Linemap *linemap)
{
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os, 0, linemap, false);
  std::cerr << os.str();
}

void Bexpression::osdump(llvm::raw_ostream &os, unsigned ilevel,
                         Linemap *linemap, bool terse) {
  bool hitValue = false;
  if (! terse) {
    if (linemap) {
      indent(os, ilevel);
      os << linemap->to_string(location()) << "\n";
    }
    if (compositeInitPending()) {
      indent(os, ilevel);
      os << "composite init pending:\n";
      for (auto exp : compositeInitContext().elementExpressions())
        exp->osdump(os, ilevel+2, linemap, terse);
    }
  }
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
  if (!terse && varExprPending()) {
    const VarContext &vc = varContext();
    os << "var pending: lvalue=" <<  (vc.lvalue() ? "yes" : "no")
       << " addrLevel=" << vc.addrLevel() << "\n";
  }
  if (!terse && stmt()) {
    os << "enclosed stmt:\n";
    stmt()->osdump(os, ilevel+2, linemap, terse);
  }
}
