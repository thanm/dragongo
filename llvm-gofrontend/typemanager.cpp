//===-- typemanger.cpp - implementation of TypeManager class --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for TypeManager class.
//
//===----------------------------------------------------------------------===//

#include "typemanager.h"
#include "go-llvm-bexpression.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"

TypeManager::TypeManager(llvm::LLVMContext &context)
    : context_(context)
    , datalayout_(nullptr)
    , addressSpace_(0)
    , traceLevel_(0)
    , nametags_(nullptr)
    , errorExpression_(nullptr)
    , complexFloatType_(nullptr)
    , complexDoubleType_(nullptr)
    , errorType_(nullptr)
    , stringType_(nullptr)
    , llvmVoidType_(nullptr)
    , llvmBoolType_(nullptr)
    , llvmPtrType_(nullptr)
    , llvmSizeType_(nullptr)
    , llvmIntegerType_(nullptr)
    , llvmInt8Type_(nullptr)
    , llvmInt32Type_(nullptr)
    , llvmInt64Type_(nullptr)
    , llvmFloatType_(nullptr)
    , llvmDoubleType_(nullptr)
    , llvmLongDoubleType_(nullptr)
{
  // LLVM doesn't have anything that corresponds directly to the
  // gofrontend notion of an error type. For now we create a so-called
  // 'identified' anonymous struct type and have that act as a
  // stand-in. See http://llvm.org/docs/LangRef.html#structure-type
  errorType_ = makeAuxType(llvm::StructType::create(context_, "$Error$"));

  // For builtin creation
  llvmPtrType_ =
      llvm::PointerType::get(llvm::StructType::create(context), addressSpace_);

  // Assorted pre-computer types for use in builtin function creation
  llvmVoidType_ = llvm::Type::getVoidTy(context_);
  llvmBoolType_ = llvm::IntegerType::get(context_, 1);
  llvmSizeType_ = llvmIntegerType_;
  llvmInt8Type_ = llvm::IntegerType::get(context_, 8);
  llvmInt32Type_ = llvm::IntegerType::get(context_, 32);
  llvmInt64Type_ = llvm::IntegerType::get(context_, 64);
  llvmFloatType_ = llvm::Type::getFloatTy(context_);
  llvmDoubleType_ = llvm::Type::getDoubleTy(context_);
  llvmLongDoubleType_ = llvm::Type::getFP128Ty(context_);

  // Predefined C string type
  stringType_ = pointerType(integerType(true, 8));
}

TypeManager::~TypeManager()
{
  for (auto &t : anonTypes_)
    delete t;
  for (auto &t : placeholders_)
    delete t;
  for (auto &t : duplicates_)
    delete t;
  for (auto &kv : auxTypeMap_)
    delete kv.second;
  for (auto &t : namedTypes_)
    delete t;
}

void TypeManager::initializeTypeManager(Bexpression *errorExpression,
                                        const llvm::DataLayout *datalayout,
                                        NameGen *nt)
{
  assert(errorExpression);
  assert(datalayout);
  assert(nt);
  errorExpression_ = errorExpression;
  nametags_ = nt;
  datalayout_ = datalayout;

  llvmIntegerType_ =
      llvm::IntegerType::get(context_, datalayout_->getPointerSizeInBits());
}



bool TypeManager::removeAnonType(Btype *typ)
{
  auto it = anonTypes_.find(typ);
  if (it != anonTypes_.end()) {
    anonTypes_.erase(it);
    return true;
  }
  return false;
}

void TypeManager::reinstallAnonType(Btype *typ)
{
  auto it = anonTypes_.find(typ);
  if (it != anonTypes_.end()) {
    // type already exists -- take the type we were intending to
    // install in anonTypes_ and record as duplicate.
    duplicates_.insert(typ);
    return;
  }
  anonTypes_.insert(typ);
}

// We have two flavors of placeholders: those created using the
// ::placeholder_*_type methods, and then concrete types that have
// placeholder children (for example when ::pointer_type is invoked
// on a placeholder type). This routine deals only with the first
// category.

void TypeManager::updatePlaceholderUnderlyingType(Btype *pht,
                                                   Btype *newtyp) {
  assert(placeholders_.find(pht) != placeholders_.end());
  assert(anonTypes_.find(pht) == anonTypes_.end());
  assert(pht->isPlaceholder());

  // make changes
  pht->setType(newtyp->type());
  if (! newtyp->isPlaceholder())
    pht->setPlaceholder(false);

  // Now that this is fully concrete, this may allow us to make
  // other types concrete if they referred to this one.
  if (!pht->isPlaceholder())
    postProcessResolvedPlaceholder(pht);
}

BFunctionType *TypeManager::makeAuxFcnType(llvm::FunctionType *ft)
{
  assert(ft);

  auto it = auxTypeMap_.find(ft);
  if (it != auxTypeMap_.end())
    return it->second->castToBFunctionType();

  Btype *recvTyp = nullptr;
  Btype *rStructTyp = nullptr;
  std::vector<Btype *> params;
  std::vector<Btype *> results;
  if (ft->getReturnType() != llvmVoidType_)
    results.push_back(makeAuxType(ft->getReturnType()));
  for (unsigned ii = 0; ii < ft->getNumParams(); ++ii)
    params.push_back(makeAuxType(ft->getParamType(ii)));
  BFunctionType *rval =
      new BFunctionType(recvTyp, params, results,
                        rStructTyp, ft);
  auxTypeMap_[ft] = rval;
  return rval;
}

Btype *TypeManager::makeAuxType(llvm::Type *lt) {
  assert(lt);

  auto it = auxTypeMap_.find(lt);
  if (it != auxTypeMap_.end())
    return it->second;
  Btype *rval = new Btype(Btype::AuxT, lt);
  auxTypeMap_[lt] = rval;
  return rval;
}

Btype *TypeManager::errorType() { return errorType_; }

Btype *TypeManager::voidType() { return makeAuxType(llvmVoidType_); }

bool TypeManager::isBooleanType(Btype *bt) {
  BIntegerType *bit = bt->castToBIntegerType();
  return (bit && bit->isUnsigned() &&
          bit->type() == llvm::IntegerType::get(context_, 8));
}

Btype *TypeManager::boolType() {
  // LLVM has no predefined boolean type. Use int8 for this purpose.
  return integerType(true, 8);
}

llvm::Type *TypeManager::makeLLVMFloatType(int bits)
{
  llvm::Type *llft = nullptr;
  if (bits == 32)
    llft = llvmFloatType_;
  else if (bits == 64)
    llft = llvmDoubleType_;
  else if (bits == 128)
    llft = llvmLongDoubleType_;
  else
    assert(false && "unsupported float width");
  return llft;
}

// Get an unnamed float type.

Btype *TypeManager::floatType(int bits)
{
  llvm::Type *llft = makeLLVMFloatType(bits);

  // Consult cache
  BFloatType cand(bits, llft);
  auto it = anonTypes_.find(&cand);
  if (it != anonTypes_.end()) {
    Btype *existing = *it;
    BFloatType *bft = existing->castToBFloatType();
    assert(bft);
    return bft;
  }

  // Install in cache
  BFloatType *rval = new BFloatType(bits, llft);
  anonTypes_.insert(rval);
  return rval;
}

// Get an unnamed integer type.
//
// Note that in the LLVM world, we don't have signed/unsigned types,
// we only have signed/unsigned operations (e.g. signed addition of
// two integers).
//
// Many frontends for C-like languages have squishyness when it comes
// to signed/unsigned arithmetic. Example: for the C code
//
//       double abc(unsigned x, int y) { return (double) x + y; }
//
// What typically happens under the hood is that a C compiler constructs
// a parse tree that looks like
//
//                  op: ADDITION
//                 /          \.
//                /            \.
//            var_ref(x)      var_ref(y)
//            typ: unsigned   type: signed
//
// where the ADD op is generic/polymorphic, and the real nature of the
// add (signed/unsigned) only becomes apparent during lowering, when
// the C rules about type conversions are enforced.
//
// To account for any potential hazards here, we record whether the
// frontend has announced that a specific type is unsigned in a side
// table.  We can then use that table later on to enforce the rules
// (for example, to insure that we didn't forget to insert a type
// conversion, or to derive the correct flavor of an integer ADD based
// on its arguments).

Btype *TypeManager::integerType(bool is_unsigned, int bits)
{
  llvm::Type *llit = llvm::IntegerType::get(context_, bits);

  // Check for and return existing anon type if we have one already
  BIntegerType cand(is_unsigned, bits, llit);
  auto it = anonTypes_.find(&cand);
  if (it != anonTypes_.end()) {
    Btype *existing = *it;
    BIntegerType *bit = existing->castToBIntegerType();
    assert(bit);
    return bit;
  }

  // Install in cache
  BIntegerType *rval = new BIntegerType(is_unsigned, bits, llit);
  anonTypes_.insert(rval);
  return rval;
}

llvm::Type *
TypeManager::makeLLVMStructType(const std::vector<Btyped_identifier> &fields) {
  llvm::SmallVector<llvm::Type *, 64> elems(fields.size());
  for (unsigned i = 0; i < fields.size(); ++i)
    elems[i] = fields[i].btype->type();
  llvm::Type *lst = llvm::StructType::get(context_, elems);
  return lst;
}

bool TypeManager::addPlaceholderRefs(Btype *btype)
{
  bool rval = false;
  switch(btype->flavor()) {
    case Btype::ArrayT: {
      BArrayType *bat = btype->castToBArrayType();
      if (bat->elemType()->isUnresolvedPlaceholder()) {
        placeholderRefs_[bat->elemType()].insert(btype);
        rval = true;
      }
      break;
    }
    case Btype::PointerT: {
      BPointerType *bpt = btype->castToBPointerType();
      if (bpt->toType()->isUnresolvedPlaceholder()) {
        placeholderRefs_[bpt->toType()].insert(btype);
        rval = true;
      }
      break;
    }
    case Btype::StructT: {
      BStructType *bst = btype->castToBStructType();
      const std::vector<Backend::Btyped_identifier> &fields = bst->fields();
      for (unsigned i = 0; i < fields.size(); ++i) {
        if (fields[i].btype->isUnresolvedPlaceholder()) {
          placeholderRefs_[fields[i].btype].insert(bst);
          rval = true;
        }
      }
      break;
    }
    default: {
      // nothing to do here
    }
  }
  return rval;
}

// Make a struct type.

Btype *TypeManager::structType(const std::vector<Btyped_identifier> &fields)
{
  // Return error type if any field has error type; look for placeholders
  bool hasPlaceField = false;
  for (unsigned i = 0; i < fields.size(); ++i) {
    if (fields[i].btype == errorType_)
      return errorType_;
    if (fields[i].btype->isUnresolvedPlaceholder())
      hasPlaceField = true;
  }

  BStructType *rval = nullptr;
  llvm::Type *llst = nullptr;
  if (hasPlaceField) {
    // If any of the fields have placeholder type, then we can't
    // create a concrete LLVM type, so manufacture a placeholder
    // instead.
    llst = makeOpaqueLlvmType("IPST");
    rval = new BStructType(fields, llst);
    rval->setPlaceholder(addPlaceholderRefs(rval));
  } else {
    // No placeholder fields -- manufacture the concrete LLVM, then
    // check for and return existing anon type if we have one already.
    llst = makeLLVMStructType(fields);
    BStructType cand(fields, llst);
    auto it = anonTypes_.find(&cand);
    if (it != anonTypes_.end()) {
      Btype *existing = *it;
      assert(existing->castToBStructType());
      return existing;
    }
    rval = new BStructType(fields, llst);
  }

  if (traceLevel() > 1) {
    std::cerr << "\n^ struct type "
              << ((void*)rval) << " [llvm type "
              << ((void*)llst) << "] fields:\n";
    for (unsigned i = 0; i < fields.size(); ++i)
      std::cerr << i << ": " << ((void*)fields[i].btype) << "\n";
    rval->dump();
  }

  // Done
  anonTypes_.insert(rval);
  return rval;
}

llvm::Type *TypeManager::makeOpaqueLlvmType(const char *tag) {
  std::string tname(tnamegen(tag));
  return llvm::StructType::create(context_, tname);
}

// Create a placeholder for a struct type.
Btype *TypeManager::placeholderStructType(const std::string &name,
                                             Location location)
{
  BStructType *pst = new BStructType(name);
  llvm::Type *opaque = makeOpaqueLlvmType("PST");
  pst->setType(opaque);
  placeholders_.insert(pst);
  return pst;
}

// LLVM has no such thing as a complex type -- it expects the front
// end to lower all complex operations from the get-go, meaning
// that the back end only sees two-element structs.

Btype *TypeManager::complexType(int bits) {
  if (bits == 64 && complexFloatType_)
    return complexFloatType_;
  if (bits == 128 && complexDoubleType_)
    return complexDoubleType_;
  assert(bits == 64 || bits == 128);
  llvm::Type *elemTy = (bits == 64 ? llvm::Type::getFloatTy(context_)
                                   : llvm::Type::getDoubleTy(context_));
  llvm::SmallVector<llvm::Type *, 2> elems(2);
  elems[0] = elemTy;
  elems[1] = elemTy;
  Btype *rval = makeAuxType(llvm::StructType::get(context_, elems));
  if (bits == 64)
    complexFloatType_ = rval;
  else
    complexDoubleType_ = rval;
  return rval;
}

// Get a pointer type.

Btype *TypeManager::pointerType(Btype *toType)
{
  if (toType == errorType_)
    return errorType_;

  // Manufacture corresponding LLVM type. Note that LLVM does not
  // allow creation of a "pointer to void" type -- model this instead
  // as pointer to char.
  llvm::Type *lltot =
      (toType->type() == llvmVoidType_ ? llvmInt8Type_ : toType->type());
  llvm::Type *llpt = llvm::PointerType::get(lltot, addressSpace_);

  // Check for and return existing anon type if we have one already
  BPointerType cand(toType, llpt);
  auto it = anonTypes_.find(&cand);
  if (it != anonTypes_.end()) {
    Btype *existing = *it;
    BPointerType *bpt = existing->castToBPointerType();
    assert(bpt);
    return bpt;
  }

  // Create new type
  BPointerType *rval = new BPointerType(toType, llpt);
  rval->setPlaceholder(addPlaceholderRefs(rval));
  if (traceLevel() > 1) {
    std::cerr << "\n^ pointer type "
              << ((void*)rval) << " [llvm type "
              << ((void*) llpt) << "]\n";
    rval->dump();
  }

  // Install in cache
  anonTypes_.insert(rval);
  return rval;
}

// LLVM doesn't directly support placeholder types other than opaque
// structs, so the general strategy for placeholders is to create an
// opaque struct (corresponding to the thing being pointed to) and
// then make a pointer to it. Since LLVM allows only a single opaque
// struct type with a given name within a given context, we capture
// the provided name but we don't try to hand that specific name to
// LLVM to use for the identified struct (so as to avoid collisions).

// Create a placeholder for a pointer type.

Btype *TypeManager::placeholderPointerType(const std::string &name,
                                           Location location, bool forfunc)
{
  Btype *ppt = new BPointerType(name);
  llvm::Type *opaque = makeOpaqueLlvmType("PPT");
  llvm::PointerType *pto = llvm::PointerType::get(opaque, addressSpace_);
  ppt->setType(pto);
  placeholders_.insert(ppt);
  return ppt;
}

llvm::Type *
TypeManager::makeLLVMFunctionType(Btype *receiverType,
                                   const std::vector<Btype *> &paramTypes,
                                   const std::vector<Btype *> &resultTypes,
                                   Btype *rbtype)
{
  llvm::SmallVector<llvm::Type *, 4> elems(0);

  // Receiver type if applicable
  if (receiverType != nullptr)
    elems.push_back(receiverType->type());

  // Argument types
  for (auto pt : paramTypes)
    elems.push_back(pt->type());

  llvm::Type *rtyp = rbtype->type();

  // https://gcc.gnu.org/PR72814 handling. From the go-gcc.cc
  // equivalent, here is an explanatory comment:
  //
  // The libffi library can not represent a zero-sized object.  To
  // avoid causing confusion on 32-bit SPARC, we treat a function that
  // returns a zero-sized value as returning void.  That should do no
  // harm since there is no actual value to be returned.
  //
  if (rtyp->isSized() && datalayout_->getTypeSizeInBits(rtyp) == 0)
    rtyp = llvm::Type::getVoidTy(context_);

  // from LLVM's perspective, no functions have varargs (all that
  // is dealt with by the front end).
  const bool isVarargs = false;
  llvm::FunctionType *llft = llvm::FunctionType::get(rtyp, elems, isVarargs);
  return llft;
}

// Make a function type.

Btype *
TypeManager::functionType(const Btyped_identifier &receiver,
                           const std::vector<Btyped_identifier> &parameters,
                            const std::vector<Btyped_identifier> &results,
                            Btype *result_struct, Location) {

  // Vett the parameters and results
  std::vector<Btype *> paramTypes;
  for (auto p : parameters) {
    if (p.btype == errorType_)
      return errorType_;
    paramTypes.push_back(p.btype);
  }
  std::vector<Btype *> resultTypes;
  for (auto r : results) {
    if (r.btype == errorType_)
      return errorType_;
    resultTypes.push_back(r.btype);
  }
  if (result_struct && result_struct == errorType_)
    return errorType_;
  if (receiver.btype && receiver.btype == errorType_)
    return errorType_;

  // Determine result Btype
  Btype *rbtype = nullptr;
  if (results.empty())
    rbtype = makeAuxType(llvm::Type::getVoidTy(context_));
  else if (results.size() == 1) {
    rbtype = results.front().btype;
  } else {
    assert(result_struct != nullptr);
    rbtype = result_struct;
  }
  assert(rbtype != nullptr);

  llvm::Type *llft = makeLLVMFunctionType(receiver.btype, paramTypes,
                                          resultTypes, rbtype);

  // Consult cache
  BFunctionType cand(receiver.btype, paramTypes, resultTypes, rbtype, llft);
  auto it = anonTypes_.find(&cand);
  if (it != anonTypes_.end()) {
    Btype *existing = *it;
    BFunctionType *bft = existing->castToBFunctionType();
    assert(bft);
    return bft;
  }

  // Manufacture new BFunctionType to return
  BFunctionType *rval = new BFunctionType(receiver.btype,
                                          paramTypes, resultTypes,
                                          rbtype, llft);

  // Do some book-keeping
  bool isPlace = false;
  if (result_struct && result_struct->isUnresolvedPlaceholder()) {
    placeholderRefs_[result_struct].insert(rval);
    isPlace = true;
  }
  if (receiver.btype && receiver.btype->isUnresolvedPlaceholder()) {
    placeholderRefs_[receiver.btype].insert(rval);
    isPlace = true;
  }
  for (auto p : paramTypes) {
    if (p->isUnresolvedPlaceholder()) {
      placeholderRefs_[p].insert(rval);
      isPlace = true;
    }
  }
  for (auto r : resultTypes) {
    if (r->isUnresolvedPlaceholder()) {
      placeholderRefs_[r].insert(rval);
      isPlace = true;
    }
  }
  if (isPlace)
    rval->setPlaceholder(true);

  // Install in cache and return
  anonTypes_.insert(rval);
  return rval;
}

Btype *TypeManager::arrayType(Btype *elemType, Bexpression *length)
{
  if (length == errorExpression_ || elemType == errorType_)
    return errorType_;

  // The length expression provided needs to be immediately available
  assert(length->value());
  assert(llvm::isa<llvm::ConstantInt>(length->value()));

  // Manufacture corresponding LLVM type
  llvm::ConstantInt *lc = llvm::cast<llvm::ConstantInt>(length->value());
  uint64_t asize = lc->getValue().getZExtValue();
  llvm::Type *llat = llvm::ArrayType::get(elemType->type(), asize);

  // Check for and return existing anon type if we have one already
  BArrayType cand(elemType, length, llat);
  auto it = anonTypes_.find(&cand);
  if (it != anonTypes_.end()) {
    Btype *existing = *it;
    BArrayType *bat = existing->castToBArrayType();
    assert(bat);
    return bat;
  }

  // Create appropriate Btype
  BArrayType *rval = new BArrayType(elemType, length, llat);
  rval->setPlaceholder(addPlaceholderRefs(rval));

  if (traceLevel() > 1) {
    std::cerr << "\n^ array type "
              << ((void*)rval) << " [llvm type "
              << ((void*) llat) << "] sz="
              << asize << " elementType:";
    std::cerr << ((void*) elemType) << "\n";
    rval->dump();
  }

  // Install in cache and return
  anonTypes_.insert(rval);
  return rval;
}

// Create a placeholder for an array type.

Btype *TypeManager::placeholderArrayType(const std::string &name,
                                            Location location)
{
  Btype *pat = new BArrayType(name);
  llvm::Type *opaque = makeOpaqueLlvmType("PAT");
  pat->setType(opaque);
  placeholders_.insert(pat);
  return pat;
}

Btype *TypeManager::functionReturnType(Btype *typ)
{
  assert(typ);
  assert(typ != errorType_);
  BPointerType *pt = typ->castToBPointerType();
  if (pt)
    typ = pt->toType();
  BFunctionType *ft = typ->castToBFunctionType();
  assert(ft);
  return ft->resultType();
}

Btype *TypeManager::elementTypeByIndex(Btype *btype, unsigned fieldIndex)
{
  assert(btype);
  assert(btype != errorType_);
  BStructType *st = btype->castToBStructType();
  if (st)
    return st->fieldType(fieldIndex);
  BArrayType *at = btype->castToBArrayType();
  assert(at);
  return at->elemType();
}

void TypeManager::postProcessResolvedPointerPlaceholder(BPointerType *bpt,
                                                         Btype *btype)
{
  assert(bpt);
  assert(bpt->isPlaceholder());
  llvm::Type *newllpt = llvm::PointerType::get(btype->type(), addressSpace_);

  // pluck out of anonTypes if stored there
  bool wasHashed = removeAnonType(bpt);

  bpt->setType(newllpt);
  if (traceLevel() > 1) {
    std::cerr << "\n^ resolving placeholder pointer type "
              << ((void*)bpt) << " to concrete target type; "
              << "new LLVM type "
              << ((void*) newllpt) << ", resolved type now:\n";
    bpt->dump();
  }
  bpt->setPlaceholder(false);

  // put back into anonTypes if it was there previously
  if (wasHashed)
    reinstallAnonType(bpt);

  postProcessResolvedPlaceholder(bpt);
}

void TypeManager::postProcessResolvedStructPlaceholder(BStructType *bst,
                                                        Btype *btype)
{
  assert(bst);
  assert(bst->isPlaceholder());

  std::vector<Btyped_identifier> fields(bst->fields());
  bool hasPl = false;
  for (unsigned i = 0; i < fields.size(); ++i) {
    const Btype *ft = fields[i].btype;
    if (btype == ft) {
      if (traceLevel() > 1) {
        std::cerr << "\n^ resolving field " << i << " of "
                  << "placeholder struct type "
                  << ((void*)bst) << " to concrete field type\n";
      }
    } else if (fields[i].btype->isUnresolvedPlaceholder()) {
      hasPl = true;
    }
  }
  if (! hasPl) {

    // No need to unhash the type -- we can simple update it in place.
    llvm::SmallVector<llvm::Type *, 64> elems(fields.size());
    for (unsigned i = 0; i < fields.size(); ++i)
      elems[i] = fields[i].btype->type();
    llvm::StructType *llst = llvm::cast<llvm::StructType>(bst->type());
    llst->setBody(elems);
    bst->setPlaceholder(false);

    if (traceLevel() > 1) {
      std::cerr << "\n^ resolving placeholder struct type "
                << ((void*)bst) << " to concrete struct type:\n";
      bst->dump();
    }

    postProcessResolvedPlaceholder(bst);
  }
}

void TypeManager::postProcessResolvedArrayPlaceholder(BArrayType *bat,
                                                       Btype *btype)
{
  assert(bat);
  assert(bat->isPlaceholder());

  // Create new resolved LLVM array type.
  uint64_t asize = bat->nelSize();
  llvm::Type *newllat = llvm::ArrayType::get(btype->type(), asize);

  // pluck out of anonTypes if stored there
  bool wasHashed = removeAnonType(bat);

  // update
  bat->setType(newllat);
  if (traceLevel() > 1) {
    std::cerr << "\n^ resolving placeholder array type "
              << ((void*)bat) << " elem type; "
              << "new LLVM type "
              << ((void*) newllat) << ", resolved type now:\n";
    bat->dump();
  }
  bat->setPlaceholder(false);

  // put back into anonTypes if it was there previously
  if (wasHashed)
    reinstallAnonType(bat);

  postProcessResolvedPlaceholder(bat);
}

// When one of the "set_placeholder_*_type()" methods is called to
// resolve a placeholder type PT to a concrete type CT, we then need
// to chase down other types that refer to PT. For example, there
// might be a separate struct type ST that has a field with type ST --
// if this is ST's only placeholder field, then when PT is
// "concretized" we can also "concretize" ST. This helper manages this
// process.  It is worth noting that the types that we're updating may
// be installed in the anonTypes_ table, meaning that to make any
// changes we need to unhash it (remove it from the table), then apply
// the changes, then add it back into the table. During the addition,
// the new type may collide with some other existing type in the
// table. If this happens, we record the type in the separate
// placeholders_ set, so as to make sure we can delete it at the end
// of the compilation (this is managed by reinstallAnonType).
//
void TypeManager::postProcessResolvedPlaceholder(Btype *btype)
{
  auto it = placeholderRefs_.find(btype);
  if (it == placeholderRefs_.end())
    return;

  for (auto refType : it->second) {

    if (!refType->isPlaceholder())
      continue;

    BPointerType *bpt = refType->castToBPointerType();
    if (bpt) {
      postProcessResolvedPointerPlaceholder(bpt, btype);
      continue;
    }

    BArrayType *bat = refType->castToBArrayType();
    if (bat) {
      postProcessResolvedArrayPlaceholder(bat, btype);
      continue;
    }

    BStructType *bst = refType->castToBStructType();
    if (bst) {
      postProcessResolvedStructPlaceholder(bst, btype);
      continue;
    }

    // Should never get here
    assert(false);
  }
}

// Set the real target type for a placeholder pointer type.

// NB: for reasons that are unclear to me, the front end sometimes
// calls this more than once on the same type -- this doesn't necessarily
// seem like a horrible thing, just kind of curious.

bool TypeManager::setPlaceholderPointerType(Btype *placeholder,
                                             Btype *to_type)
{
  assert(placeholder);
  assert(to_type);
  if (placeholder == errorType_ || to_type == errorType_)
    return false;
  assert(to_type->type()->isPointerTy());
  assert(anonTypes_.find(placeholder) == anonTypes_.end());
  assert(placeholders_.find(placeholder) != placeholders_.end());

  if (traceLevel() > 1) {
    std::cerr << "\n^ placeholder pointer "
              << ((void*)placeholder) << " [llvm type "
              << ((void*) placeholder->type()) << "]"
              << " redirected to " << ((void*) to_type)
              << " [llvm type " << ((void*) to_type->type())
              << "]\n";
    std::cerr << "placeholder: "; placeholder->dump();
    std::cerr << "redir: "; to_type->dump();
  }

  // Update the target type for the pointer
  placeholder->setType(to_type->type());

  // Decide what to do next.
  if (to_type->isUnresolvedPlaceholder()) {
    // We're redirecting this type to another placeholder -- delay the
    // creation of the final LLVM type and record the reference.
    placeholderRefs_[to_type].insert(placeholder);
  } else {
    // The target is a concrete type. Reset the placeholder flag on
    // this type, then call a helper to update any other types that
    // might refer to this one.
    placeholder->setPlaceholder(false);
    postProcessResolvedPlaceholder(placeholder);
  }

  // Circular type handling

  unsigned nct = circularPointerLoop_.size();
  if (nct != 0) {
    btpair p = circularPointerLoop_[0];
    Btype *cplTT = p.second;
    if (to_type == cplTT) {
      // We've come to the end of a cycle of circular types (in the
      // sense that we're back visiting the placeholder that initiated
      // the loop). Record proper values for inserting conversions when
      // we have operations on this this (load, addr).
      btpair adPair = circularPointerLoop_[nct == 1 ? 0 : 1];
      Btype *adConvPH = adPair.first;
      Btype *adConvTT = adPair.second;
      btpair loadPair = circularPointerLoop_.back();
      Btype *loadConvTT = loadPair.second;
      circularConversionLoadMap_[to_type] = loadConvTT;
      circularConversionLoadMap_[placeholder] = loadConvTT;
      circularConversionAddrMap_[adConvPH] = to_type;
      circularConversionAddrMap_[adConvTT] = to_type;

      if (traceLevel() > 1) {
        std::cerr << "\n^ finished circular type loop of size " << nct
                  << ": [" << ((void*) loadConvTT)
                  << "," << ((void*) adConvPH)
                  << "," << ((void*) adConvTT)
                  << "] for to_type: [load, addrPH, addrTT]:\n";
        loadConvTT->dump();
        adConvPH->dump();
        adConvTT->dump();
      }
      circularPointerLoop_.clear();
    } else {
      circularPointerLoop_.push_back(std::make_pair(placeholder, to_type));
    }
  }

  return true;
}

// Set the real values for a placeholder function type.

bool TypeManager::setPlaceholderFunctionType(Btype *placeholder,
                                              Btype *ft) {
  return setPlaceholderPointerType(placeholder, ft);
}

// Fill in the fields of a placeholder struct type.

bool
TypeManager::setPlaceholderStructType(Btype *placeholder,
                             const std::vector<Btyped_identifier> &fields)
{
  if (placeholder == errorType_)
    return false;

  assert(anonTypes_.find(placeholder) == anonTypes_.end());
  assert(placeholders_.find(placeholder) != placeholders_.end());
  BStructType *phst = placeholder->castToBStructType();
  assert(phst);
  phst->setFields(fields);

  // If we still have fields with placeholder types, then we still can't
  // manufacture a concrete LLVM type. If no placeholders, then we can
  // materialize the final LLVM type using a setBody call.
  bool isplace = addPlaceholderRefs(phst);
  if (! isplace) {
    llvm::StructType *llst = llvm::cast<llvm::StructType>(phst->type());
    llvm::SmallVector<llvm::Type *, 8> fieldtypes(fields.size());
    for (unsigned idx = 0; idx < fields.size(); ++idx)
      fieldtypes[idx] = fields[idx].btype->type();
    llst->setBody(fieldtypes);
    phst->setPlaceholder(false);
  }

  if (traceLevel() > 1) {
    std::cerr << "\n^ placeholder struct "
              << ((void*)placeholder) << " [llvm type "
              << ((void*) placeholder->type())
              << "] body redirected:\n";
    std::cerr << "placeholder: "; placeholder->dump();
    std::cerr << "fields:\n";
    for (unsigned i = 0; i < fields.size(); ++i) {
      std::cerr << i << ": ";
      fields[i].btype->dump();
    }
  }

  if (! isplace)
    postProcessResolvedPlaceholder(phst);

  return true;
}

// Fill in the components of a placeholder array type.

bool TypeManager::setPlaceholderArrayType(Btype *placeholder,
                                           Btype *element_btype,
                                           Bexpression *length) {
  if (placeholder == errorType_ || element_btype == errorType_ ||
      length == errorExpression_)
    return false;

  assert(anonTypes_.find(placeholder) == anonTypes_.end());
  assert(placeholders_.find(placeholder) != placeholders_.end());

  BArrayType *phat = placeholder->castToBArrayType();
  assert(phat);
  phat->setElemType(element_btype);
  phat->setNelements(length);
  bool isplace = addPlaceholderRefs(phat);

  uint64_t asize = phat->nelSize();
  llvm::Type *newllat = llvm::ArrayType::get(element_btype->type(), asize);
  Btype *atype = makeAuxType(newllat);
  atype->setPlaceholder(isplace);

  if (traceLevel() > 1) {
    std::string ls;
    llvm::raw_string_ostream os(ls);
    length->osdump(os);
    std::cerr << "\n^ placeholder array "
              << ((void*)placeholder) << " [llvm type "
              << ((void*) placeholder->type())
              << "] element type updated to " << ((void*) element_btype)
              << " [llvm type " << ((void*) element_btype->type())
              << "] length: " << ls
              << "\n";
    std::cerr << "placeholder: "; placeholder->dump();
    std::cerr << "redir: "; atype->dump();
  }

  updatePlaceholderUnderlyingType(placeholder, atype);

  return true;
}

// Return a named version of a type.

Btype *TypeManager::namedType(const std::string &name,
                               Btype *btype,
                               Location location)
{
  // TODO: add support for debug metadata

  Btype *rval = btype->clone();
  addPlaceholderRefs(rval);
  rval->setName(name);
  namedTypes_.insert(rval);

  if (traceLevel() > 1) {
    std::cerr << "\n^ named type '" << name << "' "
              << ((void*)rval) << " [llvm type "
              << ((void*) rval->type()) << "]\n";
    rval->dump();
  }

  return rval;
}

// Return a pointer type used as a marker for a circular type.

// Consider the following Go code:
//
//      type s *p
//      type p *q
//      type q *r
//      type r *p
//
// Here we have a cycle involving p/q/r, plus another type "p" that
// points into the cycle. When the front end detects the cycle it will
// flag "p" as circular, meaning that circular_pointer_type will
// wind up being invoked twice (once for real due to the cycle and
// once due to the jump into the cycle). We use a map to detect
// the second instance (e.g. "s") so that we can just return whatever
// we created before.

Btype *TypeManager::circularPointerType(Btype *placeholder, bool isfunc) {
  assert(placeholder);
  if (placeholder == errorType_)
    return errorType_;

  // Front end call this multiple times on the same placeholder, so we
  // cache markers and return the cached value on successive lookups.
  auto it = circularPointerTypeMap_.find(placeholder);
  if (it != circularPointerTypeMap_.end())
    return it->second;

  llvm::Type *opaque = makeOpaqueLlvmType("CPT");
  llvm::Type *circ_ptr_typ = llvm::PointerType::get(opaque, addressSpace_);
  Btype *rval = makeAuxType(circ_ptr_typ);
  circularPointerTypes_.insert(circ_ptr_typ);
  circularPointerTypeMap_[placeholder] = rval;
  assert(circularPointerLoop_.size() == 0);
  circularPointerLoop_.push_back(std::make_pair(placeholder, rval));

  if (traceLevel() > 1) {
    std::cerr << "\n^ circular_pointer_type "
              << "for placeholder " << ((void *)placeholder)
              << " [llvm type " << ((void *)placeholder->type())
              << "] returned type is " << ((void *)rval) << " [llvm type "
              << ((void *)rval->type()) << "]\n";
    placeholder->dump();
    rval->dump();
  }

  return rval;
}

// Return whether we might be looking at a circular type.

bool TypeManager::isCircularPointerType(Btype *btype) {
  assert(btype);
  return isCircularPointerType(btype->type());
}

bool TypeManager::isCircularPointerType(llvm::Type *typ) {
  assert(typ);
  auto it = circularPointerTypes_.find(typ);
  return it != circularPointerTypes_.end();
}

Btype *TypeManager::circularTypeLoadConversion(Btype *typ) {
  auto it = circularConversionLoadMap_.find(typ);
  return it != circularConversionLoadMap_.end()  ? it->second : nullptr;
}

Btype *TypeManager::circularTypeAddrConversion(Btype *typ) {
  auto it = circularConversionAddrMap_.find(typ);
  if (it != circularConversionAddrMap_.end())
    return it->second;
  return nullptr;
}

// Return the size of a type.

int64_t TypeManager::typeSize(Btype *btype) {
  if (btype == errorType_)
    return 1;
  uint64_t uval = datalayout_->getTypeSizeInBits(btype->type());
  assert((uval & 0x7) == 0);
  uval /= 8;
  return static_cast<int64_t>(uval);
}

// Return the alignment of a type.

int64_t TypeManager::typeAlignment(Btype *btype) {
  if (btype == errorType_)
    return 1;
  unsigned uval = datalayout_->getPrefTypeAlignment(btype->type());
  return static_cast<int64_t>(uval);
}

// Return the alignment of a struct field of type BTYPE.
//
// One case where type_field_align(X) != type_align(X) is
// for type 'double' on x86 32-bit, where for compatibility
// a double field is 4-byte aligned but will be 8-byte aligned
// otherwise.

int64_t TypeManager::typeFieldAlignment(Btype *btype) {
  // Corner cases.
  if (!btype->type()->isSized() || btype == errorType_)
    return -1;

  // Create a new anonymous struct with two fields: first field is a
  // single byte, second field is of type btype. Then use
  // getElementOffset to find out where the second one has been
  // placed. Finally, return min of alignof(btype) and that value.

  llvm::SmallVector<llvm::Type *, 2> elems(2);
  elems[0] = llvm::Type::getInt1Ty(context_);
  elems[1] = btype->type();
  llvm::StructType *dummyst = llvm::StructType::get(context_, elems);
  const llvm::StructLayout *sl = datalayout_->getStructLayout(dummyst);
  uint64_t uoff = sl->getElementOffset(1);
  unsigned talign = datalayout_->getPrefTypeAlignment(btype->type());
  int64_t rval = (uoff < talign ? uoff : talign);
  return rval;
}

// Return the offset of a field in a struct.

int64_t TypeManager::typeFieldOffset(Btype *btype, size_t index) {
  if (btype == errorType_)
    return 0;
  assert(btype->type()->isStructTy());
  llvm::StructType *llvm_st = llvm::cast<llvm::StructType>(btype->type());
  const llvm::StructLayout *sl = datalayout_->getStructLayout(llvm_st);
  uint64_t uoff = sl->getElementOffset(index);
  return static_cast<int64_t>(uoff);
}

bool TypeManager::isFuncDescriptorType(llvm::Type *typ)
{
  if (! typ->isStructTy())
    return false;
  llvm::StructType *st = llvm::cast<llvm::StructType>(typ);
  if (st->getNumElements() != 1)
    return false;
  if (st->getElementType(0) != llvmIntegerType_ &&
      !st->getElementType(0)->isFunctionTy())
    return false;
  return true;
}

bool TypeManager::isPtrToFuncDescriptorType(llvm::Type *typ)
{
  if (! typ->isPointerTy())
    return false;
  llvm::PointerType *pt = llvm::cast<llvm::PointerType>(typ);
  return isFuncDescriptorType(pt->getElementType());
}

bool TypeManager::isPtrToFuncType(llvm::Type *typ)
{
  if (! typ->isPointerTy())
    return false;
  llvm::PointerType *pt = llvm::cast<llvm::PointerType>(typ);
  return pt->getElementType()->isFunctionTy();
}
