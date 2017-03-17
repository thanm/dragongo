//===-- go-llvm-bvariable.cpp - implementation of 'Bvariable' class ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for class Bvariable.
//
//===----------------------------------------------------------------------===//

#include "go-llvm-bvariable.h"
#include "go-llvm-bexpression.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instruction.h"

Bvariable::Bvariable(Btype *type, Location location,
                     const std::string &name, WhichVar which,
                     bool address_taken, llvm::Value *value)
    : name_(name), value_(value), initializer_(nullptr),
      type_(type), location_(location), which_(which),
      addrtaken_(address_taken), temporary_(false)
{
}

void Bvariable::setInitializer(llvm::Value *init)
{
  assert(initializer_ == nullptr);
  assert(init);
  assert(flavor() == GlobalVar || llvm::isa<llvm::Instruction>(init));
  initializer_ = init;
}

class CollectLastInstVisitor {
 public:
  CollectLastInstVisitor() : inst_(nullptr) { }
  llvm::Instruction *instruction() const { return inst_; }
  void visitNodePre(Bnode *node) { }
  void visitNodePost(Bnode *node) {
    Bexpression *expr = node->castToBexpression();
    if (!expr)
      return;
    if (! expr->instructions().size())
      return;
    inst_ = expr->instructions().back();
  }
 private:
  llvm::Instruction *inst_;
};

void Bvariable::setInitializerExpr(Bexpression *expr)
{
  assert(expr);

  CollectLastInstVisitor vis;
  simple_walk_nodes(expr, vis);
  assert(vis.instruction());
  setInitializer(vis.instruction());
}

llvm::Instruction *Bvariable::initializerInstruction() const
{
  assert(initializer_);
  return llvm::cast<llvm::Instruction>(initializer_);
}

static void indent(llvm::raw_ostream &os, unsigned ilevel) {
  for (unsigned i = 0; i < ilevel; ++i)
    os << " ";
}

void Bvariable::dump()
{
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os, 0, nullptr, false);
  std::cerr << os.str();
}

void Bvariable::srcDump(Linemap *linemap)
{
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os, 0, linemap, false);
  std::cerr << os.str();
}

void Bvariable::osdump(llvm::raw_ostream &os, unsigned ilevel,
                       Linemap *linemap, bool terse) {
  if (! terse) {
    if (linemap) {
      indent(os, ilevel);
      os << linemap->to_string(location_) << "\n";
    }
  }
  indent(os, ilevel);
  const char *whtag = nullptr;
  switch(which_) {
    case ParamVar: whtag = "param"; break;
    case GlobalVar: whtag = "global"; break;
    case LocalVar: whtag = "local"; break;
    case BlockVar: whtag = "block"; break;
    case ErrorVar: whtag = "error"; break;
    default: whtag = "<unknownwhichval>"; break;
  }
  os << whtag << " var '" << name_ << "' type: ";
  type_->osdump(os, 0);
  os << "\n";
}
