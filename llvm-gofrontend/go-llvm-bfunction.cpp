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
#include "go-llvm-cabi-oracle.h"
#include "go-llvm-typemanager.h"
#include "go-llvm-irbuilders.h"
#include "go-system.h"

#include "llvm/IR/Argument.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Value.h"

Bfunction::Bfunction(llvm::Function *f,
                     BFunctionType *fcnType,
                     const std::string &name,
                     const std::string &asmName,
                     Location location,
                     TypeManager *tm)

    : fcnType_(fcnType), function_(f),
      abiOracle_(new CABIOracle(fcnType, tm)),
      rtnValueMem_(nullptr), paramsRegistered_(0),
      name_(name), asmName_(asmName),
      location_(location), splitStack_(YesSplit),
      prologGenerated_(false)
{
  abiSetup();
}

Bfunction::~Bfunction()
{
  if (! prologGenerated_) {
    for (auto ais : allocas_)
      delete ais;
  }
  for (auto &lab : labels_)
    delete lab;
  for (auto &kv : valueVarMap_)
    delete kv.second;
}

std::string Bfunction::namegen(const std::string &tag)
{
  return abiOracle_->tm()->tnamegen(tag);
}

llvm::Instruction *Bfunction::addAlloca(llvm::Type *typ,
                                        const std::string &name)
{
  llvm::Instruction *inst = new llvm::AllocaInst(typ);
  if (! name.empty())
    inst->setName(name);
  allocas_.push_back(inst);
  return inst;
}

void Bfunction::abiSetup()
{
  // Populate argument list
  if (arguments_.empty())
    for (auto &arg : function()->getArgumentList())
      arguments_.push_back(&arg);

  // If the return value is going to be passed via memory, make a note
  // of the argument in question, and set up the arg.
  unsigned argIdx = 0;
  if (abiOracle_->returnInfo().disp() == ParmIndirect) {
    std::string sretname(namegen("sret.tmp"));
    arguments_[0]->setName(sretname);
    arguments_[0]->addAttr(llvm::Attribute::StructRet);
    rtnValueMem_ = arguments_[0];
    argIdx += 1;
  }

  // Sort out what to do with each of the parameters.
  const std::vector<Btype *> &paramTypes = fcnType()->paramTypes();
  for (unsigned idx = 0; idx < paramTypes.size(); ++idx) {
    const CABIParamInfo &paramInfo = abiOracle_->paramInfo(idx);
    switch(paramInfo.disp()) {
      case ParmIgnore: {
        // Seems weird to create a zero-sized alloca(), but it should
        // simplify things in that we can avoid having a Bvariable with a
        // null value.
        llvm::Type *llpt = paramTypes[idx]->type();
        llvm::Instruction *inst = addAlloca(llpt, "");
        paramValues_.push_back(inst);
        break;
      }
      case ParmIndirect: {
        paramValues_.push_back(arguments_[argIdx]);
        assert(paramInfo.numArgSlots() == 1);
        arguments_[argIdx]->addAttr(llvm::Attribute::ByVal);
        argIdx += 1;
        break;
      }
      case ParmDirect: {
        llvm::Type *llpt = paramTypes[idx]->type();
        llvm::Instruction *inst = addAlloca(llpt, "");
        paramValues_.push_back(inst);
        if (paramInfo.attr() == AttrSext)
          arguments_[argIdx]->addAttr(llvm::Attribute::SExt);
        else if (paramInfo.attr() == AttrZext)
          arguments_[argIdx]->addAttr(llvm::Attribute::ZExt);
        argIdx += paramInfo.numArgSlots();
        break;
      }
    }
  }
}

Bvariable *Bfunction::parameter_variable(const std::string &name,
                                         Btype *btype,
                                         bool is_address_taken,
                                         Location location)
{
  unsigned argIdx = paramsRegistered_++;

  // Create Bvariable and install in value->var map.
  llvm::Value *argVal = paramValues_[argIdx];
  assert(valueVarMap_.find(argVal) == valueVarMap_.end());
  Bvariable *bv =
      new Bvariable(btype, location, name, ParamVar, is_address_taken, argVal);
  valueVarMap_[argVal] = bv;

  // Set parameter name or names.
  const CABIParamInfo &paramInfo = abiOracle_->paramInfo(argIdx);
  switch(paramInfo.disp()) {
    case ParmIgnore: {
      std::string iname(name);
      iname += ".ignore";
      paramValues_[argIdx]->setName(name);
      break;
    }
    case ParmIndirect: {
      unsigned soff = paramInfo.sigOffset();
      arguments_[soff]->setName(name);
      break;
    }
    case ParmDirect: {
      unsigned soff = paramInfo.sigOffset();
      if (paramInfo.abiTypes().size() == 1) {
        arguments_[soff]->setName(name);
      } else {
        assert(paramInfo.abiTypes().size() == 2);
        std::string argp1(name); argp1 += ".chunk0";
        std::string argp2(name); argp2 += ".chunk1";
        arguments_[soff]->setName(argp1);
        arguments_[soff+1]->setName(argp2);
      }
      std::string aname(name);
      aname += ".addr";
      paramValues_[argIdx]->setName(aname);
    }
  }

  // All done.
  return bv;
}

Bvariable *Bfunction::local_variable(const std::string &name,
                                     Btype *btype,
                                     bool is_address_taken,
                                     Location location) {
  llvm::Instruction *inst = addAlloca(btype->type(), name);
  Bvariable *bv =
      new Bvariable(btype, location, name, LocalVar, is_address_taken, inst);
  localVariables_.push_back(bv);
  assert(valueVarMap_.find(bv->value()) == valueVarMap_.end());
  valueVarMap_[bv->value()] = bv;
  return bv;
}

llvm::Value *Bfunction::createTemporary(Btype *btype, const std::string &tag)
{
  return addAlloca(btype->type(), tag);
}

std::vector<Bvariable*> Bfunction::getParameterVars()
{
  std::vector<Bvariable*> res;
  for (auto &argval : paramValues_) {
    auto it = valueVarMap_.find(argval);
    assert(it != valueVarMap_.end());
    Bvariable *v = it->second;
    res.push_back(v);
  }
  return res;
}

std::vector<Bvariable*> Bfunction::getFunctionLocalVars()
{
  return localVariables_;
}

Bvariable *Bfunction::getBvarForValue(llvm::Value *val)
{
  auto it = valueVarMap_.find(val);
  assert(it != valueVarMap_.end());
  return it->second;
}

Bvariable *Bfunction::getNthParamVar(unsigned argIdx)
{
  assert(argIdx < paramValues_.size());
  llvm::Value *pval = paramValues_[argIdx];
  return getBvarForValue(pval);
}

unsigned Bfunction::genArgSpill(Bvariable *paramVar,
                                const CABIParamInfo &paramInfo,
                                Binstructions *spillInstructions,
                                unsigned pIdx)
{
  assert(paramInfo.disp() == ParmDirect);
  BinstructionsLIRBuilder builder(function()->getContext(), spillInstructions);
  llvm::Instruction *sploc = llvm::cast<llvm::Instruction>(paramValues_[pIdx]);

  // Simple case: param arrived in single register.
  if (paramInfo.abiTypes().size() == 1) {
    llvm::Argument *arg = arguments_[paramInfo.sigOffset()];
    llvm::Instruction *si = builder.CreateStore(arg, sploc);
    paramVar->setInitializer(si);
    return 1;
  }
  assert(paramInfo.abiTypes().size() == 2);

  // More complex case: param arrives in two registers.

  // Create struct type corresponding to first and second params
  const std::vector<llvm::Type *> &abiTypes = paramInfo.abiTypes();
  llvm::Type *ft0 = abiTypes[0];
  llvm::Type *ft1 = abiTypes[1];
  TypeManager *tm = abiOracle_->tm();
  llvm::Type *llst = tm->makeLLVMTwoElementStructType(ft0, ft1);
  llvm::Type *ptst = tm->makeLLVMPointerType(llst);

  // Cast the spill location to a pointer to the struct created above.
  std::string tag(namegen("cast"));
  llvm::Value *bitcast = builder.CreateBitCast(sploc, ptst, tag);

  // Generate a store to the first field
  std::string tag0(namegen("field0"));
  llvm::Value *field0gep =
      builder.CreateConstInBoundsGEP2_32(llst, bitcast, 0, 0, tag0);
  llvm::Value *argChunk0 = arguments_[paramInfo.sigOffset()];
  builder.CreateStore(argChunk0, field0gep);

  // Generate a store to the second field
  std::string tag1(namegen("field1"));
  llvm::Value *field1gep =
      builder.CreateConstInBoundsGEP2_32(llst, bitcast, 0, 1, tag0);
  llvm::Value *argChunk1 = arguments_[paramInfo.sigOffset()+1];
  builder.CreateStore(argChunk1, field1gep);

  // All done.
  return 2;
}

void Bfunction::genProlog(llvm::BasicBlock *entry)
{
  // Spill any directly-passed arguments to the function
  // into their previous created spill areas.
  Binstructions spills;
  const std::vector<Btype *> &paramTypes = fcnType()->paramTypes();
  unsigned nParms = paramTypes.size();
  unsigned argIdx = (abiOracle_->returnInfo().disp() == ParmIndirect ? 1 : 0);
  for (unsigned pidx = 0; pidx < nParms; ++pidx) {
    const CABIParamInfo &paramInfo = abiOracle_->paramInfo(pidx);
    if (paramInfo.disp() != ParmDirect)
      continue;
    Bvariable *v = getNthParamVar(pidx);
    argIdx += genArgSpill(v, paramInfo, &spills, pidx);
  }

  // Append allocas for local variables
  // FIXME: create lifetime annotations
  for (auto aa : allocas_)
    entry->getInstList().push_back(aa);

  // Param spills
  for (auto sp : spills.instructions())
    entry->getInstList().push_back(sp);

  prologGenerated_ = true;
}

llvm::Value *Bfunction::genReturnSequence(Bexpression *toRet,
                                          Binstructions *retInstrs)
{
  const CABIParamInfo &returnInfo = abiOracle_->returnInfo();
  BinstructionsLIRBuilder builder(function()->getContext(), retInstrs);

  // If we're returning an empty struct, or if the function has void
  // type, then return a null ptr (return "void").
  TypeManager *tm = abiOracle_->tm();
  if (returnInfo.disp() == ParmIgnore ||
      fcnType()->resultType()->type() == tm->llvmVoidType()) {
    llvm::Value *rval = nullptr;
    return rval;
  }

  // Indirect return: emit memcpy into sret
  if (returnInfo.disp() == ParmIndirect) {
    BlockLIRBuilder bbuilder(function());
    Btype *rmemt = tm->makeAuxType(rtnValueMem_->getType());
    uint64_t sz = tm->typeSize(rmemt);
    uint64_t algn = tm->typeAlignment(rmemt);
    bbuilder.CreateMemCpy(rtnValueMem_, toRet->value(), sz, algn);
    for (auto i : bbuilder.instructions()) {
      // hack: irbuilder likes to create unnamed bitcasts
      if (i->isCast())
        i->setName(namegen("cast"));
      retInstrs->appendInstruction(i);
    }
    llvm::Value *rval = nullptr;
    return rval;
  }

  // Direct return: single value
  if (! returnInfo.abiType()->isStructTy())
    return toRet->value();

  // Direct return complex case: two-value struct. Bitcast needed here.
  std::string tag(namegen("cast"));
  llvm::Type *abiRetTyp = returnInfo.abiType();
  llvm::Value *bitcast = builder.CreateBitCast(toRet->value(), abiRetTyp, tag);
  return bitcast;
}

Blabel *Bfunction::newLabel(Location loc) {
  unsigned labelCount = labels_.size();
  Blabel *lb = new Blabel(this, labelCount, loc);
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
