//===-- go-llvm-bfunction.cpp - implementation of 'Bfunction' class ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for class Bfunction.
//
//===----------------------------------------------------------------------===//

#include "go-llvm-bfunction.h"

#include "go-llvm-btype.h"
#include "go-llvm-bstatement.h"
#include "go-llvm-bexpression.h"
#include "go-llvm-bvariable.h"
#include "go-system.h"

#include "llvm/IR/Argument.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Value.h"

Bfunction::Bfunction(llvm::Function *f,
                     BFunctionType *fcnType,
                     const std::string &name,
                     const std::string &asmName,
                     Location location)
    : function_(f), fcnType_(fcnType), name_(name), asmName_(asmName),
      labelCount_(0), location_(location), splitStack_(YesSplit),
      prologGenerated_(false)
{
}

Bfunction::~Bfunction()
{
  if (! prologGenerated_) {
    for (auto ais : allocas_)
      delete ais;
    for (auto &kv : argToVal_)
      delete kv.second;
  }
  for (auto &lab : labels_)
    delete lab;
  for (auto &kv : valueVarMap_)
    delete kv.second;
}

Bvariable *Bfunction::parameter_variable(const std::string &name,
                                         Btype *btype, bool is_address_taken,
                                         Location location) {
  // Collect argument pointer
  unsigned argIdx = paramsCreated();
  llvm::Argument *arg = getNthArg(argIdx);
  assert(arg);

  // Set name
  arg->setName(name);

  // Create the alloca slot where we will spill this argument
  llvm::Instruction *inst = argValue(arg);
  assert(valueVarMap_.find(inst) == valueVarMap_.end());
  Bvariable *bv =
      new Bvariable(btype, location, name, ParamVar, is_address_taken, inst);
  valueVarMap_[inst] = bv;
  return bv;
}

Bvariable *Bfunction::local_variable(const std::string &name,
                                     Btype *btype,
                                     bool is_address_taken,
                                     Location location) {
  llvm::Instruction *inst = new llvm::AllocaInst(btype->type(), name);
  // inst->setDebugLoc(location.debug_location());
  addAlloca(inst);
  Bvariable *bv =
      new Bvariable(btype, location, name, LocalVar, is_address_taken, inst);

  assert(valueVarMap_.find(bv->value()) == valueVarMap_.end());
  valueVarMap_[bv->value()] = bv;
  return bv;
}

std::vector<Bvariable*> Bfunction::getParameterVars()
{
  std::vector<Bvariable*> res;
  for (auto &arg : arguments_) {
    auto ait = argToVal_.find(arg);
    assert(ait != argToVal_.end());
    llvm::Value *argval = ait->second;
    auto it = valueVarMap_.find(argval);
    assert(it != valueVarMap_.end());
    Bvariable *v = it->second;
    res.push_back(v);
  }
  return res;
}

std::vector<Bvariable*> Bfunction::getFunctionLocalVars()
{
  std::vector<Bvariable*> res;
  for (auto &aa : allocas_) {
    auto it = valueVarMap_.find(aa);
    assert(it != valueVarMap_.end());
    Bvariable *v = it->second;
    if (v->flavor() == LocalVar)
      res.push_back(v);
  }
  return res;
}

llvm::Argument *Bfunction::getNthArg(unsigned argIdx) {
  assert(function()->getFunctionType()->getNumParams() != 0);
  if (arguments_.empty())
    for (auto &arg : function()->getArgumentList())
      arguments_.push_back(&arg);
  assert(argIdx < arguments_.size());
  return arguments_[argIdx];
}

Bvariable *Bfunction::getBvarForValue(llvm::Value *val)
{
  auto it = valueVarMap_.find(val);
  assert(it != valueVarMap_.end());
  return it->second;
}

llvm::Value *Bfunction::getNthArgValue(unsigned argIdx)
{
  llvm::Argument *arg = getNthArg(argIdx);
  llvm::Value *llval = argValue(arg);
  return llval;
}

llvm::Instruction *Bfunction::argValue(llvm::Argument *arg) {
  auto it = argToVal_.find(arg);
  if (it != argToVal_.end())
    return it->second;

  // Create alloca save area for argument, record that and return
  // it. Store into alloca will be generated later.
  std::string aname(arg->getName());
  aname += ".addr";
  llvm::Instruction *inst = new llvm::AllocaInst(arg->getType(), aname);
  assert(argToVal_.find(arg) == argToVal_.end());
  argToVal_[arg] = inst;
  return inst;
}

Bvariable *Bfunction::getNthParamVar(unsigned argIdx)
{
  return getBvarForValue(argValue(getNthArg(argIdx)));
}

void Bfunction::genProlog(llvm::BasicBlock *entry) {
  llvm::Function *func = function();

  unsigned nParms = func->getFunctionType()->getNumParams();
  std::vector<llvm::Instruction *> spills;
  for (unsigned idx = 0; idx < nParms; ++idx) {
    llvm::Argument *arg = getNthArg(idx);
    llvm::Instruction *inst = argValue(arg);
    entry->getInstList().push_back(inst);
    // FIXME: handle too-big-to-store composite value
    llvm::Instruction *si = new llvm::StoreInst(arg, inst);
    spills.push_back(si);
    Bvariable *v = getNthParamVar(idx);
    v->setInitializer(si);
  }

  // Append allocas for local variables
  for (auto aa : allocas_)
    entry->getInstList().push_back(aa);

  // Param spills
  for (auto sp : spills)
    entry->getInstList().push_back(sp);

  prologGenerated_ = true;
}

Blabel *Bfunction::newLabel(Location loc) {
  Blabel *lb = new Blabel(this, labelCount_++, loc);
  labelmap_.push_back(nullptr);
  labels_.push_back(lb);
  return lb;
}

void Bfunction::registerLabelDefStatement(Bstatement *st, Blabel *label)
{
  assert(st && st->flavor() == N_LabelStmt);
  assert(label);
  assert(labelmap_[label->label()] == nullptr);
  labelmap_[label->label()] = st;
}
