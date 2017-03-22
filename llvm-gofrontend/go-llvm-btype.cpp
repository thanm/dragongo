//===-- go-llvm-btype.cpp - LLVM implementation of 'Btype'  ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for class Btype and its derived classes.
//
//===----------------------------------------------------------------------===//

#include "go-llvm-btype.h"
#include "go-llvm-bexpression.h"
#include "go-system.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"

static void indent(llvm::raw_ostream &os, unsigned ilevel) {
  for (unsigned i = 0; i < ilevel; ++i)
    os << " ";
}

void Btype::dump() const
{
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os, 0);
  std::cerr << os.str() << "\n";
}

void Btype::osdump(llvm::raw_ostream &os, unsigned ilevel) const {
  indent(os, ilevel);
  if (isPlaceholder())
    os << "[ph] ";
  const BIntegerType *bit = castToBIntegerType();
  if (bit && bit->isUnsigned())
    os << "[uns] ";
  if (! name_.empty())
    os << "'" << name_ << "' ";
  if (type_)
    type_->print(os);
  else
    os << "<nil type_>\n";
}

bool Btype::equal(const Btype &other) const
{
  if (this == &other)
    return true;
  if (flavor() != other.flavor())
    return false;
  if (name() != other.name())
    return false;
  if (isPlaceholder() != other.isPlaceholder())
    return false;
  if (type() != other.type())
    return false;
  switch(flavor_) {
    case AuxT:
    case FloatT: {
      return true;
    }
    case IntegerT: {
      const BIntegerType *bit = castToBIntegerType();
      const BIntegerType *obit = other.castToBIntegerType();
      return bit->isUnsigned() == obit->isUnsigned();
    }
    case PointerT: {
      const BPointerType *bpt = castToBPointerType();
      const BPointerType *obpt = other.castToBPointerType();
      if ((bpt->toType() != nullptr) != (obpt->toType() != nullptr))
        return false;
      return bpt->toType()->equal(*obpt->toType());
    }
    case ArrayT: {
      const BArrayType *bat = castToBArrayType();
      const BArrayType *obat = other.castToBArrayType();
      if ((bat->elemType() != nullptr) != (obat->elemType() != nullptr))
        return false;
      return (bat->nelSize() == obat->nelSize() &&
              bat->elemType()->equal(*obat->elemType()));
    }
    case StructT: {
      const BStructType *bst = castToBStructType();
      const BStructType *obst = other.castToBStructType();
      const std::vector<Backend::Btyped_identifier> &ft = bst->fields();
      const std::vector<Backend::Btyped_identifier> &fo = obst->fields();
      if (ft.size() != fo.size())
        return false;
      for (unsigned i = 0; i < ft.size(); ++i) {
        if (! ft[i].btype->equal(*fo[i].btype))
          return false;
        if (ft[i].name != fo[i].name)
          return false;
        if (! (ft[i].location == fo[i].location))
          return false;
      }
      return true;
    }
    case FunctionT: {
      const BFunctionType *bft = castToBFunctionType();
      const BFunctionType *obft = other.castToBFunctionType();
      if ((bft->receiverType() != nullptr) !=
          (obft->receiverType() != nullptr))
        return false;
      if (bft->receiverType() &&
          ! bft->receiverType()->equal(*obft->receiverType()))
        return false;
      if (! bft->resultType()->equal(*obft->resultType()))
        return false;
      const std::vector<Btype *> &pt = bft->paramTypes();
      const std::vector<Btype *> &po = obft->paramTypes();
      if (pt.size() != po.size())
        return false;
      for (unsigned i = 0; i < pt.size(); ++i) {
        if (! pt[i]->equal(*po[i]))
          return false;
      }
      if (bft->followsCabi() != obft->followsCabi())
        return false;
      return true;
    }
  }

  // unreached
  return false;
}

unsigned Btype::hash() const
{
  unsigned hv = static_cast<unsigned>(flavor());
  std::size_t hn = std::hash<std::string>{}(name());
  std::size_t ht = std::hash<llvm::Type *>{}(type());
  unsigned h = ((hn + ht) << 3) | hv;
  return h;
}

Btype *Btype::clone() const
{
  Btype *rval = nullptr;
  switch(flavor_) {
    case AuxT:
    case FloatT: {
      rval = new Btype(flavor(), type(), location());
      break;
    }
    case IntegerT: {
      const BIntegerType *bit = castToBIntegerType();
      rval = bit->clone();
      break;
    }
    case PointerT: {
      const BPointerType *bpt = castToBPointerType();
      rval = bpt->clone();
      break;
    }
    case ArrayT: {
      const BArrayType *bat = castToBArrayType();
      rval = bat->clone();
      break;
    }
    case StructT: {
      const BStructType *bst = castToBStructType();
      rval = bst->clone();
      break;
    }
    case FunctionT: {
      const BFunctionType *bft = castToBFunctionType();
      rval = bft->clone();
      break;
    }
  }
  if (type())
    rval->setType(type());
  rval->setPlaceholder(isPlaceholder());
  rval->setName(name());
  return rval;
}

bool Btype::isUnresolvedPlaceholder() const {
  if (flavor() == StructT)
    return false;
  return isPlaceholder();
}

uint64_t BArrayType::nelSize() const
{
  llvm::ConstantInt *lc =
      llvm::dyn_cast<llvm::ConstantInt>(nelements_->value());
  assert(lc);
  uint64_t asize = lc->getValue().getZExtValue();
  return asize;
}
