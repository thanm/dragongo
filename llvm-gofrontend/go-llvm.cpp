//===-- go-llvm.cpp - LLVM implementation of 'Backend'  -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for class Llvm_backend, a subclass of the gofrontend class
// Backend, with LLVM-specific implementation methods.
//
//===----------------------------------------------------------------------===//

#include "go-llvm.h"
#include "backend.h"
#include "go-c.h"
#include "go-system.h"
#include "go-llvm-linemap.h"
#include "gogo.h"

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"

// Generic "no insert" builder
typedef llvm::IRBuilder<> LIRBuilder;

class BexprInserter {
 public:
  BexprInserter() : expr_(nullptr) { }
  void setDestExpr(Bexpression *expr) { assert(!expr_); expr_ = expr; }

  void InsertHelper(llvm::Instruction *I, const llvm::Twine &Name,
                    llvm::BasicBlock *BB,
                    llvm::BasicBlock::iterator InsertPt) const {
    assert(expr_);
    expr_->appendInstruction(I);
    I->setName(Name);
  }

 private:
    mutable Bexpression *expr_;
};

// Builder that appends to a specified Bexpression
class BexprLIRBuilder :
    public llvm::IRBuilder<llvm::ConstantFolder, BexprInserter> {
  typedef llvm::IRBuilder<llvm::ConstantFolder, BexprInserter> IRBuilderBase;
 public:
  BexprLIRBuilder(llvm::LLVMContext &context, Bexpression *expr) :
      IRBuilderBase(context, llvm::ConstantFolder()) {
    setDestExpr(expr);
  }
};

#define CHKTREE(x) if (checkIntegrity_ && traceLevel()) \
      enforceTreeIntegrity(x)

static const auto NotInTargetLib = llvm::LibFunc::NumLibFuncs;

Llvm_backend::Llvm_backend(llvm::LLVMContext &context,
                           Linemap *linemap)
    : context_(context)
    , module_(new llvm::Module("gomodule", context))
    , datalayout_(module_->getDataLayout())
    , linemap_(linemap)
    , addressSpace_(0)
    , traceLevel_(0)
    , checkIntegrity_(true)
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
    , TLI_(nullptr)
    , errorFunction_(nullptr)
{
  // LLVM doesn't have anything that corresponds directly to the
  // gofrontend notion of an error type. For now we create a so-called
  // 'identified' anonymous struct type and have that act as a
  // stand-in. See http://llvm.org/docs/LangRef.html#structure-type
  errorType_ = makeAuxType(llvm::StructType::create(context_, "$Error$"));

  // If nobody passed in a linemap, create one for internal use.
  if (!linemap_) {
    ownLinemap_.reset(go_get_linemap());
    linemap_ = ownLinemap_.get();
  }

  // For builtin creation
  llvmPtrType_ =
      llvm::PointerType::get(llvm::StructType::create(context), addressSpace_);

  // Assorted pre-computer types for use in builtin function creation
  llvmVoidType_ = llvm::Type::getVoidTy(context_);
  llvmBoolType_ = llvm::IntegerType::get(context_, 1);
  llvmIntegerType_ =
      llvm::IntegerType::get(context_, datalayout_.getPointerSizeInBits());
  llvmSizeType_ = llvmIntegerType_;
  llvmInt8Type_ = llvm::IntegerType::get(context_, 8);
  llvmInt32Type_ = llvm::IntegerType::get(context_, 32);
  llvmInt64Type_ = llvm::IntegerType::get(context_, 64);
  llvmFloatType_ = llvm::Type::getFloatTy(context_);
  llvmDoubleType_ = llvm::Type::getDoubleTy(context_);
  llvmLongDoubleType_ = llvm::Type::getFP128Ty(context_);

  // Predefined C string type
  stringType_ = pointer_type(integer_type(true, 8));

  // Create and record an error function. By marking it as varargs this will
  // avoid any collisions with things that the front end might create, since
  // Go varargs is handled/lowered entirely by the front end.
  llvm::SmallVector<llvm::Type *, 1> elems(0);
  elems.push_back(errorType_->type());
  const bool isVarargs = true;
  llvm::FunctionType *eft = llvm::FunctionType::get(
      llvm::Type::getVoidTy(context_), elems, isVarargs);
  llvm::GlobalValue::LinkageTypes plinkage = llvm::GlobalValue::ExternalLinkage;
  llvm::Function *ef = llvm::Function::Create(eft, plinkage, "",
                                              module_.get());
  errorFunction_.reset(new Bfunction(ef, makeAuxFcnType(eft), ""));

  // Reuse the error function as the value for error_expression
  errorExpression_.reset(
      new Bexpression(errorFunction_->function(), errorType_, Location()));

  // Error statement
  errorStatement_.reset(stmtFromExpr(nullptr, errorExpression_.get()));

  // Reuse the error function as the value for errorVariable_
  errorVariable_.reset(
      new Bvariable(errorType_, Location(), "", ErrorVar, false, nullptr));

  defineAllBuiltins();
}

Llvm_backend::~Llvm_backend() {
  for (auto &expr : expressions_)
    delete expr;
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
  for (auto &kv : valueExprmap_)
    delete kv.second;
  for (auto &kv : valueVarMap_)
    delete kv.second;
  for (auto &kv : builtinMap_)
    delete kv.second;
  for (auto &bfcn : functions_)
    delete bfcn;
}

ExprListStatement *Llvm_backend::stmtFromExpr(Bfunction *function,
                                              Bexpression *expr) {
  ExprListStatement *st = new ExprListStatement(function);
  st->appendExpression(expr);
  CHKTREE(st);
  return st;
}

void
Llvm_backend::verifyModule()
{
  bool broken = llvm::verifyModule(*module_.get(), &llvm::dbgs());
  assert(!broken && "Module not well-formed.");
}

void
Llvm_backend::dumpModule()
{
  module_->dump();
}

void Llvm_backend::dumpExpr(Bexpression *e)
{
  if (e)
    e->srcDump(linemap_);
}

void Llvm_backend::dumpStmt(Bstatement *s)
{
  if (s)
    s->srcDump(linemap_);
}

void Llvm_backend::dumpVar(Bvariable *v)
{
  if (v)
    v->srcDump(linemap_);
}

std::pair<bool, std::string>
Llvm_backend::checkTreeIntegrity(Bexpression *e, bool includePointers)
{
  IntegrityVisitor iv(this, includePointers);
  bool rval = iv.visit(e);
  return std::make_pair(rval, iv.msg());
}

std::pair<bool, std::string>
Llvm_backend::checkTreeIntegrity(Bstatement *s, bool includePointers)
{
  IntegrityVisitor iv(this, includePointers);
  bool rval = iv.visit(s);
  return std::make_pair(rval, iv.msg());
}

void Llvm_backend::enforceTreeIntegrity(Bexpression *e)
{
  IntegrityVisitor iv(this, true);
  if (! iv.visit(e)) {
    std::cerr << iv.msg() << "\n";
    assert(false);
  }
}

void Llvm_backend::enforceTreeIntegrity(Bstatement *s)
{
  IntegrityVisitor iv(this, true);
  if (! iv.visit(s)) {
    std::cerr << iv.msg() << "\n";
    assert(false);
  }
}

std::string Llvm_backend::namegen(const std::string &tag, unsigned expl) {
  auto it = nametags_.find(tag);
  unsigned count = 0;
  if (it != nametags_.end())
    count = it->second + 1;
  if (expl != 0xffffffff)
    count = expl;
  std::stringstream ss;
  ss << tag << "." << count;
  if (expl == 0xffffffff)
    nametags_[tag] = count;
  return ss.str();
}

bool Llvm_backend::removeAnonType(Btype *typ)
{
  auto it = anonTypes_.find(typ);
  if (it != anonTypes_.end()) {
    anonTypes_.erase(it);
    return true;
  }
  return false;
}

void Llvm_backend::reinstallAnonType(Btype *typ)
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

void Llvm_backend::updatePlaceholderUnderlyingType(Btype *pht,
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

BFunctionType *Llvm_backend::makeAuxFcnType(llvm::FunctionType *ft)
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

Btype *Llvm_backend::makeAuxType(llvm::Type *lt) {
  assert(lt);

  auto it = auxTypeMap_.find(lt);
  if (it != auxTypeMap_.end())
    return it->second;
  Btype *rval = new Btype(Btype::AuxT, lt);
  auxTypeMap_[lt] = rval;
  return rval;
}


Btype *Llvm_backend::error_type() { return errorType_; }

Btype *Llvm_backend::void_type() { return makeAuxType(llvmVoidType_); }

Btype *Llvm_backend::bool_type() {
  // LLVM has no predefined boolean type. Use int8 for this purpose.
  return integer_type(true, 8);
}

llvm::Type *Llvm_backend::makeLLVMFloatType(int bits)
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

Btype *Llvm_backend::float_type(int bits)
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

Btype *Llvm_backend::integer_type(bool is_unsigned, int bits)
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
Llvm_backend::makeLLVMStructType(const std::vector<Btyped_identifier> &fields) {
  llvm::SmallVector<llvm::Type *, 64> elems(fields.size());
  for (unsigned i = 0; i < fields.size(); ++i)
    elems[i] = fields[i].btype->type();
  llvm::Type *lst = llvm::StructType::get(context_, elems);
  return lst;
}

bool Llvm_backend::addPlaceholderRefs(Btype *btype)
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

Btype *Llvm_backend::struct_type(const std::vector<Btyped_identifier> &fields)
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

llvm::Type *Llvm_backend::makeOpaqueLlvmType(const char *tag) {
  std::string tname(namegen(tag));
  return llvm::StructType::create(context_, tname);
}

// Create a placeholder for a struct type.
Btype *Llvm_backend::placeholder_struct_type(const std::string &name,
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

Btype *Llvm_backend::complex_type(int bits) {
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

Btype *Llvm_backend::pointer_type(Btype *toType)
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
// opaque struct (corresponding to the thing being pointed to) and then
// make a pointer to it. Since LLVM allows only a single opaque struct
// type with a given name within a given context, we capture the provided
// name but we don't try to hand that specific name to LLVM to use
// for the identified struct (so as to avoid collisions).

// Create a placeholder for a pointer type.

Btype *Llvm_backend::placeholder_pointer_type(const std::string &name,
                                              Location location, bool)
{
  Btype *ppt = new BPointerType(name);
  llvm::Type *opaque = makeOpaqueLlvmType("PPT");
  llvm::PointerType *pto = llvm::PointerType::get(opaque, addressSpace_);
  ppt->setType(pto);
  placeholders_.insert(ppt);
  return ppt;
}

llvm::Type *
Llvm_backend::makeLLVMFunctionType(Btype *receiverType,
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
  if (rtyp->isSized() && datalayout_.getTypeSizeInBits(rtyp) == 0)
    rtyp = llvm::Type::getVoidTy(context_);

  // from LLVM's perspective, no functions have varargs (all that
  // is dealt with by the front end).
  const bool isVarargs = false;
  llvm::FunctionType *llft = llvm::FunctionType::get(rtyp, elems, isVarargs);
  return llft;
}

// Make a function type.

Btype *
Llvm_backend::function_type(const Btyped_identifier &receiver,
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

Btype *Llvm_backend::array_type(Btype *elemType, Bexpression *length)
{
  if (length == errorExpression_.get() || elemType == errorType_)
    return errorType_;

  // Manufacture corresponding LLVM type
  llvm::ConstantInt *lc = llvm::dyn_cast<llvm::ConstantInt>(length->value());
  assert(lc);
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

Btype *Llvm_backend::placeholder_array_type(const std::string &name,
                                            Location location)
{
  Btype *pat = new BArrayType(name);
  llvm::Type *opaque = makeOpaqueLlvmType("PAT");
  pat->setType(opaque);
  placeholders_.insert(pat);
  return pat;
}

Btype *Llvm_backend::functionReturnType(Btype *typ)
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

Btype *Llvm_backend::elementTypeByIndex(Btype *btype, unsigned fieldIndex)
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

void Llvm_backend::postProcessResolvedPointerPlaceholder(BPointerType *bpt,
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

void Llvm_backend::postProcessResolvedStructPlaceholder(BStructType *bst,
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

void Llvm_backend::postProcessResolvedArrayPlaceholder(BArrayType *bat,
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
void Llvm_backend::postProcessResolvedPlaceholder(Btype *btype)
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

bool Llvm_backend::set_placeholder_pointer_type(Btype *placeholder,
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

bool Llvm_backend::set_placeholder_function_type(Btype *placeholder,
                                                 Btype *ft) {
  return set_placeholder_pointer_type(placeholder, ft);
}

// Fill in the fields of a placeholder struct type.

bool
Llvm_backend::set_placeholder_struct_type(Btype *placeholder,
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

bool Llvm_backend::set_placeholder_array_type(Btype *placeholder,
                                              Btype *element_btype,
                                              Bexpression *length) {
  if (placeholder == errorType_ || element_btype == errorType_ ||
      length == errorExpression_.get())
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

Btype *Llvm_backend::named_type(const std::string &name,
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

Btype *Llvm_backend::circular_pointer_type(Btype *placeholder, bool) {
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

bool Llvm_backend::is_circular_pointer_type(Btype *btype) {
  assert(btype);
  auto it = circularPointerTypes_.find(btype->type());
  return it != circularPointerTypes_.end();
}

// Return the size of a type.

int64_t Llvm_backend::type_size(Btype *btype) {
  if (btype == errorType_)
    return 1;
  uint64_t uval = datalayout_.getTypeSizeInBits(btype->type());
  assert((uval & 0x7) == 0);
  uval /= 8;
  return static_cast<int64_t>(uval);
}

// Return the alignment of a type.

int64_t Llvm_backend::type_alignment(Btype *btype) {
  if (btype == errorType_)
    return 1;
  unsigned uval = datalayout_.getPrefTypeAlignment(btype->type());
  return static_cast<int64_t>(uval);
}

// Return the alignment of a struct field of type BTYPE.
//
// One case where type_field_align(X) != type_align(X) is
// for type 'double' on x86 32-bit, where for compatibility
// a double field is 4-byte aligned but will be 8-byte aligned
// otherwise.

int64_t Llvm_backend::type_field_alignment(Btype *btype) {
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
  const llvm::StructLayout *sl = datalayout_.getStructLayout(dummyst);
  uint64_t uoff = sl->getElementOffset(1);
  unsigned talign = datalayout_.getPrefTypeAlignment(btype->type());
  int64_t rval = (uoff < talign ? uoff : talign);
  return rval;
}

// Return the offset of a field in a struct.

int64_t Llvm_backend::type_field_offset(Btype *btype, size_t index) {
  if (btype == errorType_)
    return 0;
  assert(btype->type()->isStructTy());
  llvm::StructType *llvm_st = llvm::cast<llvm::StructType>(btype->type());
  const llvm::StructLayout *sl = datalayout_.getStructLayout(llvm_st);
  uint64_t uoff = sl->getElementOffset(index);
  return static_cast<int64_t>(uoff);
}

void Llvm_backend::defineLibcallBuiltin(const char *name, const char *libname,
                                        unsigned libfunc, ...) {
  va_list ap;
  std::vector<llvm::Type *> types;
  va_start(ap, libfunc);
  llvm::Type *resultType = va_arg(ap, llvm::Type *);
  types.push_back(resultType);
  llvm::Type *parmType = va_arg(ap, llvm::Type *);
  while (parmType) {
    types.push_back(parmType);
    parmType = va_arg(ap, llvm::Type *);
  }
  defineLibcallBuiltin(name, libname, types, libfunc);
}

void Llvm_backend::defineLibcallBuiltin(const char *name, const char *libname,
                                        const std::vector<llvm::Type *> &types,
                                        unsigned libfunc) {
  llvm::Type *resultType = types[0];
  llvm::SmallVector<llvm::Type *, 16> ptypes(0);
  for (unsigned idx = 1; idx < types.size(); ++idx)
    ptypes.push_back(types[idx]);
  const bool isVarargs = false;
  llvm::FunctionType *ft =
      llvm::FunctionType::get(resultType, ptypes, isVarargs);
  llvm::LibFunc::Func lf = static_cast<llvm::LibFunc::Func>(libfunc);
  llvm::GlobalValue::LinkageTypes plinkage = llvm::GlobalValue::ExternalLinkage;
  llvm::Function *fcn =
      llvm::Function::Create(ft, plinkage, name, module_.get());

  // FIXME: once we have a pass manager set up for the back end, we'll
  // want to turn on this code, since it will be helpful to catch
  // errors/mistakes. For the time being it can't be turned on (not
  // pass manager is set up).
  if (TLI_ && lf != llvm::LibFunc::NumLibFuncs) {

    // Verify that the function is available on this target.
    assert(TLI_->has(lf));

    // Verify that the name and type we've computer so far matches up
    // with how LLVM views the routine. For example, if we are trying
    // to create a version of memcmp() that takes a single boolean as
    // an argument, that's going to be a show-stopper type problem.
    assert(TLI_->getLibFunc(*fcn, lf));
  }

  defineBuiltinFcn(name, libname, fcn);
}

void Llvm_backend::defineIntrinsicBuiltin(const char *name, const char *libname,
                                          unsigned intrinsicID, ...) {
  va_list ap;
  llvm::SmallVector<llvm::Type *, 16> overloadTypes;
  va_start(ap, intrinsicID);
  llvm::Type *oType = va_arg(ap, llvm::Type *);
  while (oType) {
    overloadTypes.push_back(oType);
    oType = va_arg(ap, llvm::Type *);
  }
  llvm::Intrinsic::ID iid = static_cast<llvm::Intrinsic::ID>(intrinsicID);
  llvm::Function *fcn =
      llvm::Intrinsic::getDeclaration(module_.get(), iid, overloadTypes);
  assert(fcn != nullptr);
  defineBuiltinFcn(name, libname, fcn);
}

// Create a BType function type based on an equivalent LLVM function type.

//

// Define name -> fcn mapping for a builtin.
// Notes:
// - LLVM makes a distinction between libcalls (such as
//   "__sync_fetch_and_add_1") and intrinsics (such as
//   "__builtin_expect" or "__builtin_trap"); the former
//   are target-independent and the latter are target-dependent
// - intrinsics with the no-return property (such as
//   "__builtin_trap" will already be set up this way

void Llvm_backend::defineBuiltinFcn(const char *name, const char *libname,
                                    llvm::Function *fcn)
{
  llvm::PointerType *llpft =
      llvm::cast<llvm::PointerType>(fcn->getType());
  llvm::FunctionType *llft =
      llvm::cast<llvm::FunctionType>(llpft->getElementType());
  BFunctionType *fcnType = makeAuxFcnType(llft);
  Bfunction *bfunc = new Bfunction(fcn, fcnType, name);
  assert(builtinMap_.find(name) == builtinMap_.end());
  builtinMap_[name] = bfunc;
  if (libname) {
    Bfunction *bfunc = new Bfunction(fcn, fcnType, libname);
    assert(builtinMap_.find(libname) == builtinMap_.end());
    builtinMap_[libname] = bfunc;
  }
}

// Look up a named built-in function in the current backend implementation.
// Returns NULL if no built-in function by that name exists.

Bfunction *Llvm_backend::lookup_builtin(const std::string &name) {
  auto it = builtinMap_.find(name);
  if (it == builtinMap_.end())
    return nullptr;
  return it->second;
}

void Llvm_backend::defineAllBuiltins() {
  defineSyncFetchAndAddBuiltins();
  defineIntrinsicBuiltins();
  defineTrigBuiltins();
}

void Llvm_backend::defineIntrinsicBuiltins() {
  defineIntrinsicBuiltin("__builtin_trap", nullptr, llvm::Intrinsic::trap,
                         nullptr);

  defineIntrinsicBuiltin("__builtin_return_address", nullptr,
                         llvm::Intrinsic::returnaddress, llvmPtrType_,
                         llvmInt32Type_, nullptr);

  defineIntrinsicBuiltin("__builtin_frame_address", nullptr,
                         llvm::Intrinsic::frameaddress, llvmPtrType_,
                         llvmInt32Type_, nullptr);

  defineIntrinsicBuiltin("__builtin_expect", nullptr, llvm::Intrinsic::expect,
                         llvmIntegerType_, nullptr);

  defineLibcallBuiltin("__builtin_memcmp", "memcmp", llvm::LibFunc::memcmp,
                       llvmInt32Type_, llvmPtrType_, llvmPtrType_,
                       llvmSizeType_, nullptr);

  // go runtime refers to this intrinsic as "ctz", however the LLVM
  // equivalent is named "cttz".
  defineIntrinsicBuiltin("__builtin_ctz", "ctz", llvm::Intrinsic::cttz,
                         llvmIntegerType_, nullptr);

  // go runtime refers to this intrinsic as "ctzll", however the LLVM
  // equivalent is named "cttz".
  defineIntrinsicBuiltin("__builtin_ctzll", "ctzll", llvm::Intrinsic::cttz,
                         llvmInt64Type_, nullptr);

  // go runtime refers to this intrinsic as "bswap32", however the LLVM
  // equivalent is named just "bswap"
  defineIntrinsicBuiltin("__builtin_bswap32", "bswap32", llvm::Intrinsic::bswap,
                         llvmInt32Type_, nullptr);

  // go runtime refers to this intrinsic as "bswap64", however the LLVM
  // equivalent is named just "bswap"
  defineIntrinsicBuiltin("__builtin_bswap64", "bswap64", llvm::Intrinsic::bswap,
                         llvmInt64Type_, nullptr);
}

namespace {

typedef enum {
  OneArg = 0,  // takes form "double foo(double)"
  TwoArgs = 1, // takes form "double bar(double, double)"
  TwoMixed = 2 // takes form "double bar(double, int)"
} mflav;

typedef struct {
  const char *name;
  mflav nargs;
  llvm::LibFunc::Func lf;
} mathfuncdesc;
}

void Llvm_backend::defineTrigBuiltins() {
  const std::vector<llvm::Type *> onearg_double = {llvmDoubleType_,
                                                   llvmDoubleType_};
  const std::vector<llvm::Type *> onearg_long_double = {llvmLongDoubleType_,
                                                        llvmLongDoubleType_};
  const std::vector<llvm::Type *> twoargs_double = {
      llvmDoubleType_, llvmDoubleType_, llvmDoubleType_};
  const std::vector<llvm::Type *> twoargs_long_double = {
      llvmLongDoubleType_, llvmLongDoubleType_, llvmLongDoubleType_};
  const std::vector<llvm::Type *> mixed_double = {
      llvmDoubleType_, llvmDoubleType_, llvmIntegerType_};
  const std::vector<llvm::Type *> mixed_long_double = {
      llvmLongDoubleType_, llvmLongDoubleType_, llvmIntegerType_};
  const std::vector<const std::vector<llvm::Type *> *> signatures = {
      &onearg_double, &twoargs_double, &mixed_double};
  const std::vector<const std::vector<llvm::Type *> *> lsignatures = {
      &onearg_long_double, &twoargs_long_double, &mixed_long_double};

  static const mathfuncdesc funcs[] = {
      {"acos", OneArg, llvm::LibFunc::acos},
      {"asin", OneArg, llvm::LibFunc::asin},
      {"atan", OneArg, llvm::LibFunc::atan},
      {"atan2", TwoArgs, llvm::LibFunc::atan2},
      {"ceil", OneArg, llvm::LibFunc::ceil},
      {"cos", OneArg, llvm::LibFunc::cos},
      {"exp", OneArg, llvm::LibFunc::exp},
      {"expm1", OneArg, llvm::LibFunc::expm1},
      {"fabs", OneArg, llvm::LibFunc::fabs},
      {"floor", OneArg, llvm::LibFunc::floor},
      {"fmod", TwoArgs, llvm::LibFunc::fmod},
      {"log", OneArg, llvm::LibFunc::log},
      {"log1p", OneArg, llvm::LibFunc::log1p},
      {"log10", OneArg, llvm::LibFunc::log10},
      {"log2", OneArg, llvm::LibFunc::log2},
      {"sin", OneArg, llvm::LibFunc::sin},
      {"sqrt", OneArg, llvm::LibFunc::sqrt},
      {"tan", OneArg, llvm::LibFunc::tan},
      {"trunc", OneArg, llvm::LibFunc::trunc},
      {"ldexp", TwoMixed, llvm::LibFunc::trunc},
  };

  const unsigned nfuncs = sizeof(funcs) / sizeof(mathfuncdesc);
  for (unsigned idx = 0; idx < nfuncs; ++idx) {
    const mathfuncdesc &d = funcs[idx];
    char bbuf[128];
    char lbuf[128];

    sprintf(bbuf, "__builtin_%s", d.name);
    const std::vector<llvm::Type *> *sig = signatures[d.nargs];
    defineLibcallBuiltin(bbuf, d.name, *sig, d.lf);
    sprintf(lbuf, "%sl", d.name);
    sprintf(bbuf, "__builtin_%s", lbuf);
    const std::vector<llvm::Type *> *lsig = lsignatures[d.nargs];
    defineLibcallBuiltin(bbuf, lbuf, *lsig, d.lf);
  }
}

void Llvm_backend::defineSyncFetchAndAddBuiltins() {
  std::vector<unsigned> sizes = {1, 2, 4, 8};
  for (auto sz : sizes) {
    char nbuf[64];
    sprintf(nbuf, "__sync_fetch_and_add_%u", sz);
    llvm::Type *it = llvm::IntegerType::get(context_, sz << 3);
    llvm::PointerType *pit = llvm::PointerType::get(it, addressSpace_);
    defineLibcallBuiltin(nbuf, nullptr,  // name, libname
                         NotInTargetLib, // Libfunc ID
                         llvmVoidType_,  // result type
                         pit, it,        // param types
                         nullptr);
  }
}

bool Llvm_backend::moduleScopeValue(llvm::Value *val, Btype *btype) const
{
  valbtype vbt(std::make_pair(val, btype));
  return (valueExprmap_.find(vbt) != valueExprmap_.end());
}

Bexpression *Llvm_backend::makeValueExpression(llvm::Value *val,
                                               Btype *btype,
                                               ValExprScope scope,
                                               Location location) {
  assert(val || scope == LocalScope);
  if (scope == GlobalScope) {
    valbtype vbt(std::make_pair(val, btype));
    auto it = valueExprmap_.find(vbt);
    if (it != valueExprmap_.end())
      return it->second;
  }
  Bexpression *rval = new Bexpression(val, btype, location);
  if (scope == GlobalScope) {
    valbtype vbt(std::make_pair(val, btype));
    valueExprmap_[vbt] = rval;
  } else
    expressions_.push_back(rval);
  return rval;
}

void Llvm_backend::incorporateExpression(Bexpression *dst,
                                         Bexpression *src,
                                         std::set<llvm::Instruction *> *visited)
{
  dst->incorporateStmt(src->stmt());
  for (auto inst : src->instructions()) {
    if (llvm::isa<llvm::AllocaInst>(inst))
      continue;
    dst->appendInstruction(inst);
    if (visited) {
      assert(visited->find(inst) == visited->end());
      visited->insert(inst);
    }
  }
}

Bexpression *Llvm_backend::makeExpression(llvm::Value *value,
                                          MkExprAction action,
                                          Btype *btype,
                                          Location location,
                                          Bexpression *srcExpr,
                                          ...)
{
  std::vector<Bexpression *> srcs;
  va_list ap;
  va_start(ap, srcExpr);
  Bexpression *src = srcExpr;
  while (src) {
    srcs.push_back(src);
    src = va_arg(ap, Bexpression *);
  }
  return makeExpression(value, action, btype, location, srcs);
}

Bexpression *
Llvm_backend::makeExpression(llvm::Value *value,
                             MkExprAction action,
                             Btype *btype,
                             Location location,
                             const std::vector<Bexpression *> &srcs)
{
  ValExprScope scope =
      moduleScopeValue(value, btype) ? GlobalScope : LocalScope;
  Bexpression *result = makeValueExpression(value, btype, scope, location);
  std::set<llvm::Instruction *> visited;
  unsigned numSrcs = 0;
  for (auto src : srcs) {
    incorporateExpression(result, src, &visited);
    numSrcs += 1;
  }
  if (action == AppendInst && llvm::isa<llvm::Instruction>(value)) {
    // We need this guard to deal with situations where we've done
    // something like x | 0, in which the IRBuilder will simply return
    // the left operand.
    llvm::Instruction *inst = llvm::cast<llvm::Instruction>(value);
      if (visited.find(inst) == visited.end())
        result->appendInstruction(inst);
  }
  if (numSrcs == 1 && srcs[0]->varExprPending())
    result->setVarExprPending(srcs[0]->varContext());
  return result;
}

// Return the zero value for a type.

Bexpression *Llvm_backend::zero_expression(Btype *btype) {
  if (btype == errorType_)
    return errorExpression_.get();
  llvm::Value *zeroval = llvm::Constant::getNullValue(btype->type());
  return makeValueExpression(zeroval, btype, GlobalScope, Location());
}

Bexpression *Llvm_backend::error_expression() { return errorExpression_.get(); }

Bexpression *Llvm_backend::nil_pointer_expression() {

  // What type should we assign a NIL pointer expression? This
  // is something of a head-scratcher. For now use uintptr.
  llvm::Type *pti = llvm::PointerType::get(llvmIntegerType_, addressSpace_);
  Btype *uintptrt = makeAuxType(pti);
  return zero_expression(uintptrt);
}

Bexpression *Llvm_backend::loadFromExpr(Bexpression *expr,
                                        Btype* &loadResultType,
                                        Location location)
{
  std::string ldname(expr->tag());
  ldname += ".ld";
  ldname = namegen(ldname);

  // If this is a load from a pointer flagged as being a circular
  // type, insert a conversion prior to the load so as to force
  // the value to the correct type. This is weird but necessary,
  // since the LLVM type system can't accurately model Go circular
  // pointer types.
  Bexpression *space = expr;
  auto it = circularConversionLoadMap_.find(expr->btype());
  if (it != circularConversionLoadMap_.end()) {
    Btype *tctyp = it->second;
    space = convert_expression(pointer_type(tctyp), expr, location);
    loadResultType = tctyp;
  }
  llvm::Instruction *ldinst = new llvm::LoadInst(space->value(), ldname);
  Bexpression *rval = makeExpression(ldinst, AppendInst, loadResultType,
                                     expr->location(), space, nullptr);
  return rval;
}

// An expression that indirectly references an expression.

Bexpression *Llvm_backend::indirect_expression(Btype *btype,
                                               Bexpression *expr,
                                               bool known_valid,
                                               Location location) {
  if (expr == errorExpression_.get() || btype == errorType_)
    return errorExpression_.get();

  assert(expr->btype()->type()->isPointerTy());

  // FIXME: add check for nil pointer

  const VarContext *vc = nullptr;
  if (expr->varExprPending()) {
    vc = &expr->varContext();
    if (vc->addrLevel() != 0) {
      // handle *&x
      Bexpression *dexpr =
          makeValueExpression(expr->value(), btype, LocalScope, location);
      dexpr->setVarExprPending(vc->lvalue(), vc->addrLevel() - 1);
      dexpr->appendInstructions(expr->instructions());
      return dexpr;
    }
  }

  Bexpression *loadExpr = loadFromExpr(expr, btype, location);
  if (vc)
    loadExpr->setVarExprPending(expr->varContext());

  return loadExpr;
}

// Get the address of an expression.

Bexpression *Llvm_backend::address_expression(Bexpression *bexpr,
                                              Location location) {
  if (bexpr == errorExpression_.get())
    return errorExpression_.get();

  // Gofrontend tends to take the address of things that are already
  // pointer-like to begin with (for example, C strings and and
  // arrays). This presents wrinkles here, since since an array type
  // in LLVM is already effectively a pointer (you can feed it
  // directly into a GEP as opposed to having to take the address of
  // it first).  Bypass the effects of the address operator if
  // this is the case. This is hacky, maybe I can come up with a
  // better solution for this issue(?).
  if (llvm::isa<llvm::ConstantArray>(bexpr->value()))
    return bexpr;
  if (bexpr->value()->getType() == stringType_->type() &&
      llvm::isa<llvm::Constant>(bexpr->value()))
    return bexpr;

  // Create new expression with proper type.
  Btype *pt = pointer_type(bexpr->btype());
  Bexpression *rval =
      makeExpression(bexpr->value(), DontAppend, pt, location, bexpr, nullptr);
  std::string adtag(bexpr->tag());
  adtag += ".ad";
  rval->setTag(adtag);
  const VarContext &vc = bexpr->varContext();
  rval->setVarExprPending(vc.lvalue(), vc.addrLevel() + 1);

  // Handle circular types
  auto it = circularConversionAddrMap_.find(bexpr->btype());
  if (it != circularConversionAddrMap_.end()) {
    assert(it != circularConversionAddrMap_.end());
    Btype *ctyp = it->second;
    return convert_expression(ctyp, rval, location);
  }

  return rval;
}

Bexpression *Llvm_backend::resolve(Bexpression *expr, Bfunction *func)
{
  assert(!(expr->compositeInitPending() && expr->varExprPending()));
  if (expr->compositeInitPending())
    expr = resolveCompositeInit(expr, func, nullptr);
  else if (expr->varExprPending())
    expr = resolveVarContext(expr);
  return expr;
}

Bexpression *Llvm_backend::resolveVarContext(Bexpression *expr)
{
  if (expr->varExprPending()) {
    const VarContext &vc = expr->varContext();
    assert(vc.addrLevel() == 0 || vc.addrLevel() == 1);
    if (vc.addrLevel() == 1 || vc.lvalue()) {
      assert(vc.addrLevel() == 0 || expr->btype()->type()->isPointerTy());
      expr->resetVarExprContext();
      return expr;
    }
    Btype *btype = expr->btype();
    Bexpression *rval = loadFromExpr(expr, btype, Location());
    rval->resetVarExprContext();
    return rval;
  }
  return expr;
}

// This version repurposes/reuses the input Bexpression as the
// result (which could be changed if needed).

Bexpression *Llvm_backend::genArrayInit(llvm::ArrayType *llat,
                                        Bexpression *expr,
                                        llvm::Value *storage)
{
  CompositeInitContext &cic = expr->compositeInitContext();
  const std::vector<Bexpression *> &aexprs = cic.elementExpressions();
  unsigned nElements = llat->getNumElements();
  assert(nElements == aexprs.size());

  for (unsigned eidx = 0; eidx < nElements; ++eidx) {

    // Construct an appropriate GEP
    llvm::Value *idxval = llvm::ConstantInt::get(llvmInt32Type_, eidx);
    llvm::Value *gep = makeArrayIndexGEP(llat, idxval, storage);
    if (llvm::isa<llvm::Instruction>(gep))
      expr->appendInstruction(llvm::cast<llvm::Instruction>(gep));

    // Store value into gep
    Bexpression *valexp = resolveVarContext(aexprs[eidx]);
    for (auto inst : valexp->instructions()) {
      assert(inst->getParent() == nullptr);
      expr->appendInstruction(inst);
    }
    llvm::Instruction *si = new llvm::StoreInst(valexp->value(), gep);
    expr->appendInstruction(si);
  }

  expr->finishCompositeInit(storage);
  return expr;
}

// This version repurposes/reuses the input Bexpression as the
// result (which could be changed if needed).

Bexpression *Llvm_backend::genStructInit(llvm::StructType *llst,
                                         Bexpression *expr,
                                         llvm::Value *storage)
{
  CompositeInitContext &cic = expr->compositeInitContext();
  const std::vector<Bexpression *> &fexprs = cic.elementExpressions();
  unsigned nFields = llst->getNumElements();
  assert(nFields == fexprs.size());

  for (unsigned fidx = 0; fidx < nFields; ++fidx) {

    // Construct an appropriate GEP
    llvm::Value *gep = makeFieldGEP(llst, fidx, storage);
    if (llvm::isa<llvm::Instruction>(gep))
      expr->appendInstruction(llvm::cast<llvm::Instruction>(gep));

    // Store value into gep
    assert(fexprs[fidx]);
    Bexpression *valexp = resolveVarContext(fexprs[fidx]);
    for (auto inst : valexp->instructions()) {
      assert(inst->getParent() == nullptr);
      expr->appendInstruction(inst);
    }
    llvm::Instruction *si = new llvm::StoreInst(valexp->value(), gep);
    expr->appendInstruction(si);
  }

  expr->finishCompositeInit(storage);
  return expr;
}

Bexpression *Llvm_backend::resolveCompositeInit(Bexpression *expr,
                                                Bfunction *func,
                                                llvm::Value *storage)
{
  if (expr == errorExpression_.get() || func == errorFunction_.get())
    return errorExpression_.get();
  if (!storage) {
    assert(func);
    std::string tname(namegen("tmp"));
    Bvariable *tvar = local_variable(func, tname, expr->btype(), true,
                                     Location());
    storage = tvar->value();
  }

  // Call separate helper depending on array or struct
  llvm::Type *llt = expr->btype()->type();
  assert(llt->isStructTy() || llt->isArrayTy());
  if (llt->isStructTy())
    return genStructInit(llvm::cast<llvm::StructType>(llt), expr, storage);
  else
    return genArrayInit(llvm::cast<llvm::ArrayType>(llt), expr, storage);
}

// An expression that references a variable.

Bexpression *Llvm_backend::var_expression(Bvariable *var,
                                          Varexpr_context in_lvalue_pos,
                                          Location location) {
  if (var == errorVariable_.get())
    return errorExpression_.get();

  // FIXME: record debug location

  Bexpression *varexp =
      makeValueExpression(var->value(), var->btype(), LocalScope, location);
  varexp->setTag(var->name().c_str());
  varexp->setVarExprPending(in_lvalue_pos == VE_lvalue, 0);
  return varexp;
}

// Return an expression that declares a constant named NAME with the
// constant value VAL in BTYPE.

Bexpression *Llvm_backend::named_constant_expression(Btype *btype,
                                                     const std::string &name,
                                                     Bexpression *val,
                                                     Location location) {
  assert(false && "LLvm_backend::named_constant_expression not yet impl");
  return nullptr;
}

template <typename wideint_t>
wideint_t checked_convert_mpz_to_int(mpz_t value) {
  // See http://gmplib.org/manual/Integer-Import-and-Export.html for an
  // explanation of this formula
  size_t numbits = 8 * sizeof(wideint_t);
  size_t count = (mpz_sizeinbase(value, 2) + numbits - 1) / numbits;
  // frontend should have insured this already
  assert(count <= 2);
  count = 2;
  wideint_t receive[2];
  receive[0] = 0;
  receive[1] = 0;
  mpz_export(&receive[0], &count, -1, sizeof(wideint_t), 0, 0, value);
  // frontend should have insured this already
  assert(receive[1] == 0);
  wideint_t rval = receive[0];
  if (mpz_sgn(value) < 0)
    rval = -rval;
  return rval;
}

// Return a typed value as a constant integer.

Bexpression *Llvm_backend::integer_constant_expression(Btype *btype,
                                                       mpz_t mpz_val) {
  if (btype == errorType_)
    return errorExpression_.get();
  assert(btype->type()->isIntegerTy());

  // Force mpz_val into either into uint64_t or int64_t depending on
  // whether btype was declared as signed or unsigned.
  //
  // Q: better to use APInt here?

  Bexpression *rval;
  BIntegerType *bit = btype->castToBIntegerType();
  if (bit->isUnsigned()) {
    uint64_t val = checked_convert_mpz_to_int<uint64_t>(mpz_val);
    assert(llvm::ConstantInt::isValueValidForType(btype->type(), val));
    llvm::Constant *lval = llvm::ConstantInt::get(btype->type(), val);
    rval = makeValueExpression(lval, btype, GlobalScope, Location());
  } else {
    int64_t val = checked_convert_mpz_to_int<int64_t>(mpz_val);
    assert(llvm::ConstantInt::isValueValidForType(btype->type(), val));
    llvm::Constant *lval = llvm::ConstantInt::getSigned(btype->type(), val);
    rval = makeValueExpression(lval, btype, GlobalScope, Location());
  }
  return rval;
}

// Return a typed value as a constant floating-point number.

Bexpression *Llvm_backend::float_constant_expression(Btype *btype, mpfr_t val) {
  if (btype == errorType_)
    return errorExpression_.get();

  // Force the mpfr value into float, double, or APFloat as appropriate.
  //
  // Note: at the moment there is no way to create an APFloat from a
  // "long double" value, so this code takes the unpleasant step of
  // converting a quad mfr value from text and then back into APFloat
  // from there.

  if (btype->type() == llvmFloatType_) {
    float fval = mpfr_get_flt(val, GMP_RNDN);
    llvm::APFloat apf(fval);
    llvm::Constant *fcon = llvm::ConstantFP::get(context_, apf);
    return makeValueExpression(fcon, btype, GlobalScope, Location());
  } else if (btype->type() == llvmDoubleType_) {
    double dval = mpfr_get_d(val, GMP_RNDN);
    llvm::APFloat apf(dval);
    llvm::Constant *fcon = llvm::ConstantFP::get(context_, apf);
    return makeValueExpression(fcon, btype, GlobalScope, Location());
  } else if (btype->type() == llvmLongDoubleType_) {
    assert("not yet implemented" && false);
    return nullptr;
  } else {
    return errorExpression_.get();
  }
}

// Return a typed real and imaginary value as a constant complex number.

Bexpression *Llvm_backend::complex_constant_expression(Btype *btype,
                                                       mpc_t val) {
  assert(false && "Llvm_backend::complex_constant_expression not yet impl");
  return nullptr;
}

// Make a constant string expression.

Bexpression *Llvm_backend::string_constant_expression(const std::string &val)
{
  if (val.size() == 0) {
    llvm::Value *zer = llvm::Constant::getNullValue(stringType_->type());
    return makeValueExpression(zer, stringType_, GlobalScope, Location());
  }

  // At the moment strings are not commoned.
  bool doAddNull = true;
  llvm::Constant *scon =
      llvm::ConstantDataArray::getString(context_,
                                         llvm::StringRef(val),
                                         doAddNull);
  Bvariable *svar =
      makeModuleVar(makeAuxType(scon->getType()),
                    "", "", Location(), MV_Constant, MV_DefaultSection,
                    MV_NotInComdat, MV_DefaultVisibility,
                    llvm::GlobalValue::PrivateLinkage, scon, 1);
  llvm::Constant *varval = llvm::cast<llvm::Constant>(svar->value());
  llvm::Constant *bitcast =
      llvm::ConstantExpr::getBitCast(varval, stringType_->type());
  return makeValueExpression(bitcast, stringType_, LocalScope, Location());
}

// Make a constant boolean expression.

Bexpression *Llvm_backend::boolean_constant_expression(bool val) {
  LIRBuilder builder(context_, llvm::ConstantFolder());
  llvm::Value *con = (val ? llvm::ConstantInt::getTrue(context_)
                          : llvm::ConstantInt::getFalse(context_));
  llvm::Value *tobool = builder.CreateZExt(con, bool_type()->type(), "");
  return makeValueExpression(tobool, bool_type(), GlobalScope, Location());
}

// Return the real part of a complex expression.

Bexpression *Llvm_backend::real_part_expression(Bexpression *bcomplex,
                                                Location location) {
  assert(false && "Llvm_backend::real_part_expression not yet impl");
  return nullptr;
}

// Return the imaginary part of a complex expression.

Bexpression *Llvm_backend::imag_part_expression(Bexpression *bcomplex,
                                                Location location) {
  assert(false && "Llvm_backend::imag_part_expression not yet impl");
  return nullptr;
}

// Make a complex expression given its real and imaginary parts.

Bexpression *Llvm_backend::complex_expression(Bexpression *breal,
                                              Bexpression *bimag,
                                              Location location) {
  assert(false && "Llvm_backend::complex_expression not yet impl");
  return nullptr;
}

// An expression that converts an expression to a different type.

Bexpression *Llvm_backend::convert_expression(Btype *type, Bexpression *expr,
                                              Location location) {
  if (type == errorType_ || expr == errorExpression_.get())
    return errorExpression_.get();
  if (expr->btype() == type)
    return expr;

  llvm::Value *val = expr->value();
  assert(val);

  // No-op casts are ok
  llvm::Type *toType = type->type();
  if (toType == expr->value()->getType())
    return expr;

  // When the frontend casts something to function type, what this
  // really means in the LLVM realm is "pointer to function" type.
  if (toType->isFunctionTy()) {
    type = pointer_type(type);
    toType = type->type();
  }

  LIRBuilder builder(context_, llvm::ConstantFolder());

  // Adjust the "to" type if pending var expr.
  if (expr->varExprPending()) {
    llvm::Type *pet = llvm::PointerType::get(expr->btype()->type(),
                                             addressSpace_);
    if (val->getType() == pet)
      toType = llvm::PointerType::get(toType, addressSpace_);
  }

  // Pointer type to pointer-sized-integer type. Comes up when
  // converting function pointer to function descriptor (during
  // creation of function descriptor vals) or constant array to
  // uintptr (as part of GC symbol initializer creation), and in other
  // places in FE-generated code (ex: array index checks).
  if (val->getType()->isPointerTy() && toType == llvmIntegerType_) {
    std::string tname(namegen("pticast"));
    llvm::Value *pticast = builder.CreatePtrToInt(val, type->type(), tname);
    Bexpression *rval = makeExpression(pticast, AppendInst, type, location,
                                       expr, nullptr);
    return rval;
  }

  // Pointer-sized-integer type pointer type. This comes up
  // in type hash/compare functions.
  if (toType && val->getType() == llvmIntegerType_) {
    std::string tname(namegen("itpcast"));
    llvm::Value *itpcast = builder.CreateIntToPtr(val, type->type(), tname);
    Bexpression *rval = makeExpression(itpcast, AppendInst, type, location,
                                       expr, nullptr);
    return rval;
  }

  // For pointer conversions (ex: *int32 => *int64) create an
  // appropriate bitcast.
  if (val->getType()->isPointerTy() && toType->isPointerTy()) {
    std::string tag("cast");
    llvm::Value *bitcast = builder.CreateBitCast(val, toType, tag);
    Bexpression *rval = makeExpression(bitcast, AppendInst, type, location,
                                       expr, nullptr);
    return rval;
  }

  // Integer-to-integer conversions
  if (val->getType()->isIntegerTy() && type->type()->isIntegerTy()) {
    llvm::IntegerType *valIntTyp =
        llvm::cast<llvm::IntegerType>(val->getType());
    llvm::IntegerType *toIntTyp =
        llvm::cast<llvm::IntegerType>(type->type());
    unsigned valbits = valIntTyp->getBitWidth();
    unsigned tobits = toIntTyp->getBitWidth();
    llvm::Value *conv = nullptr;
    if (tobits > valbits) {
      if (type->castToBIntegerType()->isUnsigned())
        conv = builder.CreateZExt(val, type->type(), namegen("zext"));
      else
        conv = builder.CreateSExt(val, type->type(), namegen("sext"));
    } else {
      conv = builder.CreateTrunc(val, type->type(), namegen("trunc"));
    }
    Bexpression *rval = makeExpression(conv, AppendInst, type, location,
                                       expr, nullptr);
    return rval;
  }

  // This case not handled yet. In particular code needs to be
  // written to handle signed/unsigned, conversions between scalars
  // of various sizes and types.
  assert(false && "this flavor of conversion not yet handled");

  return expr;
}

// Get the address of a function.

Bexpression *Llvm_backend::function_code_expression(Bfunction *bfunc,
                                                    Location location) {
  if (bfunc == errorFunction_.get())
    return errorExpression_.get();

  assert(llvm::isa<llvm::Constant>(bfunc->function()));

  // Look up pointer-to-function type
  Btype *fpBtype = pointer_type(bfunc->fcnType());

  // Return a pointer-to-function value expression
  return makeValueExpression(bfunc->function(), fpBtype, GlobalScope, location);
}

llvm::Value *Llvm_backend::makeArrayIndexGEP(llvm::ArrayType *llat,
                                             llvm::Value *idxval,
                                             llvm::Value *sptr)
{
  LIRBuilder builder(context_, llvm::ConstantFolder());
  llvm::SmallVector<llvm::Value *, 2> elems(2);
  elems[0] = llvm::ConstantInt::get(llvmInt32Type_, 0);
  elems[1] = idxval;
  llvm::Value *val = builder.CreateGEP(llat, sptr, elems, namegen("index"));
  return val;
}

// Field GEP helper
llvm::Value *Llvm_backend::makeFieldGEP(llvm::StructType *llst,
                                        unsigned fieldIndex,
                                        llvm::Value *sptr)
{
  LIRBuilder builder(context_, llvm::ConstantFolder());
  assert(fieldIndex < llst->getNumElements());
  std::string tag(namegen("field"));
  llvm::Value *val =
      builder.CreateConstInBoundsGEP2_32(llst, sptr, 0, fieldIndex, tag);
  return val;
}

// Return an expression for the field at INDEX in BSTRUCT.

Bexpression *Llvm_backend::struct_field_expression(Bexpression *bstruct,
                                                   size_t index,
                                                   Location location) {
  if (bstruct == errorExpression_.get())
    return errorExpression_.get();

  // Construct an appropriate GEP
  llvm::Type *llt = bstruct->btype()->type();
  assert(llt->isStructTy());
  llvm::StructType *llst = llvm::cast<llvm::StructType>(llt);
  llvm::Value *gep = makeFieldGEP(llst, index, bstruct->value());

  // Wrap result in a Bexpression
  Btype *bft = elementTypeByIndex(bstruct->btype(), index);
  Bexpression *rval = makeExpression(gep, AppendInst, bft,
                                     location, bstruct, nullptr);
  std::string tag(bstruct->tag());
  tag += ".field";
  rval->setTag(tag);

  // We're done
  return rval;
}

// Return an expression that executes BSTAT before BEXPR.

Bexpression *Llvm_backend::compound_expression(Bstatement *bstat,
                                               Bexpression *bexpr,
                                               Location location) {
  if (bstat == errorStatement_.get() || bexpr == errorExpression_.get())
    return errorExpression_.get();

  bexpr->incorporateStmt(bstat);
  return bexpr;
}

// Return an expression that executes THEN_EXPR if CONDITION is true, or
// ELSE_EXPR otherwise.

Bexpression *Llvm_backend::conditional_expression(Bfunction *function,
                                                  Btype *btype,
                                                  Bexpression *condition,
                                                  Bexpression *then_expr,
                                                  Bexpression *else_expr,
                                                  Location location) {
  if (function == errorFunction_.get() ||
      btype == errorType_ ||
      condition == errorExpression_.get() ||
      then_expr == errorExpression_.get() ||
      else_expr == errorExpression_.get())
    return errorExpression_.get();

  assert(condition && then_expr);

  condition = resolveVarContext(condition);
  then_expr = resolveVarContext(then_expr);
  if (else_expr)
    else_expr = resolveVarContext(else_expr);

  assert(! condition->compositeInitPending());
  assert(! then_expr->compositeInitPending());
  assert(! else_expr || ! else_expr->compositeInitPending());

  Bblock *thenBlock = function->newBlock(function);
  Bblock *elseBlock = nullptr;
  Bvariable *tempv = nullptr;

  // FIXME: add lifetime intrinsics for temp var below.
  Bstatement *thenStmt = nullptr;
  if (!btype || then_expr->btype() == void_type())
    thenStmt = expression_statement(function, then_expr);
  else
    tempv = temporary_variable(function, nullptr,
                               then_expr->btype(), then_expr, false,
                               location, &thenStmt);
  thenBlock->stlist().push_back(thenStmt);

  if (else_expr) {
    Bstatement *elseStmt = nullptr;
    elseBlock = function->newBlock(function);
    if (!btype || else_expr->btype() == void_type()) {
      elseStmt = expression_statement(function, else_expr);
    } else {
      // Capture "else_expr" into temporary. Type needs to agree with
      // then_expr if then_expr had non-void type.
      if (!tempv) {
        tempv = temporary_variable(function, nullptr,
                                   else_expr->btype(), else_expr, false,
                                   location, &elseStmt);
      } else {
        assert(then_expr->btype() == else_expr->btype());
        Bexpression *varExpr = var_expression(tempv, VE_lvalue, location);
        elseStmt = assignment_statement(function, varExpr, else_expr, location);
      }
    }
    elseBlock->stlist().push_back(elseStmt);
  }

  // Wrap up and return the result
  Bstatement *ifStmt = if_statement(function, condition,
                                    thenBlock, elseBlock, location);
  llvm::Value *nilval = nullptr;
  Bexpression *rval = (tempv ?
                       var_expression(tempv, VE_rvalue, location) :
                       makeValueExpression(nilval, void_type(), LocalScope,
                                           location));
  Bexpression *result = compound_expression(ifStmt, rval, location);
  return result;
}

// Return an expression for the unary operation OP EXPR.

Bexpression *Llvm_backend::unary_expression(Operator op, Bexpression *expr,
                                            Location location) {
  if (expr == errorExpression_.get())
    return errorExpression_.get();

  expr = resolveVarContext(expr);
  Btype *bt = expr->btype();

  switch (op) {
    case OPERATOR_MINUS: {
      // FIXME: for floating point zero expr should be -0.0
      return binary_expression(OPERATOR_MINUS, zero_expression(bt),
                               expr, location);
    }

    case OPERATOR_NOT: {
      LIRBuilder builder(context_, llvm::ConstantFolder());
      assert(bt == bool_type());

      // FIXME: is this additional compare-to-zero needed? Or can we be certain
      // that the value in question has a single bit set?
      llvm::Constant *zero = llvm::ConstantInt::get(bt->type(), 0);
      llvm::Value *cmp =
          builder.CreateICmpNE(expr->value(), zero, namegen("icmp"));
      Btype *lbt = makeAuxType(llvmBoolType_);
      Bexpression *cmpex =
          makeExpression(cmp, AppendInst, lbt, location, expr, nullptr);
      llvm::Constant *one = llvm::ConstantInt::get(llvmBoolType_, 1);
      llvm::Value *xorExpr = builder.CreateXor(cmp, one, namegen("xor"));
      Bexpression *notex =
          makeExpression(xorExpr, AppendInst, lbt, location, cmpex, nullptr);
      Bexpression *tobool = convert_expression(bool_type(), notex, location);
      return tobool;
    }
    case OPERATOR_XOR: {
      // ^x    bitwise complement    is m ^ x  with m = "all bits set to 1"
      //                             for unsigned x and  m = -1 for signed x
      assert(bt->type()->isIntegerTy());
      LIRBuilder builder(context_, llvm::ConstantFolder());
      llvm::Value *onesval = llvm::Constant::getAllOnesValue(bt->type());
      llvm::Value *xorExpr = builder.CreateXor(expr->value(), onesval,
                                               namegen("xor"));
      Bexpression *rval =
          makeExpression(xorExpr, AppendInst, bt, location, expr, nullptr);
      return rval;
      break;
    }
    default:
      assert(false && "unexpected unary opcode");
  }
  return nullptr;
}

static llvm::CmpInst::Predicate compare_op_to_pred(Operator op, llvm::Type *typ,
                                                   bool isUnsigned) {
  bool isFloat = typ->isFloatingPointTy();

  if (isFloat) {
    switch (op) {
    case OPERATOR_EQEQ:
      return llvm::CmpInst::Predicate::FCMP_OEQ;
    case OPERATOR_NOTEQ:
      return llvm::CmpInst::Predicate::FCMP_ONE;
    case OPERATOR_LT:
      return llvm::CmpInst::Predicate::FCMP_OLT;
    case OPERATOR_LE:
      return llvm::CmpInst::Predicate::FCMP_OLE;
    case OPERATOR_GT:
      return llvm::CmpInst::Predicate::FCMP_OGT;
    case OPERATOR_GE:
      return llvm::CmpInst::Predicate::FCMP_OGE;
    default:
      break;
    }
  } else {
    assert(!isUnsigned || typ->isIntegerTy());
    switch (op) {
    case OPERATOR_EQEQ:
      return llvm::CmpInst::Predicate::ICMP_EQ;
    case OPERATOR_NOTEQ:
      return llvm::CmpInst::Predicate::ICMP_NE;
    case OPERATOR_LT:
      return (isUnsigned ? llvm::CmpInst::Predicate::ICMP_ULT
                         : llvm::CmpInst::Predicate::ICMP_SLT);
    case OPERATOR_LE:
      return (isUnsigned ? llvm::CmpInst::Predicate::ICMP_ULE
                         : llvm::CmpInst::Predicate::ICMP_SLE);
    case OPERATOR_GT:
      return (isUnsigned ? llvm::CmpInst::Predicate::ICMP_UGT
                         : llvm::CmpInst::Predicate::ICMP_SGT);
    case OPERATOR_GE:
      return (isUnsigned ? llvm::CmpInst::Predicate::ICMP_UGE
                         : llvm::CmpInst::Predicate::ICMP_SGE);
    default:
      break;
    }
  }
  assert(false);
  return llvm::CmpInst::BAD_ICMP_PREDICATE;
}

std::pair<llvm::Value *, llvm::Value *>
Llvm_backend::convertForBinary(Bexpression *left, Bexpression *right)
{
  llvm::Value *leftVal = left->value();
  llvm::Value *rightVal = right->value();
  std::pair<llvm::Value *, llvm::Value *> rval =
      std::make_pair(leftVal, rightVal);

  llvm::Type *leftType = leftVal->getType();
  llvm::Type *rightType = rightVal->getType();
  if (leftType == rightType)
    return rval;

  // Case 1: nil op X
  if (llvm::isa<llvm::ConstantPointerNull>(leftVal) &&
      rightType->isPointerTy()) {
    BexprLIRBuilder builder(context_, left);
    std::string tag("cast");
    llvm::Value *bitcast = builder.CreateBitCast(leftVal, rightType, tag);
    rval.first = bitcast;
    return rval;
  }

  // Case 2: X op nil
  if (llvm::isa<llvm::ConstantPointerNull>(rightVal) &&
      leftType->isPointerTy()) {
    BexprLIRBuilder builder(context_, right);
    std::string tag("cast");
    llvm::Value *bitcast = builder.CreateBitCast(rightVal, leftType, tag);
    rval.second = bitcast;
    return rval;
  }

  return rval;
}

// Return an expression for the binary operation LEFT OP RIGHT.

Bexpression *Llvm_backend::binary_expression(Operator op, Bexpression *left,
                                             Bexpression *right,
                                             Location location) {
  if (left == errorExpression_.get() || right == errorExpression_.get())
    return errorExpression_.get();

  left = resolveVarContext(left);
  right = resolveVarContext(right);
  assert(left->value() && right->value());

  Btype *bltype = left->btype();
  Btype *brtype = right->btype();
  std::pair<llvm::Value *, llvm::Value *> converted =
      convertForBinary(left, right);
  llvm::Value *leftVal = converted.first;
  llvm::Value *rightVal = converted.second;
  llvm::Type *ltype = leftVal->getType();
  llvm::Type *rtype = rightVal->getType();
  assert(ltype == rtype);
  BIntegerType *blitype = bltype->castToBIntegerType();
  BIntegerType *britype = brtype->castToBIntegerType();
  assert((blitype == nullptr) == (britype == nullptr));
  bool isUnsigned = false;
  if (blitype) {
    assert(blitype->isUnsigned() == britype->isUnsigned());
    isUnsigned = blitype->isUnsigned();
  }
  LIRBuilder builder(context_, llvm::ConstantFolder());
  llvm::Value *val = nullptr;

  switch (op) {
  case OPERATOR_EQEQ:
  case OPERATOR_NOTEQ:
  case OPERATOR_LT:
  case OPERATOR_LE:
  case OPERATOR_GT:
  case OPERATOR_GE: {
    llvm::CmpInst::Predicate pred = compare_op_to_pred(op, ltype, isUnsigned);
    if (ltype->isFloatingPointTy())
      val = builder.CreateFCmp(pred, leftVal, rightVal, namegen("fcmp"));
    else
      val = builder.CreateICmp(pred, leftVal, rightVal, namegen("icmp"));
    Btype *bcmpt = makeAuxType(llvmBoolType_);
    // gen compare...
    Bexpression *cmpex =
        makeExpression(val, AppendInst, bcmpt, location, left, right, nullptr);
    // ... widen to go boolean type
    return convert_expression(bool_type(), cmpex, location);
  }
  case OPERATOR_MINUS: {
    if (ltype->isFloatingPointTy())
      val = builder.CreateFSub(leftVal, rightVal, namegen("fsub"));
    else
      val = builder.CreateSub(leftVal, rightVal, namegen("sub"));
    break;
  }
  case OPERATOR_PLUS: {
    if (ltype->isFloatingPointTy())
      val = builder.CreateFAdd(leftVal, rightVal, namegen("fadd"));
    else
      val = builder.CreateAdd(leftVal, rightVal, namegen("add"));
    break;
  }
  case OPERATOR_OROR: {
    // Note that the FE will have already expanded out || in a control
    // flow context (short circuiting)
    assert(!ltype->isFloatingPointTy());
    val = builder.CreateOr(leftVal, rightVal, namegen("ior"));
    break;
  }
  case OPERATOR_ANDAND: {
    // Note that the FE will have already expanded out && in a control
    // flow context (short circuiting)
    assert(!ltype->isFloatingPointTy());
    val = builder.CreateAnd(leftVal, rightVal, namegen("iand"));
    break;
  }
  case OPERATOR_XOR: {
    // Note that the FE will have already expanded out && in a control
    // flow context (short circuiting)
    assert(!ltype->isFloatingPointTy() && !rtype->isFloatingPointTy());
    val = builder.CreateXor(leftVal, rightVal, namegen("xor"));
    break;
  }
  default:
    std::cerr << "Op " << op << "unhandled\n";
    assert(false);
  }

  return makeExpression(val, AppendInst, bltype, location,
                        left, right, nullptr);
}

bool
Llvm_backend::valuesAreConstant(const std::vector<Bexpression *> &vals)
{
  for (auto val : vals) {
    if (val->compositeInitPending()) {
      CompositeInitContext &cic = val->compositeInitContext();
      if (! valuesAreConstant(cic.elementExpressions()))
        return false;
    } else if (!llvm::isa<llvm::Constant>(val->value())) {
      return false;
      break;
    }
  }
  return true;
}

// Return an expression that constructs BTYPE with VALS.

Bexpression *Llvm_backend::constructor_expression(
    Btype *btype, const std::vector<Bexpression *> &vals, Location location) {
  if (btype == errorType_ || exprVectorHasError(vals))
    return errorExpression_.get();

  llvm::Type *llt = btype->type();
  assert(llt->isStructTy());
  llvm::StructType *llst = llvm::cast<llvm::StructType>(llt);

  // Not sure if we can count on this, may have to take it out
  unsigned numElements = llst->getNumElements();
  assert(vals.size() == numElements);

  // Constant values?
  bool isConstant = valuesAreConstant(vals);
  std::vector<unsigned long> indexes;
  for (unsigned long ii = 0; ii < vals.size(); ++ii)
    indexes.push_back(ii);
  if (isConstant)
    return makeConstCompositeExpr(btype, llst, numElements,
                                  indexes, vals, location);
  else
    return makeDelayedCompositeExpr(btype, llst, numElements,
                                    indexes, vals, location);
}

Bexpression *
Llvm_backend::makeDelayedCompositeExpr(Btype *btype,
                                       llvm::CompositeType *llct,
                                       unsigned numElements,
                                       const std::vector<unsigned long> &indexes,
                                       const std::vector<Bexpression *> &vals,
                                       Location location) {
  unsigned long nvals = vals.size();
  std::set<unsigned long> touched;
  std::vector<Bexpression *> init_vals(numElements);
  for (unsigned ii = 0; ii < indexes.size(); ++ii) {
    auto idx = indexes[ii];
    if (numElements != nvals)
      touched.insert(idx);
    init_vals[idx] = vals[ii];
  }
  if (numElements != nvals) {
    for (unsigned long ii = 0; ii < numElements; ++ii) {
      Btype *bElemTyp = elementTypeByIndex(btype, ii);
      if (touched.find(ii) == touched.end())
        init_vals[ii] = zero_expression(bElemTyp);
    }
  }
  llvm::Value *nilval = nullptr;
  Bexpression *ccon = makeValueExpression(nilval, btype, LocalScope, location);
  ccon->setCompositeInit(init_vals);
  return ccon;
}

Bexpression *
Llvm_backend::makeConstCompositeExpr(Btype *btype,
                                     llvm::CompositeType *llct,
                                     unsigned numElements,
                                     const std::vector<unsigned long> &indexes,
                                     const std::vector<Bexpression *> &vals,
                                     Location location) {
  unsigned long nvals = vals.size();
  std::set<unsigned long> touched;
  llvm::SmallVector<llvm::Constant *, 64> llvals(numElements);
  for (unsigned ii = 0; ii < indexes.size(); ++ii) {
    auto idx = indexes[ii];
    if (numElements != nvals)
      touched.insert(idx);
    Bexpression *bex = vals[ii];
    llvm::Constant *con = llvm::cast<llvm::Constant>(bex->value());
    llvm::Type *elt = llct->getTypeAtIndex(ii);
    if (elt != con->getType())
      con = llvm::ConstantExpr::getBitCast(con, elt);

    llvals[idx] = con;
  }
  if (numElements != nvals) {
    for (unsigned long ii = 0; ii < numElements; ++ii) {
      if (touched.find(ii) == touched.end()) {
        llvm::Type *elt = llct->getTypeAtIndex(ii);
        llvals[ii] = llvm::Constant::getNullValue(elt);
      }
    }
  }
  llvm::Value *scon;
  if (llct->isStructTy()) {
    llvm::StructType *llst = llvm::cast<llvm::StructType>(llct);
    scon = llvm::ConstantStruct::get(llst, llvals);
  } else {
    llvm::ArrayType *llat = llvm::cast<llvm::ArrayType>(llct);
    scon = llvm::ConstantArray::get(llat, llvals);
  }
  Bexpression *bcon = makeValueExpression(scon, btype, GlobalScope, location);
  return bcon;
}

Bexpression *Llvm_backend::array_constructor_expression(
    Btype *array_btype, const std::vector<unsigned long> &indexes,
    const std::vector<Bexpression *> &vals, Location location) {
  if (array_btype == errorType_ || exprVectorHasError(vals))
    return errorExpression_.get();

  llvm::Type *llt = array_btype->type();
  assert(llt->isArrayTy());
  llvm::ArrayType *llat = llvm::cast<llvm::ArrayType>(llt);
  unsigned numElements = llat->getNumElements();

  // frontend should be enforcing this
  assert(indexes.size() == vals.size());

  // Constant values?
  if (valuesAreConstant(vals))
    return makeConstCompositeExpr(array_btype, llat, numElements,
                                  indexes, vals, location);
  else
    return makeDelayedCompositeExpr(array_btype, llat, numElements,
                                    indexes, vals, location);
}

// Return an expression for the address of BASE[INDEX].

Bexpression *Llvm_backend::pointer_offset_expression(Bexpression *base,
                                                     Bexpression *index,
                                                     Location location) {
  assert(false && "Llvm_backend::pointer_offset_expression not yet impl");
  return nullptr;
}

// Return an expression representing ARRAY[INDEX]

Bexpression *Llvm_backend::array_index_expression(Bexpression *barray,
                                                  Bexpression *index,
                                                  Location location) {

  if (barray == errorExpression_.get() || index == errorExpression_.get())
    return errorExpression_.get();

  // FIXME: add array bounds checking

  index = resolveVarContext(index);

  // Construct an appropriate GEP
  llvm::Type *llt = barray->btype()->type();
  assert(llt->isArrayTy());
  llvm::ArrayType *llat = llvm::cast<llvm::ArrayType>(llt);
  llvm::Value *gep =
      makeArrayIndexGEP(llat, index->value(), barray->value());

  // Wrap in a Bexpression, encapsulating contents of source exprs
  Btype *bet = elementTypeByIndex(barray->btype(), 0);
  Bexpression *rval = makeExpression(gep, AppendInst, bet, location,
                                     barray, index, nullptr);
  if (barray->varExprPending())
    rval->setVarExprPending(barray->varContext());

  std::string tag(barray->tag());
  tag += ".index";
  rval->setTag(tag);

  // We're done
  return rval;
}

// Create an expression for a call to FN_EXPR with FN_ARGS.
Bexpression *
Llvm_backend::call_expression(Bexpression *fn_expr,
                              const std::vector<Bexpression *> &fn_args,
                              Bexpression *chain_expr, Location location) {
  if (fn_expr == errorExpression_.get() || exprVectorHasError(fn_args) ||
      chain_expr == errorExpression_.get())
    return errorExpression_.get();

  // Resolve fcn
  fn_expr = resolveVarContext(fn_expr);

  // Pick out function type
  BPointerType *bpft = fn_expr->btype()->castToBPointerType();
  BFunctionType *bft = bpft->toType()->castToBFunctionType();
  assert(bft);
  const std::vector<Btype *> &paramTypes = bft->paramTypes();

  // Unpack / resolve arguments
  llvm::SmallVector<llvm::Value *, 64> llargs;
  std::vector<Bexpression *> resolvedArgs;
  resolvedArgs.push_back(fn_expr);
  for (unsigned idx = 0; idx < fn_args.size(); ++idx) {
    Bexpression *resarg = resolveVarContext(fn_args[idx]);
    resolvedArgs.push_back(resarg);
    Btype *paramTyp = paramTypes[idx];
    llvm::Value *val = resarg->value();
    if (val->getType()->isPointerTy())
      val = convertForAssignment(resarg, paramTyp->type());
    llargs.push_back(val);
  }

  // Expect pointer-to-function type here
  assert(fn_expr->btype()->type()->isPointerTy());
  llvm::PointerType *pt =
      llvm::cast<llvm::PointerType>(fn_expr->btype()->type());
  llvm::FunctionType *llft =
      llvm::cast<llvm::FunctionType>(pt->getElementType());

  // Return type
  Btype *rbtype = functionReturnType(fn_expr->btype());

  // FIXME: create struct to hold result from multi-return call
  bool isvoid = llft->getReturnType()->isVoidTy();
  std::string callname(isvoid ? "" : namegen("call"));
  llvm::CallInst *call =
      llvm::CallInst::Create(llft, fn_expr->value(), llargs, callname);
  Bexpression *rval =
      makeExpression(call, AppendInst, rbtype, location, resolvedArgs);
  return rval;
}

// Return an expression that allocates SIZE bytes on the stack.

Bexpression *Llvm_backend::stack_allocation_expression(int64_t size,
                                                       Location location) {
  assert(false && "Llvm_backend::stack_allocation_expression not yet impl");
  return nullptr;
}

Bstatement *Llvm_backend::error_statement() { return errorStatement_.get(); }

// An expression as a statement.

Bstatement *Llvm_backend::expression_statement(Bfunction *bfunction,
                                               Bexpression *expr) {
  if (expr == errorExpression_.get())
    return errorStatement_.get();
  ExprListStatement *els = new ExprListStatement(bfunction);
  els->appendExpression(resolve(expr, bfunction));
  CHKTREE(els);
  return els;
}

// Variable initialization.

Bstatement *Llvm_backend::init_statement(Bfunction *bfunction,
                                         Bvariable *var, Bexpression *init) {
  if (var == errorVariable_.get() || init == errorExpression_.get() ||
      bfunction == errorFunction_.get())
    return errorStatement_.get();
  if (init) {
    if (init->compositeInitPending()) {
      init = resolveCompositeInit(init, bfunction, var->value());
      ExprListStatement *els = new ExprListStatement(bfunction);
      els->appendExpression(init);
      CHKTREE(els);
      return els;
    }
    init = resolveVarContext(init);
  } else {
    init = zero_expression(var->btype());
  }
  Bstatement *st = makeAssignment(bfunction, var->value(),
                                  nullptr, init, Location());
  CHKTREE(st);
  return st;
}

bool Llvm_backend::isFuncDescriptorType(llvm::Type *typ)
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

bool Llvm_backend::isPtrToFuncDescriptorType(llvm::Type *typ)
{
  if (! typ->isPointerTy())
    return false;
  llvm::PointerType *pt = llvm::cast<llvm::PointerType>(typ);
  return isFuncDescriptorType(pt->getElementType());
}

bool Llvm_backend::isPtrToFuncType(llvm::Type *typ)
{
  if (! typ->isPointerTy())
    return false;
  llvm::PointerType *pt = llvm::cast<llvm::PointerType>(typ);
  return pt->getElementType()->isFunctionTy();
}

llvm::Value *Llvm_backend::convertForAssignment(Bexpression *src,
                                                llvm::Type *dstToType)
{
  llvm::Type *srcType = src->value()->getType();

  if (dstToType == srcType)
    return src->value();

  // Case 1: handle discrepancies between representations of function
  // descriptors. All front end function descriptor types are structs
  // with a single field, however this field can sometimes be a pointer
  // to function, and sometimes it can be of uintptr type.
  bool srcPtrToFD = isPtrToFuncDescriptorType(srcType);
  bool dstPtrToFD = isPtrToFuncDescriptorType(dstToType);
  if (srcPtrToFD && dstPtrToFD) {
    BexprLIRBuilder builder(context_, src);
    std::string tag("cast");
    llvm::Value *bitcast = builder.CreateBitCast(src->value(), dstToType, tag);
    return bitcast;
  }

  // Case 2: handle polymorphic nil pointer expressions-- these are
  // generated without a type initially, so we need to convert them
  // to the appropriate type if they appear in an assignment context.
  if (src == nil_pointer_expression()) {
    BexprLIRBuilder builder(context_, src);
    std::string tag("cast");
    llvm::Value *bitcast = builder.CreateBitCast(src->value(), dstToType, tag);
    return bitcast;
  }

  return src->value();
}

Bstatement *Llvm_backend::makeAssignment(Bfunction *function,
                                         llvm::Value *lval, Bexpression *lhs,
                                         Bexpression *rhs, Location location) {
  assert(lval->getType()->isPointerTy());
  llvm::PointerType *pt = llvm::cast<llvm::PointerType>(lval->getType());
  llvm::Value *rval = rhs->value();

  // These cases should have been handled in the caller
  assert(!rhs->varExprPending() && !rhs->compositeInitPending());

  // Work around type inconsistencies in gofrontend.
  llvm::Type *dstToType = pt->getElementType();
  rval = convertForAssignment(rhs, dstToType);

  // At this point the types should agree
  assert(rval->getType() == pt->getElementType());

  // FIXME: alignment?
  llvm::Instruction *si = new llvm::StoreInst(rval, lval);

  Bexpression *stexp = makeExpression(si, AppendInst, rhs->btype(), location,
                                      rhs, lhs, nullptr);
  ExprListStatement *els = stmtFromExpr(function, stexp);
  return els;
}

// Assignment.

Bstatement *Llvm_backend::assignment_statement(Bfunction *bfunction,
                                               Bexpression *lhs,
                                               Bexpression *rhs,
                                               Location location) {
  if (lhs == errorExpression_.get() || rhs == errorExpression_.get() ||
      bfunction == errorFunction_.get())
    return errorStatement_.get();
  Bexpression *lhs2 = resolveVarContext(lhs);
  Bexpression *rhs2 = rhs;
  if (rhs->compositeInitPending())
    rhs2 = resolveCompositeInit(rhs, bfunction, lhs2->value());
  Bexpression *rhs3 = resolveVarContext(rhs2);
  Bstatement *st = makeAssignment(bfunction, lhs->value(),
                                  lhs2, rhs3, location);
  CHKTREE(st);
  return st;
}

Bstatement*
Llvm_backend::return_statement(Bfunction *bfunction,
                               const std::vector<Bexpression *> &vals,
                               Location location) {
  if (bfunction == error_function() || exprVectorHasError(vals))
    return errorStatement_.get();

  // Temporary
  assert(vals.size() == 1);

  // For the moment return instructions are going to have null type,
  // since their values should not be feeding into anything else (and
  // since Go functions can return multiple values).
  Btype *btype = nullptr;

  Bexpression *toret = resolve(vals[0], bfunction);
  llvm::ReturnInst *ri = llvm::ReturnInst::Create(context_, toret->value());
  Bexpression *rexp = makeExpression(ri, AppendInst, btype, location,
                                     toret, nullptr);
  ExprListStatement *els = stmtFromExpr(bfunction, rexp);
  return els;
}

// Create a statement that attempts to execute BSTAT and calls EXCEPT_STMT if an
// error occurs.  EXCEPT_STMT may be NULL.  FINALLY_STMT may be NULL and if not
// NULL, it will always be executed.  This is used for handling defers in Go
// functions.  In C++, the resulting code is of this form:
//   try { BSTAT; } catch { EXCEPT_STMT; } finally { FINALLY_STMT; }

Bstatement *Llvm_backend::exception_handler_statement(Bstatement *bstat,
                                                      Bstatement *except_stmt,
                                                      Bstatement *finally_stmt,
                                                      Location location) {
  assert(false && "Llvm_backend::exception_handler_statement not yet impl");
  return nullptr;
}

// If.

Bstatement *Llvm_backend::if_statement(Bfunction *bfunction,
                                       Bexpression *condition,
                                       Bblock *then_block, Bblock *else_block,
                                       Location location) {
  if (condition == errorExpression_.get())
    return errorStatement_.get();
  condition = resolve(condition, bfunction);
  assert(then_block);
  Btype *bt = makeAuxType(llvmBoolType_);
  Bexpression *conv = convert_expression(bt, condition, location);
  IfPHStatement *ifst =
      new IfPHStatement(bfunction, conv, then_block, else_block, location);
  CHKTREE(ifst);
  return ifst;
}

// Switch.

Bstatement *Llvm_backend::switch_statement(
    Bfunction *function, Bexpression *value,
    const std::vector<std::vector<Bexpression *>> &cases,
    const std::vector<Bstatement *> &statements, Location switch_location) {
  assert(false && "Llvm_backend::switch_statement not yet impl");
  return nullptr;
}

// Pair of statements.

Bstatement *Llvm_backend::compound_statement(Bstatement *s1, Bstatement *s2) {
  assert(!s1->function() || !s2->function() ||
         s1->function() == s2->function());
  CompoundStatement *st = new CompoundStatement(s1->function());
  std::vector<Bstatement *> &stlist = st->stlist();
  stlist.push_back(s1);
  stlist.push_back(s2);
  CHKTREE(st);
  return st;
}

// List of statements.

Bstatement *
Llvm_backend::statement_list(const std::vector<Bstatement *> &statements) {
  Bfunction *function =
      (statements.size() != 0 ? statements[0]->function() : nullptr);
  CompoundStatement *cst = new CompoundStatement(function);
  std::vector<Bstatement *> &stlist = cst->stlist();
  for (auto st : statements)
    stlist.push_back(st);
  CHKTREE(cst);
  return cst;
}

Bblock *Llvm_backend::block(Bfunction *function, Bblock *enclosing,
                            const std::vector<Bvariable *> &vars,
                            Location start_location, Location) {
  assert(function);

  // FIXME: record debug location

  // Create new Bblock
  Bblock *bb = new Bblock(function);
  function->addBlock(bb);

  // Mark start of lifetime for each variable
  // for (auto var : vars) {
  // Not yet implemented
  // }

  return bb;
}

// Add statements to a block.

void Llvm_backend::block_add_statements(
    Bblock *bblock, const std::vector<Bstatement *> &statements) {
  for (auto st : statements)
    if (st == errorStatement_.get())
      return;
  assert(bblock);
  for (auto st : statements)
    bblock->stlist().push_back(st);
  CHKTREE(bblock);
}

// Return a block as a statement.

Bstatement *Llvm_backend::block_statement(Bblock *bblock) {
  CHKTREE(bblock);
  return bblock; // class Bblock inherits from CompoundStatement
}

// Helper routine for creating module-scope variables (static, global, etc).

Bvariable *
Llvm_backend::makeModuleVar(Btype *btype,
                            const std::string &name,
                            const std::string &asm_name,
                            Location location,
                            ModVarConstant isConstant,
                            ModVarSec inUniqueSection,
                            ModVarComdat inComdat,
                            ModVarVis isHiddenVisibility,
                            llvm::GlobalValue::LinkageTypes linkage,
                            llvm::Constant *initializer,
                            unsigned alignment)
{
  if (btype == errorType_)
    return errorVariable_.get();

#if 0
  // FIXME: add code to insure non-zero size
  assert(datalayout_.getTypeSizeInBits(btype->type()) != 0);
#endif

  // FIXME: add support for this
  assert(inUniqueSection == MV_DefaultSection);

  // FIXME: add support for this
  assert(inComdat == MV_NotInComdat);

  // FIXME: add DIGlobalVariable to debug info for this variable

  llvm::Constant *init = nullptr;
  llvm::GlobalVariable *glob = new llvm::GlobalVariable(
      *module_.get(), btype->type(), isConstant == MV_Constant,
      linkage, init, asm_name);
  if (isHiddenVisibility == MV_HiddenVisibility)
    glob->setVisibility(llvm::GlobalValue::HiddenVisibility);
  if (alignment)
    glob->setAlignment(alignment);
  if (initializer)
    glob->setInitializer(initializer);
  bool addressTaken = true; // for now
  Bvariable *bv =
      new Bvariable(btype, location, name, GlobalVar, addressTaken, glob);
  assert(valueVarMap_.find(bv->value()) == valueVarMap_.end());
  valueVarMap_[bv->value()] = bv;
  return bv;
}

// Make a global variable.

Bvariable *Llvm_backend::global_variable(const std::string &var_name,
                                         const std::string &asm_name,
                                         Btype *btype,
                                         bool is_external,
                                         bool is_hidden,
                                         bool in_unique_section,
                                         Location location) {
#if 0
  llvm::GlobalValue::LinkageTypes linkage =
      (is_external || is_hidden ? llvm::GlobalValue::ExternalLinkage
       : llvm::GlobalValue::InternalLinkage);
#endif
  llvm::GlobalValue::LinkageTypes linkage = llvm::GlobalValue::ExternalLinkage;

  ModVarSec inUniqSec =
      (in_unique_section ? MV_UniqueSection : MV_DefaultSection);
  ModVarVis varVis =
      (is_hidden ? MV_HiddenVisibility : MV_DefaultVisibility);
  Bvariable *gvar =
      makeModuleVar(btype, var_name, asm_name, location,
                    MV_NonConstant, inUniqSec, MV_NotInComdat,
                    varVis, linkage, nullptr);
  return gvar;
}

// Set the initial value of a global variable.

void Llvm_backend::global_variable_set_init(Bvariable *var, Bexpression *expr) {
  if (var == errorVariable_.get() || expr == errorExpression_.get())
    return;
  assert(llvm::isa<llvm::GlobalVariable>(var->value()));
  llvm::GlobalVariable *gvar = llvm::cast<llvm::GlobalVariable>(var->value());

  if (expr->compositeInitPending())
    expr = resolveCompositeInit(expr, nullptr, gvar);

  assert(llvm::isa<llvm::Constant>(expr->value()));
  llvm::Constant *econ = llvm::cast<llvm::Constant>(expr->value());

  gvar->setInitializer(econ);
}

Bvariable *Llvm_backend::error_variable() { return errorVariable_.get(); }

// Make a local variable.

Bvariable *Llvm_backend::local_variable(Bfunction *function,
                                        const std::string &name,
                                        Btype *btype,
                                        bool is_address_taken,
                                        Location location) {
  assert(function);
  if (btype == errorType_ || function == error_function())
    return errorVariable_.get();
  return function->local_variable(name, btype, is_address_taken, location);
}

// Make a function parameter variable.

Bvariable *Llvm_backend::parameter_variable(Bfunction *function,
                                            const std::string &name,
                                            Btype *btype, bool is_address_taken,
                                            Location location) {
  assert(function);
  if (btype == errorType_ || function == error_function())
    return errorVariable_.get();
  return function->parameter_variable(name, btype,
                                      is_address_taken, location);
}

// Make a static chain variable.

Bvariable *Llvm_backend::static_chain_variable(Bfunction *function,
                                               const std::string &name,
                                               Btype *btype,
                                               Location location) {
  assert(false && "Llvm_backend::static_chain_variable not yet impl");
  return nullptr;
}

// Make a temporary variable.

Bvariable *Llvm_backend::temporary_variable(Bfunction *function,
                                            Bblock *bblock,
                                            Btype *btype,
                                            Bexpression *binit,
                                            bool is_address_taken,
                                            Location location,
                                            Bstatement **pstatement) {
  if (binit == errorExpression_.get())
    return errorVariable_.get();
  std::string tname(namegen("tmpv"));
  Bvariable *tvar = local_variable(function, tname, btype,
                                   is_address_taken, location);
  if (tvar == errorVariable_.get())
    return tvar;
  Bstatement *is = init_statement(function, tvar, binit);
  *pstatement = is;
  return tvar;
}

// Create an implicit variable that is compiler-defined.  This is used when
// generating GC root variables and storing the values of a slice initializer.

Bvariable *Llvm_backend::implicit_variable(const std::string &name,
                                           const std::string &asm_name,
                                           Btype *btype,
                                           bool is_hidden,
                                           bool is_constant,
                                           bool is_common,
                                           int64_t ialignment) {
  if (btype == errorType_)
    return errorVariable_.get();

  // Vett alignment
  assert(ialignment >= 0);
  assert(ialignment < 1<<30);
  unsigned alignment = static_cast<unsigned>(ialignment);

  // Common + hidden makes no sense
  assert(!(is_hidden && is_common));

  llvm::GlobalValue::LinkageTypes linkage =
      (is_hidden ? llvm::GlobalValue::ExternalLinkage
       : llvm::GlobalValue::WeakODRLinkage);

  // bool isComdat = is_common;
  ModVarComdat inComdat = MV_NotInComdat; // for now
  ModVarSec inUniqSec = MV_DefaultSection; // override for now
  ModVarVis varVis =
      (is_hidden ? MV_HiddenVisibility : MV_DefaultVisibility);
  ModVarConstant isConst = (is_constant ? MV_Constant : MV_NonConstant);

  Bvariable *gvar =
      makeModuleVar(btype, name, asm_name, Location(),
                    isConst, inUniqSec, inComdat,
                    varVis, linkage, nullptr, alignment);
  return gvar;
}

// Set the initalizer for a variable created by implicit_variable.
// This is where we finish compiling the variable.

void Llvm_backend::implicit_variable_set_init(Bvariable *var,
                                              const std::string &,
                                              Btype *type,
                                              bool, bool, bool is_common,
                                              Bexpression *init) {

  if (init != nullptr && init == errorExpression_.get())
    return;
  if (var == errorVariable_.get())
    return;
  if (!init)
    init = zero_expression(type);
  global_variable_set_init(var, init);
}

// Return a reference to an implicit variable defined in another package.

Bvariable *Llvm_backend::implicit_variable_reference(const std::string &name,
                                                     const std::string &asmname,
                                                     Btype *btype) {
  assert(false && "Llvm_backend::implicit_variable_reference not yet impl");
  return nullptr;
}

// Create a named immutable initialized data structure.

Bvariable *Llvm_backend::immutable_struct(const std::string &name,
                                          const std::string &asm_name,
                                          bool is_hidden,
                                          bool is_common,
                                          Btype *btype,
                                          Location location) {
  if (btype == errorType_)
    return errorVariable_.get();

  // Common + hidden makes no sense
  assert(!(is_hidden && is_common));

  llvm::GlobalValue::LinkageTypes linkage =
      (is_hidden ? llvm::GlobalValue::ExternalLinkage
       : llvm::GlobalValue::WeakODRLinkage);

  ModVarComdat inComdat = MV_NotInComdat; // for now
  ModVarSec inUniqueSec = MV_DefaultSection; // override for now
  ModVarVis varVis =
      (is_hidden ? MV_HiddenVisibility : MV_DefaultVisibility);
  Bvariable *gvar =
      makeModuleVar(btype, name, asm_name, location,
                    MV_Constant, inUniqueSec, inComdat,
                    varVis, linkage, nullptr);
  return gvar;
}

// Set the initializer for a variable created by immutable_struct.
// This is where we finish compiling the variable.

void Llvm_backend::immutable_struct_set_init(Bvariable *var,
                                             const std::string &,
                                             bool is_hidden,
                                             bool is_common,
                                             Btype *,
                                             Location,
                                             Bexpression *initializer) {
  if (var == errorVariable_.get() || initializer == errorExpression_.get())
    return;

  assert(llvm::isa<llvm::GlobalVariable>(var->value()));
  llvm::GlobalVariable *gvar = llvm::cast<llvm::GlobalVariable>(var->value());
  assert(llvm::isa<llvm::Constant>(var->value()));
  llvm::Constant *econ = llvm::cast<llvm::Constant>(initializer->value());
  gvar->setInitializer(econ);
}

// Return a reference to an immutable initialized data structure
// defined in another package.

Bvariable *Llvm_backend::immutable_struct_reference(const std::string &name,
                                                    const std::string &asmname,
                                                    Btype *btype,
                                                    Location location) {
  if (btype == errorType_)
    return errorVariable_.get();

  // FIXME: add code to insure non-zero size
  assert(datalayout_.getTypeSizeInBits(btype->type()) != 0);

  // FIXME: add DIGlobalVariable to debug info for this variable

  llvm::GlobalValue::LinkageTypes linkage = llvm::GlobalValue::ExternalLinkage;
  bool isConstant = true;
  llvm::Constant *init = nullptr;
  llvm::GlobalVariable *glob = new llvm::GlobalVariable(
      *module_.get(), btype->type(), isConstant, linkage, init, asmname);
  Bvariable *bv =
      new Bvariable(btype, location, name, GlobalVar, false, glob);
  assert(valueVarMap_.find(bv->value()) == valueVarMap_.end());
  valueVarMap_[bv->value()] = bv;
  return bv;
}

// Make a label.

Blabel *Llvm_backend::label(Bfunction *function,
                            const std::string &name,
                            Location location) {
  assert(function);
  return function->newLabel();
}

// Make a statement which defines a label.

Bstatement *Llvm_backend::label_definition_statement(Blabel *label) {
  Bfunction *function = label->function();
  return function->newLabelDefStatement(label);
}

// Make a goto statement.

Bstatement *Llvm_backend::goto_statement(Blabel *label, Location location) {
  Bfunction *function = label->function();
  return function->newGotoStatement(label, location);
}

// Get the address of a label.

Bexpression *Llvm_backend::label_address(Blabel *label, Location location) {
  assert(false);
}

Bfunction *Llvm_backend::error_function() { return errorFunction_.get(); }

// Declare or define a new function.

Bfunction *Llvm_backend::function(Btype *fntype, const std::string &name,
                                  const std::string &asm_name, bool is_visible,
                                  bool is_declaration, bool is_inlinable,
                                  bool disable_split_stack,
                                  bool in_unique_section, Location location) {
  if (fntype == errorType_)
    return errorFunction_.get();
  llvm::Twine fn(name);
  llvm::FunctionType *fty = llvm::cast<llvm::FunctionType>(fntype->type());
  llvm::GlobalValue::LinkageTypes linkage = llvm::GlobalValue::ExternalLinkage;
  llvm::Function *fcn = llvm::Function::Create(fty, linkage, fn, module_.get());

  // visibility
  if (!is_visible)
    fcn->setVisibility(llvm::GlobalValue::HiddenVisibility);

  // inline/noinline
  if (!is_inlinable)
    fcn->addFnAttr(llvm::Attribute::NoInline);

  BFunctionType *fcnType = fntype->castToBFunctionType();
  assert(fcnType);
  Bfunction *bfunc = new Bfunction(fcn, fcnType, asm_name);

  // TODO: unique section support. llvm::GlobalObject has support for
  // setting COMDAT groups and section names, but nothing to manage how
  // section names are created or doled out as far as I can tell, need
  // to look a little more closely at how -ffunction-sections is implemented
  // for clang/LLVM.
  assert(!in_unique_section || is_declaration);

  if (disable_split_stack)
    bfunc->setSplitStack(Bfunction::NoSplit);

  functions_.push_back(bfunc);

  return bfunc;
}

// Create a statement that runs all deferred calls for FUNCTION.  This should
// be a statement that looks like this in C++:
//   finish:
//     try { UNDEFER; } catch { CHECK_DEFER; goto finish; }

Bstatement *Llvm_backend::function_defer_statement(Bfunction *function,
                                                   Bexpression *undefer,
                                                   Bexpression *defer,
                                                   Location location) {
  assert(false && "Llvm_backend::function_defer_statement not yet impl");
  return nullptr;
}

// Record PARAM_VARS as the variables to use for the parameters of FUNCTION.
// This will only be called for a function definition.

bool Llvm_backend::function_set_parameters(
    Bfunction *function, const std::vector<Bvariable *> &param_vars) {
  // At the moment this is a no-op.
  return true;
}

//
// Helper class for assigning instructions to LLVM basic blocks
// and materializing control transfers.
//
class GenBlocks {
public:
  GenBlocks(llvm::LLVMContext &context, Llvm_backend *be, Bfunction *function)
      : context_(context), be_(be), function_(function) {}

  llvm::BasicBlock *walk(Bstatement *stmt, llvm::BasicBlock *curblock);
  Bfunction *function() { return function_; }
  llvm::BasicBlock *genIf(IfPHStatement *ifst, llvm::BasicBlock *curblock);

private:
  llvm::BasicBlock *getBlockForLabel(LabelId lab);
  llvm::BasicBlock *walkExpr(llvm::BasicBlock *curblock, Bexpression *expr);

private:
  llvm::LLVMContext &context_;
  Llvm_backend *be_;
  Bfunction *function_;
  std::map<LabelId, llvm::BasicBlock *> labelmap_;
};

llvm::BasicBlock *GenBlocks::getBlockForLabel(LabelId lab) {
  auto it = labelmap_.find(lab);
  if (it != labelmap_.end())
    return it->second;
  std::string lname = be_->namegen("label", lab);
  llvm::Function *func = function()->function();
  llvm::BasicBlock *bb = llvm::BasicBlock::Create(context_, lname, func);
  labelmap_[lab] = bb;
  return bb;
}

llvm::BasicBlock *GenBlocks::walkExpr(llvm::BasicBlock *curblock,
                                      Bexpression *expr)
{
  if (expr->stmt())
    curblock = walk(expr->stmt(), curblock);
  for (auto inst : expr->instructions()) {
    curblock->getInstList().push_back(inst);
  }
  return curblock;
}

llvm::BasicBlock *GenBlocks::genIf(IfPHStatement *ifst,
                                   llvm::BasicBlock *curblock) {

  // Walk condition first
  curblock = walkExpr(curblock, ifst->cond());

  // Create true block
  std::string tname = be_->namegen("then");
  llvm::Function *func = function()->function();
  llvm::BasicBlock *tblock = llvm::BasicBlock::Create(context_, tname, func);

  // Push fallthrough block
  std::string ftname = be_->namegen("fallthrough");
  llvm::BasicBlock *ft = llvm::BasicBlock::Create(context_, ftname, func);

  // Create false block if present
  llvm::BasicBlock *fblock = ft;
  if (ifst->falseStmt()) {
    std::string fname = be_->namegen("else");
    fblock = llvm::BasicBlock::Create(context_, fname, func);
  }

  // Insert conditional branch into current block
  llvm::Value *cval = ifst->cond()->value();
  llvm::BranchInst::Create(tblock, fblock, cval, curblock);

  // Visit true block
  llvm::BasicBlock *tsucc = walk(ifst->trueStmt(), tblock);
  if (! tsucc->getTerminator())
    llvm::BranchInst::Create(ft, tsucc);

  // Walk false block if present
  if (ifst->falseStmt()) {
    llvm::BasicBlock *fsucc = fsucc = walk(ifst->falseStmt(), fblock);
    if (! fsucc->getTerminator())
      llvm::BranchInst::Create(ft, fsucc);
  }

  return ft;
}

llvm::BasicBlock *GenBlocks::walk(Bstatement *stmt,
                                  llvm::BasicBlock *curblock) {
  switch (stmt->flavor()) {
  case Bstatement::ST_Compound: {
    CompoundStatement *cst = stmt->castToCompoundStatement();
    for (auto st : cst->stlist())
      curblock = walk(st, curblock);
    break;
  }
  case Bstatement::ST_ExprList: {
    ExprListStatement *elst = stmt->castToExprListStatement();
    for (auto expr : elst->expressions())
      curblock = walkExpr(curblock, expr);
    break;
  }
  case Bstatement::ST_IfPlaceholder: {
    IfPHStatement *ifst = stmt->castToIfPHStatement();
    curblock = genIf(ifst, curblock);
    break;
  }
  case Bstatement::ST_Goto: {
    GotoStatement *gst = stmt->castToGotoStatement();
    llvm::BasicBlock *lbb = getBlockForLabel(gst->targetLabel());
    llvm::BranchInst::Create(lbb, curblock);
    // FIXME: don't bother generating an orphan block, just zero
    // the current block and don't emit whatever statically unreachable
    // code is there.
    std::string n = be_->namegen("orphan");
    llvm::Function *func = function()->function();
    llvm::BasicBlock *orphan = llvm::BasicBlock::Create(context_, n, func, lbb);
    curblock = orphan;
    break;
  }
  case Bstatement::ST_Label: {
    LabelStatement *lbst = stmt->castToLabelStatement();
    llvm::BasicBlock *lbb = getBlockForLabel(lbst->definedLabel());
    llvm::BranchInst::Create(lbb, curblock);
    curblock = lbb;
    break;
  }
  default:
    assert(false && "not yet handled");
  }
  return curblock;
}

llvm::BasicBlock *Llvm_backend::genEntryBlock(Bfunction *bfunction) {
  llvm::Function *func = bfunction->function();
  llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", func);

  // Spill parameters/arguments, insert allocas for local vars
  bfunction->genProlog(entry);

  return entry;
}

void Llvm_backend::fixupEpilogBlog(Bfunction *bfunction,
                                   llvm::BasicBlock *epilog)
{
  // Append a return instruction if the block does not end with
  // a control transfer.
  if (epilog->empty() || !epilog->back().isTerminator()) {
    LIRBuilder builder(context_, llvm::ConstantFolder());
    llvm::Function *func = bfunction->function();
    llvm::Type *rtyp= func->getFunctionType()->getReturnType();
    llvm::ReturnInst *ri = nullptr;
    if (rtyp->isVoidTy()) {
      ri = builder.CreateRetVoid();
    } else {
      llvm::Value *zv = llvm::Constant::getNullValue(rtyp);
      ri = builder.CreateRet(zv);
    }
    epilog->getInstList().push_back(ri);
  }
}

// Set the function body for FUNCTION using the code in CODE_BLOCK.

bool Llvm_backend::function_set_body(Bfunction *function,
                                     Bstatement *code_stmt) {
  // debugging
  if (traceLevel() > 1) {
    std::cerr << "Statement tree dump:\n";
    code_stmt->dump();
  }

  // Sanity checks
  if (checkIntegrity_)
    enforceTreeIntegrity(code_stmt);

  // Create and populate entry block
  llvm::BasicBlock *entryBlock = genEntryBlock(function);

  // Walk the code statements
  GenBlocks gb(context_, this, function);
  llvm::BasicBlock *block = gb.walk(code_stmt, entryBlock);

  // Fix up epilog block if needed
  fixupEpilogBlog(function, block);

  // debugging
  if (traceLevel() > 0) {
    std::cerr << "LLVM function dump:\n";
    function->function()->dump();
  }

  // At this point we can delete the Bstatement tree, we're done with it
  Bstatement::destroy(code_stmt, DelWrappers);

  return true;
}

// Write the definitions for all TYPE_DECLS, CONSTANT_DECLS,
// FUNCTION_DECLS, and VARIABLE_DECLS declared globally, as well as
// emit early debugging information.

void Llvm_backend::write_global_definitions(
    const std::vector<Btype *> &type_decls,
    const std::vector<Bexpression *> &constant_decls,
    const std::vector<Bfunction *> &function_decls,
    const std::vector<Bvariable *> &variable_decls) {
  std::cerr << "Llvm_backend::write_global_definitions not yet implemented.\n";
}

// Convert an identifier for use in an error message.
// TODO(tham): scan the identifier to determine if contains
// only ASCII or printable UTF-8, then perform character set
// conversions if needed.

const char *go_localize_identifier(const char *ident) { return ident; }

// Return a new backend generator.

Backend *go_get_backend(llvm::LLVMContext &context) {
  return new Llvm_backend(context, nullptr);
}
