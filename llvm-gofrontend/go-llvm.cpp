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
#include "gogo.h"

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

static const auto NotInTargetLib = llvm::LibFunc::NumLibFuncs;

static void indent(unsigned ilevel) {
  for (unsigned i = 0; i < ilevel; ++i)
    std::cerr << " ";
}

void Btype::dump(unsigned ilevel) {
  indent(ilevel);
  if (isUnsigned())
    std::cerr << "[uns] ";
  type_->dump();
}

Bexpression::Bexpression(llvm::Value *value,
                         Btype *btype,
                         const std::string &tag)
    : value_(value), btype_(btype), tag_(tag) {}

Bexpression::~Bexpression() {}

void Bexpression::destroy(Bexpression *expr, WhichDel which) {
  if (which != DelWrappers)
    for (auto inst : expr->instructions())
      delete inst;
  if (which != DelInstructions)
    delete expr;
}

void Bexpression::dump(unsigned ilevel) {
  indent(ilevel);
  bool hitValue = false;
  for (auto inst : instructions()) {
    indent(ilevel);
    if (inst == value()) {
      std::cerr << "*";
      hitValue = true;
    }
    inst->dump();
  }
  if (!hitValue) {
    indent(ilevel);
    value()->dump();
  }
  if (varPending())
    std::cerr << "pending: level=" << addrLevel() << "\n";
}

ExprListStatement *Bstatement::stmtFromExprs(Bexpression *expr, ...) {
  ExprListStatement *st = new ExprListStatement();
  va_list ap;
  va_start(ap, expr);
  while (expr) {
    st->appendExpression(expr);
    expr = va_arg(ap, Bexpression *);
  }
  return st;
}

void Bstatement::dump(unsigned ilevel) {
  switch (flavor()) {
  case ST_Compound: {
    CompoundStatement *cst = castToCompoundStatement();
    indent(ilevel);
    std::cerr << "{\n";
    for (auto st : cst->stlist())
      st->dump(ilevel + 2);
    indent(ilevel);
    std::cerr << "}\n";
    break;
  }
  case ST_ExprList: {
    ExprListStatement *elst = castToExprListStatement();
    for (auto expr : elst->expressions()) {
      expr->dump(ilevel + 2);
    }
    break;
  }
  case ST_IfPlaceholder: {
    IfPHStatement *ifst = castToIfPHStatement();
    indent(ilevel);
    std::cerr << "if:\n";
    indent(ilevel + 2);
    std::cerr << "cond:\n";
    ifst->cond()->dump(ilevel + 2);
    if (ifst->trueStmt()) {
      indent(ilevel + 2);
      std::cerr << "then:\n";
      ifst->trueStmt()->dump(ilevel + 2);
    }
    if (ifst->falseStmt()) {
      indent(ilevel + 2);
      std::cerr << "else:\n";
      ifst->falseStmt()->dump(ilevel + 2);
    }
    break;
  }
  case ST_Goto: {
    GotoStatement *gst = castToGotoStatement();
    indent(ilevel);
    std::cerr << "goto L" << gst->targetLabel() << "\n";
    break;
  }
  case ST_Label: {
    LabelStatement *lbst = castToLabelStatement();
    indent(ilevel);
    std::cerr << "label L" << lbst->definedLabel() << "\n";
    break;
  }

  case ST_SwitchPlaceholder:
    std::cerr << "not yet implemented\n";
    break;
  }
}

void Bstatement::destroy(Bstatement *stmt, WhichDel which) {
  assert(stmt);
  switch (stmt->flavor()) {
  case ST_Compound: {
    CompoundStatement *cst = stmt->castToCompoundStatement();
    for (auto st : cst->stlist())
      destroy(st, which);
    break;
  }

  case ST_ExprList:
    if (which != DelWrappers) {
      ExprListStatement *elst = stmt->castToExprListStatement();
      for (auto expr : elst->expressions())
        Bexpression::destroy(expr, which);
    }
    break;
  case ST_IfPlaceholder: {
    IfPHStatement *ifst = stmt->castToIfPHStatement();
    if (which != DelWrappers)
      Bexpression::destroy(ifst->cond(), which);
    if (ifst->trueStmt())
      Bstatement::destroy(ifst->trueStmt(), which);
    if (ifst->falseStmt())
      Bstatement::destroy(ifst->falseStmt(), which);
    break;
  }
  case ST_Goto:
  case ST_Label: {
    // nothing to do here at the moment
    break;
  }

  case ST_SwitchPlaceholder:
    assert(false && "not yet implemented");
    break;
  }
  if (which != DelInstructions)
    delete stmt;
}

Bfunction::Bfunction(llvm::Function *f)
    : function_(f), labelcount_(0), splitstack_(YesSplit) {}

Bfunction::~Bfunction() {
  // Needed mainly for unit testing
  for (auto ais : allocas_)
    delete ais;
  for (auto &kv : argtoval_)
    delete kv.second;
  for (auto &lab : labels_)
    delete lab;
}

llvm::Argument *Bfunction::getNthArg(unsigned argIdx) {
  assert(function()->getFunctionType()->getNumParams() != 0);
  if (arguments_.empty())
    for (auto &arg : function()->getArgumentList())
      arguments_.push_back(&arg);
  assert(argIdx < arguments_.size());
  return arguments_[argIdx];
}

llvm::Instruction *Bfunction::argValue(llvm::Argument *arg) {
  auto it = argtoval_.find(arg);
  if (it != argtoval_.end())
    return it->second;

  // Create alloca save area for argument, record that and return
  // it. Store into alloca will be generated later.
  std::string aname(arg->getName());
  aname += ".addr";
  llvm::Instruction *inst = new llvm::AllocaInst(arg->getType(), aname);
  assert(argtoval_.find(arg) == argtoval_.end());
  argtoval_[arg] = inst;
  return inst;
}

void Bfunction::genProlog(llvm::BasicBlock *entry) {
  llvm::Function *func = function();

  unsigned nParms = func->getFunctionType()->getNumParams();
  std::vector<llvm::Instruction *> spills;
  for (unsigned idx = 0; idx < nParms; ++idx) {
    llvm::Argument *arg = getNthArg(idx);
    llvm::Instruction *inst = argValue(arg);
    entry->getInstList().push_back(inst);
    llvm::Instruction *si = new llvm::StoreInst(arg, inst);
    spills.push_back(si);
  }
  argtoval_.clear();

  // Append allocas for local variables
  for (auto aa : allocas_)
    entry->getInstList().push_back(aa);
  allocas_.clear();

  // Param spills
  for (auto sp : spills)
    entry->getInstList().push_back(sp);
}

Blabel *Bfunction::newLabel() {
  Blabel *lb = new Blabel(this, labelcount_++);
  labelmap_.push_back(nullptr);
  labels_.push_back(lb);
  return lb;
}

Bstatement *Bfunction::newLabelDefStatement(Blabel *label) {
  LabelStatement *st = new LabelStatement(label->label());
  assert(labelmap_[label->label()] == nullptr);
  labelmap_[label->label()] = st;
  return st;
}

Bstatement *Bfunction::newGotoStatement(Blabel *label, Location location) {
  GotoStatement *st = new GotoStatement(label->label(), location);
  return st;
}

Llvm_backend::Llvm_backend(llvm::LLVMContext &context)
    : context_(context)
    , module_(new llvm::Module("gomodule", context))
    , datalayout_(module_->getDataLayout())
    , addressSpace_(0)
    , traceLevel_(0)
    , complexFloatType_(nullptr)
    , complexDoubleType_(nullptr)
    , errorType_(nullptr)
    , llvmVoidType_(nullptr)
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
  errorType_ = makeAnonType(llvm::StructType::create(context_));

  // For use handling circular types and for builtin creation
  llvmPtrType_ =
      llvm::PointerType::get(llvm::StructType::create(context), addressSpace_);

  // Assorted pre-computer types for use in builtin function creation
  llvmVoidType_ = llvm::Type::getVoidTy(context_);
  llvmIntegerType_ =
      llvm::IntegerType::get(context_, datalayout_.getPointerSizeInBits());
  llvmSizeType_ = llvmIntegerType_;
  llvmInt8Type_ = llvm::IntegerType::get(context_, 8);
  llvmInt32Type_ = llvm::IntegerType::get(context_, 32);
  llvmInt64Type_ = llvm::IntegerType::get(context_, 64);
  llvmFloatType_ = llvm::Type::getFloatTy(context_);
  llvmDoubleType_ = llvm::Type::getDoubleTy(context_);
  llvmLongDoubleType_ = llvm::Type::getFP128Ty(context_);

  // Create and record an error function. By marking it as varargs this will
  // avoid any collisions with things that the front end might create, since
  // Go varargs is handled/lowered entirely by the front end.
  llvm::SmallVector<llvm::Type *, 1> elems(0);
  elems.push_back(errorType_->type());
  const bool isVarargs = true;
  llvm::FunctionType *eft = llvm::FunctionType::get(
      llvm::Type::getVoidTy(context_), elems, isVarargs);
  llvm::GlobalValue::LinkageTypes plinkage = llvm::GlobalValue::ExternalLinkage;
  errorFunction_.reset(
      new Bfunction(llvm::Function::Create(eft, plinkage, "", module_.get())));

  // Reuse the error function as the value for error_expression
  errorExpression_.reset(
      new Bexpression(errorFunction_->function(), errorType_));

  // Error statement
  errorStatement_.reset(Bstatement::stmtFromExprs(errorExpression_.get(),
                                                  nullptr));

  // Reuse the error function as the value for errorVariable_
  errorVariable_.reset(
      new Bvariable(errorType_, Location(), "", ErrorVar, false, nullptr));

  defineAllBuiltins();
}

Llvm_backend::~Llvm_backend() {
  for (auto &expr : expressions_)
    delete expr;
  for (auto pht : placeholders_)
    delete pht;
  for (auto pht : updatedPlaceholders_)
    delete pht;
  for (auto &kv : anonTypemap_)
    delete kv.second;
  for (auto &kv : integerTypemap_)
    delete kv.second;
  for (auto &kv : valueExprmap_)
    delete kv.second;
  for (auto &kv : valueVarmap_)
    delete kv.second;
  for (auto &kv : namedTypemap_)
    delete kv.second;
  for (auto &kv : builtinMap_)
    delete kv.second;
  for (auto &bfcn : functions_)
    delete bfcn;
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

Btype *Llvm_backend::makeAnonType(llvm::Type *lt) {
  assert(lt);

  // unsure whether caching is a net win, but for now cache all
  // previously created types and return the cached result
  // if we ask for the same type twice.
  auto it = anonTypemap_.find(lt);
  if (it != anonTypemap_.end())
    return it->second;
  Btype *rval = new Btype(lt);
  anonTypemap_[lt] = rval;
  return rval;
}

Btype *Llvm_backend::makePlaceholderType(llvm::Type *pht) {
  Btype *bplace = new Btype(pht);
  assert(placeholders_.find(bplace) == placeholders_.end());
  placeholders_.insert(bplace);
  return bplace;
}

void Llvm_backend::updatePlaceholderUnderlyingType(Btype *pht,
                                                   llvm::Type *newtyp) {

  assert(placeholders_.find(pht) != placeholders_.end());
  placeholders_.erase(pht);
  updatedPlaceholders_.insert(pht);
  pht->type_ = newtyp;
}

Btype *Llvm_backend::error_type() { return errorType_; }

Btype *Llvm_backend::void_type() { return makeAnonType(llvmVoidType_); }

Btype *Llvm_backend::bool_type() {
  // LLVM has no predefined boolean type. Use int8 for now
  return makeAnonType(llvm::Type::getInt1Ty(context_));
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

Btype *Llvm_backend::integer_type(bool is_unsigned, int bits) {
  llvm::Type *typ = llvm::IntegerType::get(context_, bits);
  type_plus_unsigned tpu = std::make_pair(typ, is_unsigned);
  auto it = integerTypemap_.find(tpu);
  if (it != integerTypemap_.end())
    return it->second;
  Btype *btyp = new Btype(typ);
  if (is_unsigned)
    btyp->setUnsigned();
  integerTypemap_[tpu] = btyp;
  return btyp;
}

// Get an unnamed float type.

Btype *Llvm_backend::float_type(int bits) {
  if (bits == 32)
    return makeAnonType(llvmFloatType_);
  else if (bits == 64)
    return makeAnonType(llvmDoubleType_);
  else if (bits == 128)
    return makeAnonType(llvmLongDoubleType_);
  assert(false && "unsupported float width");
  return nullptr;
}

// Make a struct type.

// FIXME: llvm::StructType::get has no means of specifying field
// names, meaning that for debug info generation we'll need to
// capture fields here in some way, either by eagerly creating the DI
// (preferrable) or recording the field names for later use (less so)

Btype *Llvm_backend::struct_type(const std::vector<Btyped_identifier> &fields) {
  llvm::SmallVector<llvm::Type *, 64> elems(fields.size());
  for (unsigned i = 0; i < fields.size(); ++i) {
    if (fields[i].btype == errorType_)
      return errorType_;
    elems[i] = fields[i].btype->type();
  }
  llvm::Type *lst = llvm::StructType::get(context_, elems);
  bool isNew = (anonTypemap_.find(lst) == anonTypemap_.end());
  Btype *st = makeAnonType(lst);
  if (isNew) {
    for (unsigned i = 0; i < fields.size(); ++i) {
      structplusindextype spi(std::make_pair(st, i));
      auto it = fieldmap_.find(spi);
      assert(it == fieldmap_.end());
      fieldmap_[spi] = fields[i].btype;
    }
  }
  return st;
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
  Btype *rval = makeAnonType(llvm::StructType::get(context_, elems));
  if (bits == 64)
    complexFloatType_ = rval;
  else
    complexDoubleType_ = rval;
  return rval;
}

// Get a pointer type.

Btype *Llvm_backend::pointer_type(Btype *to_type) {
  if (to_type == errorType_)
    return errorType_;

  // LLVM does not allow creation of a "pointer to void" type -- model
  // this instead as pointer to char.
  llvm::Type *lltot =
      (to_type->type() == llvmVoidType_ ? llvmInt8Type_ : to_type->type());

  return makeAnonType(llvm::PointerType::get(lltot, addressSpace_));
}

// Make a function type.

Btype *
Llvm_backend::function_type(const Btyped_identifier &receiver,
                            const std::vector<Btyped_identifier> &parameters,
                            const std::vector<Btyped_identifier> &results,
                            Btype *result_struct, Location) {
  llvm::SmallVector<llvm::Type *, 4> elems(0);

  // Receiver type if applicable
  if (receiver.btype != nullptr) {
    if (receiver.btype == errorType_)
      return errorType_;
    elems.push_back(receiver.btype->type());
  }

  // Argument types
  for (std::vector<Btyped_identifier>::const_iterator p = parameters.begin();
       p != parameters.end(); ++p) {
    if (p->btype == errorType_)
      return errorType_;
    elems.push_back(p->btype->type());
  }

  // Result types
  llvm::Type *rtyp = nullptr;
  if (results.empty())
    rtyp = llvm::Type::getVoidTy(context_);
  else if (results.size() == 1) {
    if (results.front().btype == errorType_)
      return errorType_;
    rtyp = results.front().btype->type();
  } else {
    assert(result_struct != nullptr);
    rtyp = result_struct->type();
  }
  assert(rtyp != nullptr);

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
  return makeAnonType(llvm::FunctionType::get(rtyp, elems, isVarargs));
}

Btype *Llvm_backend::array_type(Btype *element_btype, Bexpression *length) {
  if (length == errorExpression_.get() || element_btype == errorType_)
    return errorType_;

  llvm::ConstantInt *lc = llvm::dyn_cast<llvm::ConstantInt>(length->value());
  assert(lc);
  uint64_t asize = lc->getValue().getZExtValue();

  llvm::Type *llat = llvm::ArrayType::get(element_btype->type(), asize);
  return makeAnonType(llat);
}

Btype *Llvm_backend::fieldTypeByIndex(Btype *type, unsigned field_index)
{
  structplusindextype spi(std::make_pair(type, field_index));
  auto it = fieldmap_.find(spi);
  assert(it != fieldmap_.end());
  return it->second;
}

// LLVM doesn't directly support placeholder types other than opaque
// structs, so the general strategy for placeholders is to create an
// opaque struct (corresponding to the thing being pointed to) and then
// make a pointer to it. Since LLVM allows only a single opaque struct
// type with a given name within a given context, we generally throw
// out the name/location information passed into the placeholder type
// creation routines.

llvm::Type *Llvm_backend::makeOpaqueLlvmType() {
  return llvm::StructType::create(context_);
}



// Create a placeholder for a pointer type.

Btype *Llvm_backend::placeholder_pointer_type(const std::string &name,
                                              Location location, bool) {
  llvm::Type *opaque = makeOpaqueLlvmType();
  llvm::Type *ph_ptr_typ = llvm::PointerType::get(opaque, addressSpace_);
  return makePlaceholderType(ph_ptr_typ);
}

// Set the real target type for a placeholder pointer type.
//
// NB: front end seems to occasionally call this method on
// types that were not created via makePlaceholderType(),
// so handle this conservatively if the case comes up.

bool Llvm_backend::set_placeholder_pointer_type(Btype *placeholder,
                                                Btype *to_type) {
  assert(placeholder);
  assert(to_type);
  if (placeholder == errorType_ || to_type == errorType_)
    return false;
  assert(to_type->type()->isPointerTy());
  if (placeholders_.find(placeholder) == placeholders_.end()) {
    assert(placeholder->type() == to_type->type());
  } else {
    updatePlaceholderUnderlyingType(placeholder, to_type->type());
  }
  return true;
}

// Set the real values for a placeholder function type.

bool Llvm_backend::set_placeholder_function_type(Btype *placeholder,
                                                 Btype *ft) {
  return set_placeholder_pointer_type(placeholder, ft);
}

// Create a placeholder for a struct type.

Btype *Llvm_backend::placeholder_struct_type(const std::string &name,
                                             Location location) {
  return makePlaceholderType(makeOpaqueLlvmType());
}

// Fill in the fields of a placeholder struct type.

bool Llvm_backend::set_placeholder_struct_type(
    Btype *placeholder, const std::vector<Btyped_identifier> &fields) {
  if (placeholder == errorType_)
    return false;
  Btype *stype = struct_type(fields);
  updatePlaceholderUnderlyingType(placeholder, stype->type());
  return true;
}

// Create a placeholder for an array type.

Btype *Llvm_backend::placeholder_array_type(const std::string &name,
                                            Location location) {
  assert(false && "LLvm_backend::placeholder_array_type not yet implemented");
  return nullptr;
}

// Fill in the components of a placeholder array type.

bool Llvm_backend::set_placeholder_array_type(Btype *placeholder,
                                              Btype *element_btype,
                                              Bexpression *length) {
  if (placeholder == errorType_)
    return false;
  Btype *atype = array_type(element_btype, length);
  updatePlaceholderUnderlyingType(placeholder, atype->type());
  return true;
}

// Return a named version of a type.

Btype *Llvm_backend::named_type(const std::string &name, Btype *btype,
                                Location location) {
  // TODO: add support for debug metadata

  // In the LLVM type world, all types are nameless except for so-called
  // identified struct types. For this reason, names are stored in a side
  // data structure.

  named_llvm_type cand(name, btype->type());
  auto it = namedTypemap_.find(cand);
  if (it != namedTypemap_.end())
    return it->second;
  Btype *rval = new Btype(btype->type());
  namedTypemap_[cand] = rval;
  return rval;
}

// Return a pointer type used as a marker for a circular type.

Btype *Llvm_backend::circular_pointer_type(Btype *, bool) {
  return makeAnonType(llvmPtrType_);
}

// Return whether we might be looking at a circular type.

bool Llvm_backend::is_circular_pointer_type(Btype *btype) {
  return btype->type() == llvmPtrType_;
}

// Return the size of a type.

int64_t Llvm_backend::type_size(Btype *btype) {
  if (btype == errorType_)
    return 1;
  uint64_t uval = datalayout_.getTypeSizeInBits(btype->type());
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

// Define name -> fcn mapping for a builtin.
// Notes:
// - LLVM makes a distinction between libcalls (such as
//   "__sync_fetch_and_add_1") and intrinsics (such as
//   "__builtin_expect" or "__builtin_trap"); the former
//   are target-independent and the latter are target-dependent
// - intrinsics with the no-return property (such as
//   "__builtin_trap" will already be set up this way

void Llvm_backend::defineBuiltinFcn(const char *name, const char *libname,
                                    llvm::Function *fcn) {
  Bfunction *bfunc = new Bfunction(fcn);
  assert(builtinMap_.find(name) == builtinMap_.end());
  builtinMap_[name] = bfunc;
  if (libname) {
    Bfunction *bfunc = new Bfunction(fcn);
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

Bexpression *Llvm_backend::makeValueExpression(llvm::Value *val, Btype *btype,
                                               ValExprScope scope) {
  assert(val);
  if (scope == GlobalScope) {
    valbtype vbt(std::make_pair(val, btype));
    auto it = valueExprmap_.find(vbt);
    if (it != valueExprmap_.end())
      return it->second;
  }
  Bexpression *rval = new Bexpression(val, btype);
  if (scope == GlobalScope) {
    valbtype vbt(std::make_pair(val, btype));
    valueExprmap_[vbt] = rval;
  } else
    expressions_.push_back(rval);
  return rval;
}

Bexpression *Llvm_backend::makeInstExpression(llvm::Instruction *inst,
                                              Btype *btype) {
  assert(inst);
  Bexpression *rval = new Bexpression(inst, btype);
  rval->appendInstruction(inst);
  expressions_.push_back(rval);
  return rval;
}

// Return the zero value for a type.

Bexpression *Llvm_backend::zero_expression(Btype *btype) {
  if (btype == errorType_)
    return errorExpression_.get();
  llvm::Value *zeroval = llvm::Constant::getNullValue(btype->type());
  return makeValueExpression(zeroval, btype, GlobalScope);
}

Bexpression *Llvm_backend::error_expression() { return errorExpression_.get(); }

Bexpression *Llvm_backend::nil_pointer_expression() {
  assert(false && "LLvm_backend::nil_pointer_expression not yet implemented");
  return nullptr;
}

Bexpression *Llvm_backend::loadFromExpr(Bexpression *expr,
                                        Btype *loadResultType)
{
  std::string ldname(expr->tag());
  ldname += ".ld";
  ldname = namegen(ldname);
  llvm::Instruction *ldinst = new llvm::LoadInst(expr->value(), ldname);
  Bexpression *rval = makeExpression(ldinst, loadResultType, expr, nullptr);
  rval->appendInstruction(ldinst);
  return rval;
}

// An expression that indirectly references an expression.

Bexpression *Llvm_backend::indirect_expression(Btype *btype,
                                               Bexpression *expr,
                                               bool known_valid,
                                               Location location) {
  if (expr->varPending()) {
    if (expr->addrLevel() > 0) {
      unsigned addrLevel = expr->addrLevel() - 1;
      Bexpression *rval = new Bexpression(expr->value(), btype, expr->tag());
      rval->setVarExprPending(addrLevel);
      std::string tag(expr->tag());
      tag += ".deref";
      rval->setTag(tag);
      expressions_.push_back(rval);
      return rval;
    }
  }
  expr = resolveVarContext(expr);
  return loadFromExpr(expr, btype);
}

// Get the address of an expression.

Bexpression *Llvm_backend::address_expression(Bexpression *bexpr,
                                              Location location) {
  Btype *pt = pointer_type(bexpr->btype());
  unsigned addrLevel = 0;
  if (bexpr->varPending())
    addrLevel = bexpr->addrLevel();
  Bexpression *rval = new Bexpression(bexpr->value(), pt, bexpr->tag());
  rval->setVarExprPending(addrLevel+1);
  expressions_.push_back(rval);
  return rval;
}

Bexpression *Llvm_backend::resolveVarContext(Bexpression *expr)
{
  if (expr->varPending()) {
    assert(expr->addrLevel() == 0 || expr->addrLevel() == 1);
    if (expr->addrLevel() == 1)
      return expr;
    return loadFromExpr(expr, expr->btype());
  }
  return expr;
}

// An expression that references a variable.

Bexpression *Llvm_backend::var_expression(Bvariable *var,
                                          Varexpr_context in_lvalue_pos,
                                          Location location) {
  if (var == errorVariable_.get())
    return errorExpression_.get();

  // FIXME: record debug location

  unsigned addrLevel = (in_lvalue_pos == VE_lvalue ? 1 : 0);
  Bexpression *varexp =
      makeValueExpression(var->value(), var->getType(), LocalScope);
  varexp->setTag(var->getName().c_str());
  varexp->setVarExprPending(addrLevel);
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
  if (btype->isUnsigned()) {
    uint64_t val = checked_convert_mpz_to_int<uint64_t>(mpz_val);
    assert(llvm::ConstantInt::isValueValidForType(btype->type(), val));
    llvm::Constant *lval = llvm::ConstantInt::get(btype->type(), val);
    rval = makeValueExpression(lval, btype, GlobalScope);
  } else {
    int64_t val = checked_convert_mpz_to_int<int64_t>(mpz_val);
    assert(llvm::ConstantInt::isValueValidForType(btype->type(), val));
    llvm::Constant *lval = llvm::ConstantInt::getSigned(btype->type(), val);
    rval = makeValueExpression(lval, btype, GlobalScope);
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
    return makeValueExpression(fcon, btype, GlobalScope);
  } else if (btype->type() == llvmDoubleType_) {
    double dval = mpfr_get_d(val, GMP_RNDN);
    llvm::APFloat apf(dval);
    llvm::Constant *fcon = llvm::ConstantFP::get(context_, apf);
    return makeValueExpression(fcon, btype, GlobalScope);
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

Bexpression *Llvm_backend::string_constant_expression(const std::string &val) {
  assert(false && "Llvm_backend::string_constant_expression not yet impl");
  return nullptr;
}

// Make a constant boolean expression.

Bexpression *Llvm_backend::boolean_constant_expression(bool val) {
  llvm::Value *con = (val ? llvm::ConstantInt::getTrue(context_)
                          : llvm::ConstantInt::getFalse(context_));
  return makeValueExpression(con, bool_type(), GlobalScope);
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
  // no real implementation yet
  assert(type->type() == expr->value()->getType());
  return expr;
}

// Get the address of a function.

Bexpression *Llvm_backend::function_code_expression(Bfunction *bfunc,
                                                    Location location) {
  assert(false && "Llvm_backend::function_code_expression not yet impl");
  return nullptr;
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
  assert(index < llst->getNumElements());
  //llvm::Type *llft = llst->getElementType(index);
  Btype *bft = fieldTypeByIndex(bstruct->btype(), index);
  llvm::SmallVector<llvm::Value *, 2> elems(2);
  elems[0] = llvm::ConstantInt::get(llvmInt32Type_, 0);
  elems[1] = llvm::ConstantInt::get(llvmInt32Type_, index);
  std::string name = namegen("field");
  llvm::GetElementPtrInst *gep =
      llvm::GetElementPtrInst::Create(llst, bstruct->value(), elems, name);

  // Wrap in a Bexpression
  Bexpression *rval = makeValueExpression(gep, bft, LocalScope);
  rval->appendInstruction(gep);
  if (bstruct->varPending())
    rval->setVarExprPending(bstruct->addrLevel());
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
  assert(false && "Llvm_backend::compound_expression not yet impl");
  return nullptr;
}

// Return an expression that executes THEN_EXPR if CONDITION is true, or
// ELSE_EXPR otherwise.

Bexpression *Llvm_backend::conditional_expression(Btype *btype,
                                                  Bexpression *condition,
                                                  Bexpression *then_expr,
                                                  Bexpression *else_expr,
                                                  Location location) {
  assert(false && "Llvm_backend::conditional_expression not yet impl");
  return nullptr;
}

// Return an expression for the unary operation OP EXPR.

Bexpression *Llvm_backend::unary_expression(Operator op, Bexpression *expr,
                                            Location location) {
  assert(false && "Llvm_backend::unary_expression not yet impl");
  return nullptr;
}

static llvm::Instruction::BinaryOps arith_op_to_binop(Operator op,
                                                      llvm::Type *typ,
                                                      bool isUnsigned,
                                                      const char *&tag) {
  bool isFloat = typ->isFloatingPointTy();

  if (isFloat) {
    switch (op) {
    case OPERATOR_PLUS:
      tag = "fadd";
      return llvm::Instruction::FAdd;
    default:
      break;
    }
  } else {
    switch (op) {
    case OPERATOR_PLUS:
      tag = "add";
      return llvm::Instruction::Add;
    default:
      break;
    }
  }
  assert(false);
  return llvm::Instruction::BinaryOpsEnd;
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

// Return an expression for the binary operation LEFT OP RIGHT.

Bexpression *Llvm_backend::binary_expression(Operator op, Bexpression *left,
                                             Bexpression *right,
                                             Location location) {
  if (left == errorExpression_.get() || right == errorExpression_.get())
    return errorExpression_.get();

  left = resolveVarContext(left);
  right = resolveVarContext(right);

  Btype *bltype = left->btype();
  Btype *brtype = right->btype();
  llvm::Type *ltype = left->value()->getType();
  llvm::Type *rtype = right->value()->getType();
  assert(ltype == rtype);
  assert(bltype->isUnsigned() == brtype->isUnsigned());
  bool isUnsigned = bltype->isUnsigned();

  switch (op) {
  case OPERATOR_EQEQ:
  case OPERATOR_NOTEQ:
  case OPERATOR_LT:
  case OPERATOR_LE:
  case OPERATOR_GT:
  case OPERATOR_GE: {
    llvm::CmpInst::Predicate pred = compare_op_to_pred(op, ltype, isUnsigned);
    llvm::Instruction *cmp;
    if (ltype->isFloatingPointTy())
      cmp = new llvm::FCmpInst(pred, left->value(), right->value(),
                               namegen("fcmp"));
    else
      cmp = new llvm::ICmpInst(pred, left->value(), right->value(),
                               namegen("icmp"));
    Bexpression *e = makeExpression(cmp, bltype, left, right, nullptr);
    e->appendInstruction(cmp);
    return e;
  }
  case OPERATOR_PLUS: {
    const char *tag;
    llvm::Instruction::BinaryOps llvmop =
        arith_op_to_binop(op, ltype, isUnsigned, tag);
    llvm::Instruction *binop = llvm::BinaryOperator::Create(
        llvmop, left->value(), right->value(), namegen(tag));
    Bexpression *e = makeExpression(binop, bltype, left, right, nullptr);
    e->appendInstruction(binop);
    return e;
  }
  default:
    std::cerr << "Op " << op << "unhandled\n";
    assert(false);
  }

  return nullptr;
}

// Return an expression that constructs BTYPE with VALS.

Bexpression *Llvm_backend::constructor_expression(
    Btype *btype, const std::vector<Bexpression *> &vals, Location location) {
  assert(false && "Llvm_backend::constructor_expression not yet impl");
  return nullptr;
}

Bexpression *Llvm_backend::array_constructor_expression(
    Btype *array_btype, const std::vector<unsigned long> &indexes,
    const std::vector<Bexpression *> &vals, Location location) {
  assert(false && "Llvm_backend::array_constructor_expression not yet impl");
  return nullptr;
}

// Return an expression for the address of BASE[INDEX].

Bexpression *Llvm_backend::pointer_offset_expression(Bexpression *base,
                                                     Bexpression *index,
                                                     Location location) {
  assert(false && "Llvm_backend::pointer_offset_expression not yet impl");
  return nullptr;
}

// Return an expression representing ARRAY[INDEX]

Bexpression *Llvm_backend::array_index_expression(Bexpression *array,
                                                  Bexpression *index,
                                                  Location location) {
  assert(false && "Llvm_backend::array_index_expression not yet impl");
  return nullptr;
}

// Create an expression for a call to FN_EXPR with FN_ARGS.
Bexpression *
Llvm_backend::call_expression(Bexpression *fn_expr,
                              const std::vector<Bexpression *> &fn_args,
                              Bexpression *chain_expr, Location location) {
  assert(false && "Llvm_backend::call_expression not yet impl");
  return nullptr;
}

// Return an expression that allocates SIZE bytes on the stack.

Bexpression *Llvm_backend::stack_allocation_expression(int64_t size,
                                                       Location location) {
  assert(false && "Llvm_backend::stack_allocation_expression not yet impl");
  return nullptr;
}

Bstatement *Llvm_backend::error_statement() { return errorStatement_.get(); }

// An expression as a statement.

Bstatement *Llvm_backend::expression_statement(Bexpression *expr) {
  if (expr == errorExpression_.get())
    return errorStatement_.get();
  ExprListStatement *els = new ExprListStatement();
  els->appendExpression(resolveVarContext(expr));
  return els;
}

// Variable initialization.

Bstatement *Llvm_backend::init_statement(Bvariable *var, Bexpression *init) {
  if (var == errorVariable_.get() || init == errorExpression_.get())
    return errorStatement_.get();
  init = resolveVarContext(init);
  return makeAssignment(var->value(), nullptr, init, Location());
}

Bexpression *Llvm_backend::makeExpression(llvm::Instruction *value,
                                           Btype *btype,
                                           Bexpression *src,
                                           ...)
{
  Bexpression *result = makeValueExpression(value, btype, LocalScope);
  va_list ap;
  va_start(ap, src);
  while (src) {
    for (auto inst : src->instructions()) {
      assert(inst->getParent() == nullptr);
      result->appendInstruction(inst);
    }
    src = va_arg(ap, Bexpression *);
  }
  return result;
}

Bstatement *Llvm_backend::makeAssignment(llvm::Value *lval, Bexpression *lhs,
                                         Bexpression *rhs, Location location) {
  llvm::PointerType *pt = llvm::cast<llvm::PointerType>(lval->getType());
  assert(pt);
  llvm::Value *rval = rhs->value();
  assert(rval->getType() == pt->getElementType());

  // FIXME: alignment?
  llvm::Instruction *si = new llvm::StoreInst(rval, lval);

  Bexpression *stexp = makeExpression(si, rhs->btype(), rhs, lhs, nullptr);
  stexp->appendInstruction(si);
  ExprListStatement *els = Bstatement::stmtFromExprs(stexp, nullptr);
  return els;
}

// Assignment.

Bstatement *Llvm_backend::assignment_statement(Bexpression *lhs,
                                               Bexpression *rhs,
                                               Location location) {
  if (lhs == errorExpression_.get() || rhs == errorExpression_.get())
    return errorStatement_.get();
  lhs = resolveVarContext(lhs);
  rhs = resolveVarContext(rhs);
  return makeAssignment(lhs->value(), lhs, rhs, location);
}

Bstatement *
Llvm_backend::return_statement(Bfunction *bfunction,
                               const std::vector<Bexpression *> &vals,
                               Location location) {
  if (bfunction == error_function())
    return errorStatement_.get();
  for (auto v : vals)
    if (v == errorExpression_.get())
      return errorStatement_.get();

  // Temporary
  assert(vals.size() == 1);

  // For the moment return instructions are going to have null type,
  // since their values should not be feeding into anything else (and
  // since Go functions can return multiple values).
  Btype *btype = nullptr;

  Bexpression *toret = resolveVarContext(vals[0]);
  llvm::ReturnInst *ri = llvm::ReturnInst::Create(context_, toret->value());
  Bexpression *rexp = makeExpression(ri, btype, toret, nullptr);
  rexp->appendInstruction(ri);
  ExprListStatement *els = Bstatement::stmtFromExprs(rexp, nullptr);
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

Bstatement *Llvm_backend::if_statement(Bexpression *condition,
                                       Bblock *then_block, Bblock *else_block,
                                       Location location) {
  if (condition == errorExpression_.get())
    return errorStatement_.get();
  condition = resolveVarContext(condition);
  assert(then_block);
  IfPHStatement *ifst =
      new IfPHStatement(condition, then_block, else_block, location);
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
  CompoundStatement *st = new CompoundStatement();
  std::vector<Bstatement *> &stlist = st->stlist();
  stlist.push_back(s1);
  stlist.push_back(s2);
  return st;
}

// List of statements.

Bstatement *
Llvm_backend::statement_list(const std::vector<Bstatement *> &statements) {
  CompoundStatement *st = new CompoundStatement();
  std::vector<Bstatement *> &stlist = st->stlist();
  for (auto st : statements)
    stlist.push_back(st);
  return st;
}

Bblock *Llvm_backend::block(Bfunction *function, Bblock *enclosing,
                            const std::vector<Bvariable *> &vars,
                            Location start_location, Location) {
  assert(function);

  // FIXME: record debug location

  // Create new Bblock
  Bblock *bb = new Bblock();
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
}

// Return a block as a statement.

Bstatement *Llvm_backend::block_statement(Bblock *bblock) {

  // class Bblock inherits from CompoundStatement
  return bblock;
}

// Make a global variable.

Bvariable *Llvm_backend::global_variable(const std::string &var_name,
                                         const std::string &asm_name,
                                         Btype *btype, bool is_external,
                                         bool is_hidden, bool in_unique_section,
                                         Location location) {
  if (btype == errorType_)
    return errorVariable_.get();

  // FIXME: add code to insure non-zero size
  assert(datalayout_.getTypeSizeInBits(btype->type()) != 0);

  // FIXME: add support for this
  assert(in_unique_section == false);

  // FIXME: add DIGlobalVariable to debug info for this variable

  // FIXME: use correct ASM name

  llvm::GlobalValue::LinkageTypes linkage =
      (is_external ? llvm::GlobalValue::ExternalLinkage
                   : llvm::GlobalValue::InternalLinkage);

  bool isConstant = false;
  llvm::Constant *init = nullptr;
  llvm::GlobalVariable *glob = new llvm::GlobalVariable(
      *module_.get(), btype->type(), isConstant, linkage, init, asm_name);
  Bvariable *bv =
      new Bvariable(btype, location, var_name, GlobalVar, false, glob);
  assert(valueVarmap_.find(bv->value()) == valueVarmap_.end());
  valueVarmap_[bv->value()] = bv;
  return bv;
}

// Set the initial value of a global variable.

void Llvm_backend::global_variable_set_init(Bvariable *var, Bexpression *expr) {
  if (var == errorVariable_.get() || expr == errorExpression_.get())
    return;
  assert(llvm::isa<llvm::GlobalVariable>(var->value()));
  llvm::GlobalVariable *gvar = llvm::cast<llvm::GlobalVariable>(var->value());

  assert(llvm::isa<llvm::Constant>(var->value()));
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
  llvm::Instruction *inst = new llvm::AllocaInst(btype->type(), name);
  // inst->setDebugLoc(location.debug_location());
  function->addAlloca(inst);
  Bvariable *bv =
      new Bvariable(btype, location, name, LocalVar, is_address_taken, inst);
  assert(valueVarmap_.find(bv->value()) == valueVarmap_.end());
  valueVarmap_[bv->value()] = bv;
  return bv;
}

// Make a function parameter variable.

Bvariable *Llvm_backend::parameter_variable(Bfunction *function,
                                            const std::string &name,
                                            Btype *btype, bool is_address_taken,
                                            Location location) {
  assert(function);
  if (btype == errorType_ || function == error_function())
    return errorVariable_.get();

  // Collect argument pointer
  unsigned argIdx = function->paramsCreated();
  llvm::Argument *arg = function->getNthArg(argIdx);
  assert(arg);

  // Set name
  arg->setName(name);

  // Create the alloca slot where we will spill this argument
  llvm::Instruction *inst = function->argValue(arg);
  assert(valueVarmap_.find(inst) == valueVarmap_.end());
  Bvariable *bv =
      new Bvariable(btype, location, name, ParamVar, is_address_taken, inst);
  valueVarmap_[inst] = bv;
  return bv;
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
  std::string tname(namegen("tmp"));
  Bvariable *tvar = local_variable(function, tname, btype,
                                   is_address_taken, location);
  if (tvar == errorVariable_.get())
    return tvar;
  Bstatement *is = init_statement(tvar, binit);
  bblock->stlist().push_back(is);
  *pstatement = is;
  return tvar;
}

// Create an implicit variable that is compiler-defined.  This is used when
// generating GC root variables and storing the values of a slice initializer.

Bvariable *Llvm_backend::implicit_variable(const std::string &name,
                                           const std::string &asm_name,
                                           Btype *type, bool is_hidden,
                                           bool is_constant, bool is_common,
                                           int64_t alignment) {
  assert(false && "Llvm_backend::implicit_variable not yet impl");
  return nullptr;
}

// Set the initalizer for a variable created by implicit_variable.
// This is where we finish compiling the variable.

void Llvm_backend::implicit_variable_set_init(Bvariable *var,
                                              const std::string &, Btype *,
                                              bool, bool, bool is_common,
                                              Bexpression *init) {
  assert(false && "Llvm_backend::implicit_variable_set_init not yet impl");
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
                                          bool is_hidden, bool is_common,
                                          Btype *btype, Location location) {
  if (btype == errorType_)
    return errorVariable_.get();

  // FIXME: add code to insure non-zero size
  assert(datalayout_.getTypeSizeInBits(btype->type()) != 0);

  // Common + hidden makes no sense
  assert(!(is_hidden && is_common));

  // Determine linkage
  llvm::GlobalValue::LinkageTypes linkage =
      (is_common ? llvm::GlobalValue::CommonLinkage
                 : (is_hidden ? llvm::GlobalValue::InternalLinkage
                              : llvm::GlobalValue::ExternalLinkage));

  bool isConstant = true;
  llvm::Constant *init = nullptr;
  bool addressTakenDontCare = false;
  llvm::GlobalVariable *glob = new llvm::GlobalVariable(
      *module_.get(), btype->type(), isConstant, linkage, init, asm_name);
  Bvariable *bv = new Bvariable(btype, location, name, GlobalVar,
                                addressTakenDontCare, glob);
  assert(valueVarmap_.find(bv->value()) == valueVarmap_.end());
  valueVarmap_[bv->value()] = bv;
  return bv;
}

// Set the initializer for a variable created by immutable_struct.
// This is where we finish compiling the variable.

void Llvm_backend::immutable_struct_set_init(Bvariable *var,
                                             const std::string &, bool,
                                             bool is_common, Btype *, Location,
                                             Bexpression *initializer) {
  assert(false && "Llvm_backend::immutable_struct_set_init not yet impl");
}

// Return a reference to an immutable initialized data structure
// defined in another package.

Bvariable *Llvm_backend::immutable_struct_reference(const std::string &name,
                                                    const std::string &asmname,
                                                    Btype *btype,
                                                    Location location) {
  assert(false && "Llvm_backend::immutable_struct_reference not yet impl");
  return nullptr;
}

// Make a label.

Blabel *Llvm_backend::label(Bfunction *function, const std::string &name,
                            Location location) {
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

  Bfunction *bfunc = new Bfunction(fcn);

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

class GenBlocks {
public:
  GenBlocks(llvm::LLVMContext &context, Llvm_backend *be, Bfunction *function)
      : context_(context), be_(be), function_(function) {}

  llvm::BasicBlock *walk(Bstatement *stmt, llvm::BasicBlock *curblock);
  Bfunction *function() { return function_; }
  llvm::BasicBlock *genIf(IfPHStatement *ifst, llvm::BasicBlock *curblock);

private:
  llvm::BasicBlock *getBlockForLabel(LabelId lab) {
    auto it = labelmap_.find(lab);
    if (it != labelmap_.end())
      return it->second;
    std::string lname = be_->namegen("label", lab);
    llvm::Function *func = function()->function();
    llvm::BasicBlock *bb = llvm::BasicBlock::Create(context_, lname, func);
    labelmap_[lab] = bb;
    return bb;
  }

private:
  llvm::LLVMContext &context_;
  Llvm_backend *be_;
  Bfunction *function_;
  std::map<LabelId, llvm::BasicBlock *> labelmap_;
};

llvm::BasicBlock *GenBlocks::genIf(IfPHStatement *ifst,
                                   llvm::BasicBlock *curblock) {
  // Append the condition instructions to the current block
  for (auto inst : ifst->cond()->instructions())
    curblock->getInstList().push_back(inst);

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
  llvm::BranchInst::Create(ft, tsucc);

  // Walk false block if present
  if (ifst->falseStmt()) {
    llvm::BasicBlock *fsucc = fsucc = walk(ifst->falseStmt(), fblock);
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
    for (auto expr : elst->expressions()) {
      for (auto inst : expr->instructions()) {
        curblock->getInstList().push_back(inst);
      }
    }
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

// Set the function body for FUNCTION using the code in CODE_BLOCK.

bool Llvm_backend::function_set_body(Bfunction *function,
                                     Bstatement *code_stmt) {
  // debugging
  if (traceLevel() > 1) {
    std::cerr << "Statement tree dump:\n";
    code_stmt->dump();
  }

  // Create and populate entry block
  llvm::BasicBlock *entryBlock = genEntryBlock(function);

  // Walk the code statements
  GenBlocks gb(context_, this, function);
  gb.walk(code_stmt, entryBlock);

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
  return new Llvm_backend(context);
}
