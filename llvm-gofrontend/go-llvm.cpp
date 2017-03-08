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
#include "go-llvm-builtins.h"
#include "backend.h"
#include "go-c.h"
#include "go-system.h"
#include "go-llvm-linemap.h"
#include "go-llvm-dibuildhelper.h"
#include "go-llvm-cabi-oracle.h"
#include "go-llvm-irbuilders.h"
#include "gogo.h"

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FileSystem.h"

#define CHKTREE(x) if (checkIntegrity_ && traceLevel()) \
      enforceTreeIntegrity(x)

Llvm_backend::Llvm_backend(llvm::LLVMContext &context,
                           llvm::Module *module,
                           Llvm_linemap *linemap)
    : TypeManager(context, llvm::CallingConv::X86_64_SysV)
    , context_(context)
    , module_(module)
    , datalayout_(module ? &module->getDataLayout() : nullptr)
    , diCompileUnit_(nullptr)
    , linemap_(linemap)
    , addressSpace_(0)
    , traceLevel_(0)
    , checkIntegrity_(true)
    , createDebugMetaData_(true)
    , exportDataStarted_(false)
    , exportDataFinalized_(false)
    , TLI_(nullptr)
    , builtinTable_(new BuiltinTable(typeManager(), false))
    , errorFunction_(nullptr)
{
  // If nobody passed in a linemap, create one for internal use (unit testing)
  if (!linemap_) {
    ownLinemap_.reset(new Llvm_linemap());
    linemap_ = ownLinemap_.get();
  }

  // Similarly for the LLVM module (unit testing)
  if (!module_) {
    ownModule_.reset(new llvm::Module("gomodule", context));
    ownModule_->setTargetTriple("x86_64-unknown-linux-gnu");
    ownModule_->setDataLayout("e-m:e-i64:64-f80:128-n8:16:32:64-S128");
    module_ = ownModule_.get();
  }

  datalayout_ = &module_->getDataLayout();

  // Reuse the error function as the value for error_expression
  errorExpression_ = nbuilder_.mkError(errorType());

  // We now have the necessary bits to finish initialization of the
  // type manager.
  initializeTypeManager(errorExpression(),
                        datalayout_,
                        nameTags());

  // Create and record an error function. By marking it as varargs this will
  // avoid any collisions with things that the front end might create, since
  // Go varargs is handled/lowered entirely by the front end.
  llvm::SmallVector<llvm::Type *, 1> elems(0);
  elems.push_back(llvmBoolType());
  const bool isVarargs = true;
  llvm::FunctionType *eft = llvm::FunctionType::get(
      llvm::Type::getVoidTy(context_), elems, isVarargs);
  llvm::GlobalValue::LinkageTypes plinkage = llvm::GlobalValue::ExternalLinkage;
  llvm::Function *ef = llvm::Function::Create(eft, plinkage, "", module_);
  errorFunction_.reset(new Bfunction(ef, makeAuxFcnType(eft), "", "",
                                     Location(), typeManager()));

  // Error statement.
  errorStatement_ = nbuilder_.mkErrorStmt();

  // Error variable.
  Location loc;
  errorVariable_.reset(
      new Bvariable(errorType(), loc, "", ErrorVar, false, nullptr));

  // Initialize machinery for builtins
  builtinTable_->defineAllBuiltins();
}

Llvm_backend::~Llvm_backend() {
  for (auto &kv : valueVarMap_)
    delete kv.second;
  for (auto &bfcn : functions_)
    delete bfcn;
}

llvm::DICompileUnit *Llvm_backend::getDICompUnit()
{
  if (!createDebugMetaData_)
    return nullptr;

  if (diCompileUnit_ || !createDebugMetaData_)
    return diCompileUnit_;

  // Create debug info builder
  assert(dibuilder_.get() == nullptr);
  dibuilder_.reset(new llvm::DIBuilder(*module_));

  // Create compile unit
  llvm::SmallString<256> currentDir;
  llvm::sys::fs::current_path(currentDir);
  auto primaryFile =
      dibuilder_->createFile(linemap_->get_initial_file(), currentDir);
  bool isOptimized = true;
  std::string compileFlags; // FIXME
  unsigned runtimeVersion = 0; // not sure what would be for Go
  diCompileUnit_ =
      dibuilder_->createCompileUnit(llvm::dwarf::DW_LANG_Go, primaryFile,
                                    "llvm-goparse", isOptimized,
                                    compileFlags, runtimeVersion);

  return diCompileUnit_;
}

TypeManager *Llvm_backend::typeManager() const {
  const TypeManager *tm = this;
  return const_cast<TypeManager*>(tm);
}

void Llvm_backend::setTraceLevel(unsigned level)
{
  traceLevel_ = level;
  setTypeManagerTraceLevel(level);
}

void
Llvm_backend::verifyModule()
{
  bool broken = llvm::verifyModule(module(), &llvm::dbgs());
  assert(!broken && "Module not well-formed.");
}

void
Llvm_backend::dumpModule()
{
  module().dump();
}

void Llvm_backend::dumpExpr(Bexpression *e)
{
  if (e) {
    e->srcDump(linemap_);
    auto p = checkTreeIntegrity(e, DumpPointers);
    if (p.first)
      std::cerr << p.second;
  }
}

void Llvm_backend::dumpStmt(Bstatement *s)
{
  if (s) {
    s->srcDump(linemap_);
    auto p = checkTreeIntegrity(s, DumpPointers);
    if (p.first)
      std::cerr << p.second;
  }
}

void Llvm_backend::dumpVar(Bvariable *v)
{
  if (v)
    v->srcDump(linemap_);
}

std::pair<bool, std::string>
Llvm_backend::checkTreeIntegrity(Bexpression *e,
                                 CkTreePtrDisp ptrDisp,
                                 CkTreeVarDisp varDisp)
{
  IntegrityVisitor iv(this, ptrDisp, varDisp);
  bool rval = iv.visit(e);
  return std::make_pair(rval, iv.msg());
}

std::pair<bool, std::string>
Llvm_backend::checkTreeIntegrity(Bstatement *s,
                                 CkTreePtrDisp ptrDisp,
                                 CkTreeVarDisp varDisp)
{
  IntegrityVisitor iv(this, ptrDisp, varDisp);
  bool rval = iv.visit(s);
  return std::make_pair(rval, iv.msg());
}

void Llvm_backend::enforceTreeIntegrity(Bexpression *e)
{
  IntegrityVisitor iv(this, DumpPointers, IgnoreVarExprs);
  if (! iv.visit(e)) {
    std::cerr << iv.msg() << "\n";
    assert(false);
  }
}

void Llvm_backend::enforceTreeIntegrity(Bstatement *s)
{
  IntegrityVisitor iv(this, DumpPointers, IgnoreVarExprs);
  if (! iv.visit(s)) {
    std::cerr << iv.msg() << "\n";
    assert(false);
  }
}

Btype *Llvm_backend::error_type() { return errorType(); }

Btype *Llvm_backend::void_type() { return voidType(); }

Btype *Llvm_backend::bool_type() { return boolType(); }

// Get an unnamed float type.

Btype *Llvm_backend::float_type(int bits)
{
  return floatType(bits);
}

Btype *Llvm_backend::integer_type(bool is_unsigned, int bits)
{
  return integerType(is_unsigned, bits);
}

Btype *Llvm_backend::struct_type(const std::vector<Btyped_identifier> &fields)
{
  return structType(fields);
}

// Create a placeholder for a struct type.
Btype *Llvm_backend::placeholder_struct_type(const std::string &name,
                                             Location location)
{
  return placeholderStructType(name, location);
}

Btype *Llvm_backend::complex_type(int bits) {
  return complexType(bits);
}

// Get a pointer type.

Btype *Llvm_backend::pointer_type(Btype *toType)
{
  return pointerType(toType);
}

Btype *Llvm_backend::placeholder_pointer_type(const std::string &name,
                                              Location location, bool forfunc)
{
  return placeholderPointerType(name, location, forfunc);
}

// Make a function type.

Btype *
Llvm_backend::function_type(const Btyped_identifier &receiver,
                            const std::vector<Btyped_identifier> &parameters,
                            const std::vector<Btyped_identifier> &results,
                            Btype *result_struct, Location loc) {
  return functionType(receiver, parameters, results,
                      result_struct, loc);
}

Btype *Llvm_backend::array_type(Btype *elemType, Bexpression *length)
{
  return arrayType(elemType, length);
}

Btype *Llvm_backend::placeholder_array_type(const std::string &name,
                                            Location location)
{
  return placeholderArrayType(name, location);
}

bool Llvm_backend::set_placeholder_pointer_type(Btype *placeholder,
                                                Btype *to_type)
{
  return setPlaceholderPointerType(placeholder, to_type);
}

bool Llvm_backend::set_placeholder_function_type(Btype *placeholder,
                                                 Btype *ft) {
  return setPlaceholderPointerType(placeholder, ft);
}

bool
Llvm_backend::set_placeholder_struct_type(Btype *placeholder,
                            const std::vector<Btyped_identifier> &fields)
{
  return setPlaceholderStructType(placeholder, fields);
}

// Fill in the components of a placeholder array type.

bool Llvm_backend::set_placeholder_array_type(Btype *placeholder,
                                              Btype *element_btype,
                                              Bexpression *length) {
  return setPlaceholderArrayType(placeholder, element_btype, length);
}

Btype *Llvm_backend::named_type(const std::string &name,
                                Btype *btype,
                                Location location)
{
  return namedType(name, btype, location);
}

Btype *Llvm_backend::circular_pointer_type(Btype *placeholder, bool isf) {
  return circularPointerType(placeholder, isf);
}

bool Llvm_backend::is_circular_pointer_type(Btype *btype) {
  return isCircularPointerType(btype);
}

int64_t Llvm_backend::type_size(Btype *btype) {
  return typeSize(btype);
}

int64_t Llvm_backend::type_alignment(Btype *btype) {
  return typeAlignment(btype);
}

int64_t Llvm_backend::type_field_alignment(Btype *btype)
{
  return typeFieldAlignment(btype);
}

int64_t Llvm_backend::type_field_offset(Btype *btype, size_t index)
{
  return typeFieldOffset(btype, index);
}

Bfunction *Llvm_backend::defineBuiltinFcn(const std::string &name,
                                          llvm::Function *fcn)
{
  llvm::PointerType *llpft =
      llvm::cast<llvm::PointerType>(fcn->getType());
  llvm::FunctionType *llft =
      llvm::cast<llvm::FunctionType>(llpft->getElementType());
  BFunctionType *fcnType = makeAuxFcnType(llft);
  Location pdcl = linemap()->get_predeclared_location();
  Bfunction *bfunc = new Bfunction(fcn, fcnType, name, name, pdcl,
                                   typeManager());
  return bfunc;
}

// Look up a named built-in function in the current backend implementation.
// Returns NULL if no built-in function by that name exists.

Bfunction *Llvm_backend::lookup_builtin(const std::string &name) {

  // Do we have an entry at all for this builtin?
  BuiltinEntry *be = builtinTable_->lookup(name);
  if (!be)
    return nullptr;

  // We have an entry -- have we materialized a Bfunction for it yet?
  Bfunction *bf = be->bfunction();
  if (bf != nullptr)
    return bf;

  // Materialize a Bfunction for the builtin
  if (be->flavor() == BuiltinEntry::IntrinsicBuiltin) {
    llvm::SmallVector<llvm::Type *, 8> ltypes;
    for (auto &t : be->types())
      ltypes.push_back(t->type());
    llvm::Function *fcn =
        llvm::Intrinsic::getDeclaration(module_, be->intrinsicId(), ltypes);
    assert(fcn != nullptr);
    bf = defineBuiltinFcn(be->name(), fcn);
  } else {
    assert(be->flavor() == BuiltinEntry::LibcallBuiltin);

    // Create function type.
    Btyped_identifier receiver;
    std::vector<Btyped_identifier> params;
    std::vector<Btyped_identifier> results;
    Location bloc(linemap_->get_predeclared_location());
    const BuiltinEntryTypeVec &types = be->types();
    Btyped_identifier result("ret", types[0], bloc);
    results.push_back(result);
    for (unsigned idx = 1; idx < types.size(); ++idx)
      params.push_back(Btyped_identifier("", types[idx], bloc));
    Btype *fcnType = functionType(receiver, params, results, nullptr, bloc);

    // Create function
    bf = function(fcnType, be->name(), be->name(),
                  true, false, false, false, false, bloc);

    // FIXME: add attributes this function? Such as maybe
    // llvm::Attribute::ArgMemOnly, llvm::Attribute::ReadOnly?

    // FIXME: once we have a pass manager set up for the back end, we'll
    // want to turn on this code, since it will be helpful to catch
    // errors/mistakes. For the time being it can't be turned on (not
    // pass manager is set up).

    llvm::LibFunc lf = be->libfunc();
    if (TLI_ && lf != llvm::LibFunc::NumLibFuncs) {

      // Verify that the function is available on this target.
      assert(TLI_->has(lf));

      // Verify that the name and type we've computer so far matches up
      // with how LLVM views the routine. For example, if we are trying
      // to create a version of memcmp() that takes a single boolean as
      // an argument, that's going to be a show-stopper type problem.
      assert(TLI_->getLibFunc(*bf->function(), lf));
    }
  }
  be->setBfunction(bf);

  return bf;
}

bool Llvm_backend::moduleScopeValue(llvm::Value *val, Btype *btype) const
{
  valbtype vbt(std::make_pair(val, btype));
  return (valueExprmap_.find(vbt) != valueExprmap_.end());
}

Bexpression *Llvm_backend::makeGlobalExpression(Bexpression *expr,
                                                llvm::Value *val,
                                                Btype *btype,
                                                Location location) {
  assert(! llvm::isa<llvm::Instruction>(val));
  valbtype vbt(std::make_pair(val, btype));
  auto it = valueExprmap_.find(vbt);
  if (it != valueExprmap_.end()) {
    nbuilder_.freeExpr(expr);
    return it->second;
  }
  valueExprmap_[vbt] = expr;
  return expr;
}

// Return the zero value for a type.

Bexpression *Llvm_backend::zero_expression(Btype *btype) {
  if (btype == errorType())
    return errorExpression();
  llvm::Value *zeroval = llvm::Constant::getNullValue(btype->type());
  Bexpression *cexpr = nbuilder_.mkConst(btype, zeroval);
  return makeGlobalExpression(cexpr, zeroval, btype, Location());
}

Bexpression *Llvm_backend::error_expression() { return errorExpression(); }

Bexpression *Llvm_backend::nil_pointer_expression() {

  // What type should we assign a NIL pointer expression? This
  // is something of a head-scratcher. For now use uintptr.
  llvm::Type *pti = llvm::PointerType::get(llvmIntegerType(), addressSpace_);
  Btype *uintptrt = makeAuxType(pti);
  return zero_expression(uintptrt);
}

Bexpression *Llvm_backend::loadFromExpr(Bexpression *expr,
                                        Btype *btype,
                                        Location loc,
                                        const std::string &tag)
{
  std::string ldname(tag);
  ldname += ".ld";
  ldname = namegen(ldname);

  // If this is a load from a pointer flagged as being a circular
  // type, insert a conversion prior to the load so as to force
  // the value to the correct type. This is weird but necessary,
  // since the LLVM type system can't accurately model Go circular
  // pointer types.
  Bexpression *space = expr;
  Btype *loadResultType = btype;
  Btype *tctyp = circularTypeLoadConversion(expr->btype());
  if (tctyp != nullptr) {
    space = convert_expression(pointer_type(tctyp), expr, loc);
    loadResultType = tctyp;
  }
  llvm::Instruction *loadInst = new llvm::LoadInst(space->value(), ldname);
  Bexpression *rval = nbuilder_.mkDeref(loadResultType, loadInst, space, loc);
  rval->appendInstruction(loadInst);
  return rval;
}

// An expression that indirectly references an expression.

Bexpression *Llvm_backend::indirect_expression(Btype *btype,
                                               Bexpression *expr,
                                               bool known_valid,
                                               Location location) {
  if (expr == errorExpression() || btype == errorType())
    return errorExpression();

  assert(expr->btype()->type()->isPointerTy());

  const VarContext *vc = nullptr;
  if (expr->varExprPending()) {
    vc = &expr->varContext();
    // handle *&x
    if (vc->addrLevel() != 0) {
      Bexpression *rval = nbuilder_.mkDeref(btype, expr->value(), expr,
                                            location);
      rval->setVarExprPending(vc->lvalue(), vc->addrLevel() - 1);
      return rval;
    }
  }

  std::string tag(expr->tag().size() == 0 ? "deref" : expr->tag());
  Bexpression *rval = loadFromExpr(expr, btype, location, tag);
  if (vc)
    rval->setVarExprPending(expr->varContext());

  return rval;
}

// Get the address of an expression.

Bexpression *Llvm_backend::address_expression(Bexpression *bexpr,
                                              Location location) {
  if (bexpr == errorExpression())
    return errorExpression();

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
  if (bexpr->value()->getType() == stringType()->type() &&
      llvm::isa<llvm::Constant>(bexpr->value()))
    return bexpr;

  // Create new expression with proper type.
  Btype *pt = pointer_type(bexpr->btype());
  Bexpression *rval = nbuilder_.mkAddress(pt, bexpr->value(), bexpr, location);
  std::string adtag(bexpr->tag());
  adtag += ".ad";
  rval->setTag(adtag);
  const VarContext &vc = bexpr->varContext();
  rval->setVarExprPending(vc.lvalue(), vc.addrLevel() + 1);

  // Handle circular types
  Btype *ctypconv = circularTypeAddrConversion(bexpr->btype());
  if (ctypconv != nullptr)
    return convert_expression(ctypconv, rval, location);

  return rval;
}

bool Llvm_backend::exprVectorHasError(const std::vector<Bexpression *> &vals) {
  for (auto v : vals)
    if (v == errorExpression())
      return true;
  return false;
}

bool Llvm_backend::stmtVectorHasError(const std::vector<Bstatement *> &stmts)
{
  for (auto s : stmts)
    if (s == errorStatement())
      return true;
  return false;
}

Bexpression *Llvm_backend::resolve(Bexpression *expr, Bfunction *func)
{
  if (expr->compositeInitPending())
    expr = resolveCompositeInit(expr, func, nullptr);
  if (expr->varExprPending())
    expr = resolveVarContext(expr);
  return expr;
}

Bexpression *Llvm_backend::resolveVarContext(Bexpression *expr,
                                             Varexpr_context ctx)

{
  if (expr->varExprPending()) {
    const VarContext &vc = expr->varContext();
    assert(vc.addrLevel() == 0 || vc.addrLevel() == 1);
    if (vc.addrLevel() == 1 || vc.lvalue() || ctx == VE_lvalue) {
      assert(vc.addrLevel() == 0 || expr->btype()->type()->isPointerTy());
      return nbuilder_.mkAddress(expr->btype(), expr->value(),
                                 expr, expr->location());
    }
    Btype *btype = expr->btype();
    Bexpression *rval = loadFromExpr(expr, btype, expr->location(),
                                     expr->tag());
    return rval;
  }
  return expr;
}

Bvariable *Llvm_backend::genVarForConstant(llvm::Constant *conval,
                                           Btype *type)
{
  auto it = valueVarMap_.find(conval);
  if (it != valueVarMap_.end())
    return it->second;

  bool isHidden = true;
  bool isConstant = true;
  bool isCommon = false;
  std::string ctag(namegen("const"));
  Bvariable *rv = implicit_variable(ctag, "", type, isHidden,
                                    isConstant, isCommon, 0);
  assert(llvm::isa<llvm::GlobalVariable>(rv->value()));
  llvm::GlobalVariable *gvar = llvm::cast<llvm::GlobalVariable>(rv->value());
  gvar->setInitializer(conval);
  return rv;
}

Bexpression *Llvm_backend::genStore(Bfunction *func,
                                    Bexpression *srcExpr,
                                    Bexpression *dstExpr,
                                    Location location)
{
  Binstructions insns;
  BinstructionsLIRBuilder builder(context_, &insns);

  // If the value we're storing has non-composite type,
  // then issue a simple store instruction.
  if (!srcExpr->btype()->type()->isAggregateType()) {

    Bexpression *valexp = resolve(srcExpr, func);
    llvm::Value *val = valexp->value();
    llvm::Value *dst = dstExpr->value();
    if (val->getType()->isPointerTy()) {
      llvm::PointerType *dstpt =
          llvm::cast<llvm::PointerType>(dst->getType());
      val = convertForAssignment(valexp, dstpt->getElementType());
    }

    // At this point the types should agree
    llvm::PointerType *dpt = llvm::cast<llvm::PointerType>(dst->getType());
    assert(val->getType() == dpt->getElementType());

    // Create and return store
    llvm::Instruction *si = builder.CreateStore(val, dst);

    // Return result
    Bexpression *rval =
        nbuilder_.mkBinaryOp(OPERATOR_EQ, valexp->btype(), si,
                             dstExpr, valexp, insns, location);
    return rval;
  }

  // Composite type case

  if (srcExpr->compositeInitPending())
    srcExpr = resolveCompositeInit(srcExpr, func, nullptr);

  llvm::SmallVector<llvm::Value *, 3> llargs;
  Btype *memargt = makeAuxType(llvmPtrType());

  // memcpy destination
  assert(dstExpr->value()->getType()->isPointerTy());
  if (memargt->type() != dstExpr->value()->getType())
    dstExpr = convert_expression(memargt, dstExpr, location);
  llargs.push_back(dstExpr->value());

  // memcpy src: handle constant input
  llvm::Value *val = srcExpr->value();
  if (llvm::isa<llvm::Constant>(val)) {
    llvm::Constant *cval = llvm::cast<llvm::Constant>(val);
    Bvariable *cvar = genVarForConstant(cval, srcExpr->btype());
    srcExpr = var_expression(cvar, VE_rvalue, location);
    srcExpr = address_expression(srcExpr, location);
  }

  // memcpy src: cast
  assert(srcExpr->value()->getType()->isPointerTy());
  if (memargt->type() != srcExpr->value()->getType()) {
    LIRBuilder builder(context_, llvm::ConstantFolder());
    std::string tag(namegen("cast"));
    llvm::Value *bitcast = builder.CreateBitCast(srcExpr->value(),
                                                 memargt->type(), tag);
    srcExpr = nbuilder_.mkConversion(memargt, bitcast, srcExpr, location);
  }
  llargs.push_back(srcExpr->value());

  // number of bytes to copy
  uint64_t sz = typeSize(srcExpr->btype());
  llvm::Constant *szval = llvm::ConstantInt::get(llvmSizeType(), sz);
  llargs.push_back(szval);

  // alignment of src expr
  unsigned algn = typeAlignment(srcExpr->btype());
  llvm::Constant *alval = llvm::ConstantInt::get(llvmInt32Type(), algn);
  llargs.push_back(alval);

  // volatile bit
  llvm::Constant *volval = llvm::ConstantInt::get(llvmBoolType(), 0);
  llargs.push_back(volval);

  // Q: should we be using memmove here instead?
  Bfunction *memcpy = lookup_builtin("__builtin_memcpy");
  assert(memcpy);
  std::string ctag(namegen("copy"));
  llvm::CallInst *call = builder.CreateCall(memcpy->function(), llargs, ctag);

  // Return result
  Bexpression *rval =
      nbuilder_.mkBinaryOp(OPERATOR_EQ, srcExpr->btype(), call,
                           dstExpr, srcExpr, insns, location);
  return rval;
}

// This version repurposes/reuses the input Bexpression as the
// result (which could be changed if needed).

Bexpression *Llvm_backend::genArrayInit(llvm::ArrayType *llat,
                                        Bexpression *expr,
                                        llvm::Value *storage,
                                        Bfunction *bfunc)
{
  const std::vector<Bexpression *> aexprs = expr->getChildExprs();
  unsigned nElements = llat->getNumElements();
  assert(nElements == aexprs.size());

  for (unsigned eidx = 0; eidx < nElements; ++eidx) {

    // Construct an appropriate GEP
    llvm::Value *idxval = llvm::ConstantInt::get(llvmInt32Type(), eidx);
    llvm::Value *gep = makeArrayIndexGEP(llat, idxval, storage);
    if (llvm::isa<llvm::Instruction>(gep))
      expr->appendInstruction(llvm::cast<llvm::Instruction>(gep));

    // Store value into gep
    Bexpression *valexp = resolve(aexprs[eidx], bfunc);
    nbuilder_.updateCompositeChild(expr, eidx, valexp);
    llvm::Instruction *si = new llvm::StoreInst(valexp->value(), gep);
    expr->appendInstruction(si);
  }

  nbuilder_.finishComposite(expr, storage);
  return expr;
}

// This version repurposes/reuses the input Bexpression as the
// result (which could be changed if needed).

Bexpression *Llvm_backend::genStructInit(llvm::StructType *llst,
                                         Bexpression *expr,
                                         llvm::Value *storage,
                                         Bfunction *bfunc)
{
  const std::vector<Bexpression *> fexprs = expr->getChildExprs();
  unsigned nFields = llst->getNumElements();
  assert(nFields == fexprs.size());

  for (unsigned fidx = 0; fidx < nFields; ++fidx) {

    // Construct an appropriate GEP
    llvm::Value *gep = makeFieldGEP(llst, fidx, storage);
    if (llvm::isa<llvm::Instruction>(gep))
      expr->appendInstruction(llvm::cast<llvm::Instruction>(gep));

    // Unpack/post-process the value in question
    assert(fexprs[fidx]);
    Bexpression *valexp = resolve(fexprs[fidx], bfunc);
    nbuilder_.updateCompositeChild(expr, fidx, valexp);

    // Store value into gep
    llvm::Value *val = valexp->value();
    if (val->getType()->isPointerTy()) {
      llvm::PointerType *geppt =
          llvm::cast<llvm::PointerType>(gep->getType());
      val = convertForAssignment(valexp, geppt->getElementType());
    }
    llvm::Instruction *si = new llvm::StoreInst(val, gep);
    expr->appendInstruction(si);
  }

  nbuilder_.finishComposite(expr, storage);
  return expr;
}

Bexpression *Llvm_backend::resolveCompositeInit(Bexpression *expr,
                                                Bfunction *func,
                                                llvm::Value *storage)
{
  if (expr == errorExpression() || func == errorFunction_.get())
    return errorExpression();
  bool setPending = false;
  Bvariable *tvar = nullptr;
  if (!storage) {
    assert(func);
    std::string tname(namegen("tmp"));
    tvar = local_variable(func, tname, expr->btype(), true, Location());
    assert(tvar != errorVariable_.get());
    tvar->markAsTemporary();
    storage = tvar->value();
    setPending = true;
  }

  // Call separate helper depending on array or struct
  llvm::Type *llt = expr->btype()->type();
  assert(llt->isStructTy() || llt->isArrayTy());
  Bexpression *rval = nullptr;
  if (llt->isStructTy()) {
    llvm::StructType *llst = llvm::cast<llvm::StructType>(llt);
    rval = genStructInit(llst, expr, storage, func);
  } else {
    llvm::ArrayType *llat = llvm::cast<llvm::ArrayType>(llt);
    rval = genArrayInit(llat, expr, storage, func);
  }
  if (setPending) {
    rval->setVarExprPending(false, 0);
    tvar->setInitializerExpr(rval);
  }
  return rval;
}

// An expression that references a variable.

Bexpression *Llvm_backend::var_expression(Bvariable *var,
                                          Varexpr_context in_lvalue_pos,
                                          Location location) {
  if (var == errorVariable_.get())
    return errorExpression();

  // FIXME: record debug location

  Bexpression *varexp = nbuilder_.mkVar(var, location);
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
  if (btype == errorType() || val == errorExpression())
    return errorExpression();

  // FIXME: declare named read-only variable with initial value 'val'

  return val;
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
  if (btype == errorType())
    return errorExpression();
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
    Bexpression *bconst = nbuilder_.mkConst(btype, lval);
    return makeGlobalExpression(bconst, lval, btype, Location());
  } else {
    int64_t val = checked_convert_mpz_to_int<int64_t>(mpz_val);
    assert(llvm::ConstantInt::isValueValidForType(btype->type(), val));
    llvm::Constant *lval = llvm::ConstantInt::getSigned(btype->type(), val);
    Bexpression *bconst = nbuilder_.mkConst(btype, lval);
    return makeGlobalExpression(bconst, lval, btype, Location());
  }
  return rval;
}

// Return a typed value as a constant floating-point number.

Bexpression *Llvm_backend::float_constant_expression(Btype *btype, mpfr_t val) {
  if (btype == errorType())
    return errorExpression();

  // Force the mpfr value into float, double, or APFloat as appropriate.
  //
  // Note: at the moment there is no way to create an APFloat from a
  // "long double" value, so this code takes the unpleasant step of
  // converting a quad mfr value from text and then back into APFloat
  // from there.

  if (btype->type() == llvmFloatType()) {
    float fval = mpfr_get_flt(val, GMP_RNDN);
    llvm::APFloat apf(fval);
    llvm::Constant *fcon = llvm::ConstantFP::get(context_, apf);
    Bexpression *bconst = nbuilder_.mkConst(btype, fcon);
    return makeGlobalExpression(bconst, fcon, btype, Location());
  } else if (btype->type() == llvmDoubleType()) {
    double dval = mpfr_get_d(val, GMP_RNDN);
    llvm::APFloat apf(dval);
    llvm::Constant *fcon = llvm::ConstantFP::get(context_, apf);
    Bexpression *bconst = nbuilder_.mkConst(btype, fcon);
    return makeGlobalExpression(bconst, fcon, btype, Location());
  } else if (btype->type() == llvmLongDoubleType()) {
    assert("not yet implemented" && false);
    return nullptr;
  } else {
    return errorExpression();
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
    llvm::Value *zer = llvm::Constant::getNullValue(stringType()->type());
    Bexpression *bconst = nbuilder_.mkConst(stringType(), zer);
    return makeGlobalExpression(bconst, zer, stringType(), Location());
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
      llvm::ConstantExpr::getBitCast(varval, stringType()->type());
  Bexpression *bconst = nbuilder_.mkConst(stringType(), bitcast);
  return makeGlobalExpression(bconst, bitcast, stringType(), Location());
}

// Make a constant boolean expression.

Bexpression *Llvm_backend::boolean_constant_expression(bool val) {
  LIRBuilder builder(context_, llvm::ConstantFolder());
  llvm::Value *con = (val ? llvm::ConstantInt::getTrue(context_)
                          : llvm::ConstantInt::getFalse(context_));
  llvm::Value *tobool = builder.CreateZExt(con, bool_type()->type(), "");

  Bexpression *bconst = nbuilder_.mkConst(bool_type(), tobool);
  return makeGlobalExpression(bconst, tobool, bool_type(), Location());
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
  if (type == errorType() || expr == errorExpression())
    return errorExpression();
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
  if (val->getType()->isPointerTy() && toType == llvmIntegerType()) {
    std::string tname(namegen("pticast"));
    llvm::Value *pticast = builder.CreatePtrToInt(val, type->type(), tname);
    return nbuilder_.mkConversion(type, pticast, expr, location);
  }

  // Pointer-sized-integer type pointer type. This comes up
  // in type hash/compare functions.
  if (toType && val->getType() == llvmIntegerType()) {
    std::string tname(namegen("itpcast"));
    llvm::Value *itpcast = builder.CreateIntToPtr(val, type->type(), tname);
    return nbuilder_.mkConversion(type, itpcast, expr, location);
  }

  // For pointer conversions (ex: *int32 => *int64) create an
  // appropriate bitcast.
  if (val->getType()->isPointerTy() && toType->isPointerTy()) {
    std::string tag(namegen("cast"));
    llvm::Value *bitcast = builder.CreateBitCast(val, toType, tag);
    return nbuilder_.mkConversion(type, bitcast, expr, location);
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
    return nbuilder_.mkConversion(type, conv, expr, location);
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
    return errorExpression();

  assert(llvm::isa<llvm::Constant>(bfunc->function()));

  // Look up pointer-to-function type
  Btype *fpBtype = pointer_type(bfunc->fcnType());

  // Create an address-of-function expr
  Bexpression *fexpr = nbuilder_.mkFcnAddress(fpBtype, bfunc->function(),
                                                    bfunc, location);
  return makeGlobalExpression(fexpr, bfunc->function(), fpBtype, location);
}

llvm::Value *Llvm_backend::makePointerOffsetGEP(llvm::PointerType *llpt,
                                                llvm::Value *idxval,
                                                llvm::Value *sptr)
{
  LIRBuilder builder(context_, llvm::ConstantFolder());
  llvm::SmallVector<llvm::Value *, 1> elems(1);
  elems[0] = idxval;
  llvm::Type *pointee = llpt->getElementType();
  llvm::Value *val = builder.CreateGEP(pointee, sptr, elems, namegen("ptroff"));
  return val;
}

llvm::Value *Llvm_backend::makeArrayIndexGEP(llvm::ArrayType *llat,
                                             llvm::Value *idxval,
                                             llvm::Value *sptr)
{
  LIRBuilder builder(context_, llvm::ConstantFolder());
  llvm::SmallVector<llvm::Value *, 2> elems(2);
  elems[0] = llvm::ConstantInt::get(llvmInt32Type(), 0);
  elems[1] = idxval;
  llvm::Value *val = builder.CreateGEP(llat, sptr, elems, namegen("index"));
  return val;
}

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
  if (bstruct == errorExpression())
    return errorExpression();

  // Construct an appropriate GEP
  llvm::Type *llt = bstruct->btype()->type();
  assert(llt->isStructTy());
  llvm::StructType *llst = llvm::cast<llvm::StructType>(llt);
  llvm::Value *gep = makeFieldGEP(llst, index, bstruct->value());
  Btype *bft = elementTypeByIndex(bstruct->btype(), index);

  // Wrap result in a Bexpression
  Bexpression *rval = nbuilder_.mkStructField(bft, gep, bstruct,
                                              index, location);

  if (bstruct->varExprPending())
    rval->setVarExprPending(bstruct->varContext());

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
  if (bstat == errorStatement() || bexpr == errorExpression())
    return errorExpression();

  // Compound expressions can be used to produce lvalues, so we don't
  // want to call resolve() on bexpr here.
  Bexpression *rval = nbuilder_.mkCompound(bstat, bexpr, location);
  return rval;
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
      btype == errorType() ||
      condition == errorExpression() ||
      then_expr == errorExpression() ||
      else_expr == errorExpression())
    return errorExpression();

  assert(condition && then_expr);

  condition = resolveVarContext(condition);
  then_expr = resolve(then_expr, function);
  if (else_expr)
    else_expr = resolve(else_expr, function);

  std::vector<Bvariable *> novars;
  Bblock *thenBlock = nbuilder_.mkBlock(function, novars, location);
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
  nbuilder_.addStatementToBlock(thenBlock, thenStmt);

  if (else_expr) {
    Bstatement *elseStmt = nullptr;
    elseBlock = nbuilder_.mkBlock(function, novars, location);
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
    nbuilder_.addStatementToBlock(elseBlock, elseStmt);
  }

  // Wrap up and return the result
  Bstatement *ifStmt = if_statement(function, condition,
                                    thenBlock, elseBlock, location);

  Bexpression *rval = (tempv ?
                       var_expression(tempv, VE_rvalue, location) :
                       nbuilder_.mkVoidValue(void_type()));
  Bexpression *result = compound_expression(ifStmt, rval, location);
  return result;
}

// Return an expression for the unary operation OP EXPR.

Bexpression *Llvm_backend::unary_expression(Operator op, Bexpression *expr,
                                            Location location) {
  if (expr == errorExpression())
    return errorExpression();

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
      assert(isBooleanType(bt));

      // FIXME: is this additional compare-to-zero needed? Or can we be certain
      // that the value in question has a single bit set?
      Bexpression *bzero = zero_expression(bt);
      llvm::Value *cmp =
          builder.CreateICmpNE(expr->value(), bzero->value(), namegen("icmp"));
      Btype *lbt = makeAuxType(llvmBoolType());
      Bexpression *cmpex =
          nbuilder_.mkBinaryOp(OPERATOR_EQEQ, lbt, cmp, bzero, expr, location);
      llvm::Constant *one = llvm::ConstantInt::get(llvmBoolType(), 1);
      llvm::Value *xorex = builder.CreateXor(cmp, one, namegen("xor"));
      Bexpression *notex = nbuilder_.mkUnaryOp(op, lbt, xorex, cmpex, location);
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
      Bexpression *rval = nbuilder_.mkUnaryOp(op, bt, xorExpr, expr, location);
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
    std::string tag(namegen("cast"));
    llvm::Value *bitcast = builder.CreateBitCast(leftVal, rightType, tag);
    rval.first = bitcast;
    return rval;
  }

  // Case 2: X op nil
  if (llvm::isa<llvm::ConstantPointerNull>(rightVal) &&
      leftType->isPointerTy()) {
    BexprLIRBuilder builder(context_, right);
    std::string tag(namegen("cast"));
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
  if (left == errorExpression() || right == errorExpression())
    return errorExpression();

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
    Btype *bcmpt = makeAuxType(llvmBoolType());
    // gen compare...
    Bexpression *cmpex =
        nbuilder_.mkBinaryOp(op, bcmpt, val, left, right, location);
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
  case OPERATOR_MULT: {
    if (ltype->isFloatingPointTy())
      val = builder.CreateFMul(leftVal, rightVal, namegen("fmul"));
    else
      val = builder.CreateMul(leftVal, rightVal, namegen("mul"));
    break;
  }
  case OPERATOR_DIV: {
    if (ltype->isFloatingPointTy())
      val = builder.CreateFDiv(leftVal, rightVal, namegen("fdiv"));
    else if (isUnsigned)
      val = builder.CreateUDiv(leftVal, rightVal, namegen("div"));
    else
      val = builder.CreateSDiv(leftVal, rightVal, namegen("div"));
    break;
  }
  case OPERATOR_OROR: {
    // Note that the FE will have already expanded out || in a control
    // flow context (short circuiting)
    assert(!ltype->isFloatingPointTy());
    val = builder.CreateOr(leftVal, rightVal, namegen("ior"));
    break;
  }
  case OPERATOR_ANDAND:
    // Note that the FE will have already expanded out && in a control
    // flow context (short circuiting).

    // fall through...

  case OPERATOR_AND: {
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

  return nbuilder_.mkBinaryOp(op, bltype, val, left, right, location);
}

bool
Llvm_backend::valuesAreConstant(const std::vector<Bexpression *> &vals)
{
  for (auto &val : vals)
    if (val->value() == nullptr ||
        ! llvm::isa<llvm::Constant>(val->value()))
      return false;
  return true;
}

// Return an expression that constructs BTYPE with VALS.

Bexpression *Llvm_backend::constructor_expression(
    Btype *btype, const std::vector<Bexpression *> &vals, Location location) {
  if (btype == errorType() || exprVectorHasError(vals))
    return errorExpression();

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

  // Here the NULL value signals that we want to delay full instantiation
  // of this constant expression until we can identify the storage for it.
  llvm::Value *nilval = nullptr;
  Bexpression *ccon = nbuilder_.mkComposite(btype, nilval, init_vals, location);
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

  Bexpression *bcon = nbuilder_.mkComposite(btype, scon, vals, location);
  return makeGlobalExpression(bcon, scon, btype, location);
}

Bexpression *Llvm_backend::array_constructor_expression(
    Btype *array_btype, const std::vector<unsigned long> &indexes,
    const std::vector<Bexpression *> &vals, Location location) {
  if (array_btype == errorType() || exprVectorHasError(vals))
    return errorExpression();

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
  if (base == errorExpression() || index == errorExpression())
    return errorExpression();

  base = resolveVarContext(base);
  index = resolveVarContext(index);

  // Construct an appropriate GEP
  llvm::PointerType *llpt =
      llvm::cast<llvm::PointerType>(base->btype()->type());
  llvm::Value *gep =
      makePointerOffsetGEP(llpt, index->value(), base->value());
  //  BPointerType *bpft = base->btype()->castToBPointerType();
  //Btype *bet = bpft->toType();

  // Wrap in a Bexpression
  Bexpression *rval = nbuilder_.mkArrayIndex(base->btype(), gep, base,
                                             index, location);

  std::string tag(base->tag());
  tag += ".ptroff";
  rval->setTag(tag);

  // We're done
  return rval;
}

// Return an expression representing ARRAY[INDEX]

Bexpression *Llvm_backend::array_index_expression(Bexpression *barray,
                                                  Bexpression *index,
                                                  Location location) {

  if (barray == errorExpression() || index == errorExpression())
    return errorExpression();

  index = resolveVarContext(index);

  // Construct an appropriate GEP
  llvm::ArrayType *llat =
      llvm::cast<llvm::ArrayType>(barray->btype()->type());
  llvm::Value *gep =
      makeArrayIndexGEP(llat, index->value(), barray->value());
  Btype *bet = elementTypeByIndex(barray->btype(), 0);

  // Wrap in a Bexpression
  Bexpression *rval = nbuilder_.mkArrayIndex(bet, gep, barray, index, location);
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
  if (fn_expr == errorExpression() || exprVectorHasError(fn_args) ||
      chain_expr == errorExpression())
    return errorExpression();

  // FIXME: call chain not yet handled
  assert(chain_expr == nullptr);

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
      nbuilder_.mkCall(rbtype, call, resolvedArgs, location);
  return rval;
}

// Return an expression that allocates SIZE bytes on the stack.

Bexpression *Llvm_backend::stack_allocation_expression(int64_t size,
                                                       Location location) {
  assert(false && "Llvm_backend::stack_allocation_expression not yet impl");
  return nullptr;
}

Bstatement *Llvm_backend::error_statement() { return errorStatement(); }

// An expression as a statement.

Bstatement *Llvm_backend::expression_statement(Bfunction *bfunction,
                                               Bexpression *expr) {
  if (expr == errorExpression() || bfunction == errorFunction_.get())
    return errorStatement();
  Bstatement *es =
      nbuilder_.mkExprStmt(bfunction,
                           resolve(expr, bfunction),
                           expr->location());
  CHKTREE(es);
  return es;
}

// Variable initialization.

Bstatement *Llvm_backend::init_statement(Bfunction *bfunction,
                                         Bvariable *var, Bexpression *init) {
  if (var == errorVariable_.get() || init == errorExpression() ||
      bfunction == errorFunction_.get())
    return errorStatement();
  if (init) {
    if (init->compositeInitPending()) {
      init = resolveCompositeInit(init, bfunction, var->value());
      Bstatement *es = nbuilder_.mkExprStmt(bfunction, init,
                                            init->location());
      var->setInitializer(es->getExprStmtExpr()->value());
      CHKTREE(es);
      return es;
    }
    //init = resolveVarContext(init);
  } else {
    init = zero_expression(var->btype());
  }
  Bexpression *varexp = nbuilder_.mkVar(var, var->location());
  Bstatement *st = makeAssignment(bfunction, var->value(),
                                  varexp, init, Location());
  var->setInitializer(st->getExprStmtExpr()->value());
  CHKTREE(st);
  return st;
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
    std::string tag(namegen("cast"));
    llvm::Value *bitcast = builder.CreateBitCast(src->value(), dstToType, tag);
    return bitcast;
  }

  // Case 2: handle circular function pointer types.
  bool dstCircPtr = isCircularPointerType(dstToType);
  if (srcPtrToFD && dstCircPtr) {
    BexprLIRBuilder builder(context_, src);
    std::string tag(namegen("cast"));
    llvm::Value *bitcast = builder.CreateBitCast(src->value(), dstToType, tag);
    return bitcast;
  }

  // Case 2: handle raw function pointer assignment (frontend will
  // sometimes take a function pointer and assign it to "void *" without
  // an explicit conversion).
  bool dstPtrToVoid = isPtrToVoidType(dstToType);
  bool srcFuncPtr = isPtrToFuncType(srcType);
  if (dstPtrToVoid && srcFuncPtr) {
    BexprLIRBuilder builder(context_, src);
    std::string tag(namegen("cast"));
    llvm::Value *bitcast = builder.CreateBitCast(src->value(), dstToType, tag);
    return bitcast;
  }

  // Case 4: handle polymorphic nil pointer expressions-- these are
  // generated without a type initially, so we need to convert them
  // to the appropriate type if they appear in an assignment context.
  if (src->value() == nil_pointer_expression()->value()) {
    BexprLIRBuilder builder(context_, src);
    std::string tag(namegen("cast"));
    llvm::Value *bitcast = builder.CreateBitCast(src->value(), dstToType, tag);
    return bitcast;
  }

  return src->value();
}

Bstatement *Llvm_backend::makeAssignment(Bfunction *function,
                                         llvm::Value *lval, Bexpression *lhs,
                                         Bexpression *rhs, Location location) {
  assert(lval->getType()->isPointerTy());

  // This cases should have been handled in the caller
  assert(!rhs->compositeInitPending());

  // Invoke helper to create store or memcpy
  Bexpression *stexp = genStore(function, rhs, lhs, location);

  // Return wrapped in statement
  return nbuilder_.mkExprStmt(function, stexp, location);
}

// Assignment.

Bstatement *Llvm_backend::assignment_statement(Bfunction *bfunction,
                                               Bexpression *lhs,
                                               Bexpression *rhs,
                                               Location location) {
  if (lhs == errorExpression() || rhs == errorExpression() ||
      bfunction == errorFunction_.get())
    return errorStatement();
  Bexpression *lhs2 = resolveVarContext(lhs, VE_lvalue);
  Bexpression *rhs2 = rhs;
  if (rhs->compositeInitPending()) {
    rhs2 = resolveCompositeInit(rhs, bfunction, lhs2->value());
    Bexpression *stexp =
        nbuilder_.mkBinaryOp(OPERATOR_EQ, voidType(), lhs2->value(),
                             lhs2, rhs2, location);
    Bstatement *es = nbuilder_.mkExprStmt(bfunction, stexp, location);
    CHKTREE(es);
    return es;
  }

  Bstatement *st = makeAssignment(bfunction, lhs->value(),
                                  lhs2, rhs2, location);
  CHKTREE(st);
  return st;
}

Bstatement*
Llvm_backend::return_statement(Bfunction *bfunction,
                               const std::vector<Bexpression *> &vals,
                               Location location) {
  if (bfunction == error_function() || exprVectorHasError(vals))
    return errorStatement();

  // For the moment return instructions are going to have null type,
  // since their values should not be feeding into anything else (and
  // since Go functions can return multiple values).
  Btype *btype = nullptr;

  // Resolve arguments
  std::vector<Bexpression *> resolvedVals;
  for (auto &val : vals)
    resolvedVals.push_back(resolve(val, bfunction));

  Bexpression *toret = nullptr;
  if (vals.size() == 1) {
    toret = resolvedVals[0];
  } else {
    Btype *rtyp = bfunction->fcnType()->resultType();
    Bexpression *structVal =
        constructor_expression(rtyp, resolvedVals, location);
    if (! bfunction->returnValueMem()) {
      structVal = resolve(structVal, bfunction);
      toret = structVal;
    } else {
      if (llvm::isa<llvm::Constant>(structVal->value())) {
        llvm::Constant *cval = llvm::cast<llvm::Constant>(structVal->value());
        Bvariable *cv = genVarForConstant(cval, structVal->btype());
        structVal = var_expression(cv, VE_rvalue, location);
        structVal = address_expression(structVal, location);
      }
    }
  }

  Binstructions retInsns;
  llvm::Value *rval = bfunction->genReturnSequence(toret, &retInsns);
  llvm::ReturnInst *ri = llvm::ReturnInst::Create(context_, rval);
  Bexpression *rexp =
      nbuilder_.mkReturn(btype, ri, toret, retInsns, location);
  return nbuilder_.mkExprStmt(bfunction, rexp, location);
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

// If statement.

Bstatement *Llvm_backend::if_statement(Bfunction *bfunction,
                                       Bexpression *condition,
                                       Bblock *then_block, Bblock *else_block,
                                       Location location) {
  if (condition == errorExpression())
    return errorStatement();
  condition = resolve(condition, bfunction);
  assert(then_block);
  Btype *bt = makeAuxType(llvmBoolType());
  Bexpression *conv = convert_expression(bt, condition, location);

  Bstatement *ifst = nbuilder_.mkIfStmt(bfunction, conv, then_block,
                                        else_block, location);
  CHKTREE(ifst);
  return ifst;
}

// Switch.

Bstatement *Llvm_backend::switch_statement(Bfunction *bfunction,
                                           Bexpression *value,
    const std::vector<std::vector<Bexpression *>> &cases,
    const std::vector<Bstatement *> &statements, Location switch_location)
{
  // Error handling
  if (value == errorExpression())
    return errorStatement();
  for (auto casev : cases)
    if (exprVectorHasError(casev))
      return errorStatement();
  if (stmtVectorHasError(statements))
      return errorStatement();

  // Resolve value
  value = resolve(value, bfunction);

  // Case expressions are expected to be constants for this flavor of switch
  for (auto &bexpvec : cases)
    for (auto &exp : bexpvec)
      if (! llvm::cast<llvm::Constant>(exp->value()))
        go_assert(false && "bad case value expression");

  // Store results stmt
  Bstatement *swst =
      nbuilder_.mkSwitchStmt(bfunction, value, cases, statements,
                             switch_location);
  CHKTREE(swst);
  return swst;
}

// Pair of statements.

Bstatement *Llvm_backend::compound_statement(Bstatement *s1, Bstatement *s2) {
  if (s1 == errorStatement() || s2 == errorStatement())
    return errorStatement();

  assert(!s1->function() || !s2->function() ||
         s1->function() == s2->function());

  std::vector<Bstatement *> stvec;
  stvec.push_back(s1);
  stvec.push_back(s2);
  return statement_list(stvec);
}

// List of statements.

Bstatement *
Llvm_backend::statement_list(const std::vector<Bstatement *> &statements) {

  Bfunction *func = nullptr;
  for (auto &st : statements) {
    if (st == errorStatement())
      return errorStatement();
    if (st->function()) {
      if (func)
        assert(st->function() == func);
      else
        func = st->function();
    }
  }

  std::vector<Bvariable *> novars;
  Bblock *block = nbuilder_.mkBlock(func, novars, Location());
  for (auto &st : statements)
    nbuilder_.addStatementToBlock(block, st);
  CHKTREE(block);
  return block;
}

Bblock *Llvm_backend::block(Bfunction *function, Bblock *enclosing,
                            const std::vector<Bvariable *> &vars,
                            Location start_location, Location) {
  assert(function);

  // FIXME: record debug location

  // Create new Bblock
  Bblock *bb = nbuilder_.mkBlock(function, vars, start_location);
  function->addBlock(bb);

  // FIXME:
  // Mark start and end of lifetime for each variable
  // for (auto var : vars) {
  //   Not yet implemented
  // }

  return bb;
}

// Add statements to a block.

void Llvm_backend::block_add_statements(Bblock *bblock,
                           const std::vector<Bstatement *> &statements) {
  for (auto st : statements)
    if (st == errorStatement())
      return;
  assert(bblock);
  for (auto st : statements)
    nbuilder_.addStatementToBlock(bblock, st);
  CHKTREE(bblock);
}

// Return a block as a statement.

Bstatement *Llvm_backend::block_statement(Bblock *bblock) {
  CHKTREE(bblock);
  return bblock; // class Bblock inherits from Bstatement
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
  if (btype == errorType())
    return errorVariable_.get();

#if 0
  // FIXME: add code to insure non-zero size
  assert(datalayout().getTypeSizeInBits(btype->type()) != 0);
#endif

  // FIXME: add support for this
  assert(inUniqueSection == MV_DefaultSection);

  // FIXME: add support for this
  assert(inComdat == MV_NotInComdat);

  // FIXME: add DIGlobalVariable to debug info for this variable

  llvm::Constant *init = nullptr;
  std::string gname(asm_name.empty() ? name : asm_name);
  llvm::GlobalVariable *glob = new llvm::GlobalVariable(
      module(), btype->type(), isConstant == MV_Constant,
      linkage, init, gname);
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
  if (var == errorVariable_.get() || expr == errorExpression())
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
  if (btype == errorType() || function == error_function())
    return errorVariable_.get();
  return function->local_variable(name, btype, is_address_taken, location);
}

// Make a function parameter variable.

Bvariable *Llvm_backend::parameter_variable(Bfunction *function,
                                            const std::string &name,
                                            Btype *btype, bool is_address_taken,
                                            Location location) {
  assert(function);
  if (btype == errorType() || function == error_function())
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
  if (binit == errorExpression())
    return errorVariable_.get();
  std::string tname(namegen("tmpv"));
  Bvariable *tvar = local_variable(function, tname, btype,
                                   is_address_taken, location);
  if (tvar == errorVariable_.get())
    return tvar;
  tvar->markAsTemporary();
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
  if (btype == errorType())
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

  if (init != nullptr && init == errorExpression())
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
  if (btype == errorType())
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
  if (var == errorVariable_.get() || initializer == errorExpression())
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
  if (btype == errorType())
    return errorVariable_.get();

  // FIXME: add code to insure non-zero size
  assert(datalayout().getTypeSizeInBits(btype->type()) != 0);

  // FIXME: add DIGlobalVariable to debug info for this variable

  llvm::GlobalValue::LinkageTypes linkage = llvm::GlobalValue::ExternalLinkage;
  bool isConstant = true;
  llvm::Constant *init = nullptr;
  llvm::GlobalVariable *glob = new llvm::GlobalVariable(
      module(), btype->type(), isConstant, linkage, init, asmname);
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
  return function->newLabel(location);
}

// Make a statement which defines a label.

Bstatement *Llvm_backend::label_definition_statement(Blabel *label) {
  Bfunction *function = label->function();
  Bstatement *st = nbuilder_.mkLabelDefStmt(function, label, label->location());
  function->registerLabelDefStatement(st, label);
  return st;
}

// Make a goto statement.

Bstatement *Llvm_backend::goto_statement(Blabel *label, Location location) {
  Bfunction *function = label->function();
  return nbuilder_.mkGotoStmt(function, label, location);
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
  if (fntype == errorType())
    return errorFunction_.get();
  llvm::Twine fn(name);
  llvm::FunctionType *fty = llvm::cast<llvm::FunctionType>(fntype->type());
  llvm::GlobalValue::LinkageTypes linkage = llvm::GlobalValue::ExternalLinkage;
  llvm::Function *fcn = llvm::Function::Create(fty, linkage, fn, module_);

  fcn->addFnAttr("disable-tail-calls", "true");

  // visibility
  if (!is_visible)
    fcn->setVisibility(llvm::GlobalValue::HiddenVisibility);

  // inline/noinline
  if (!is_inlinable)
    fcn->addFnAttr(llvm::Attribute::NoInline);

  BFunctionType *fcnType = fntype->castToBFunctionType();
  assert(fcnType);
  Bfunction *bfunc = new Bfunction(fcn, fcnType, name, asm_name, location,
                                   typeManager());

  // split-stack or nosplit
  if (! disable_split_stack)
    fcn->addFnAttr("split-stack");
  else
    bfunc->setSplitStack(Bfunction::NoSplit);

  // TODO: unique section support. llvm::GlobalObject has support for
  // setting COMDAT groups and section names, but nothing to manage how
  // section names are created or doled out as far as I can tell, need
  // to look a little more closely at how -ffunction-sections is implemented
  // for clang/LLVM.
  assert(!in_unique_section || is_declaration);

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
// FIXME: convert to use new Bnode walk paradigm.
//
class GenBlocks {
public:
  GenBlocks(llvm::LLVMContext &context, Llvm_backend *be,
            Bfunction *function, Bnode *topNode, llvm::DIScope *scope,
            bool createDebugMetadata, llvm::BasicBlock *entryBlock);

  llvm::BasicBlock *walk(Bnode *node, llvm::BasicBlock *curblock);
  void finishFunction();

  Bfunction *function() { return function_; }
  llvm::BasicBlock *genIf(Bstatement *ifst,
                          llvm::BasicBlock *curblock);
  llvm::BasicBlock *genSwitch(Bstatement *swst,
                              llvm::BasicBlock *curblock);

 private:
  llvm::BasicBlock *mkLLVMBlock(const std::string &name,
                            unsigned expl = Llvm_backend::ChooseVer);
  llvm::BasicBlock *getBlockForLabel(LabelId lab);
  llvm::BasicBlock *walkExpr(llvm::BasicBlock *curblock, Bexpression *expr);

  llvm::DIBuilder &dibuilder() { return be_->dibuilder(); }
  DIBuildHelper &dibuildhelper() { return *dibuildhelper_.get(); }
  Llvm_linemap *linemap() { return be_->linemap(); }

 private:
  llvm::LLVMContext &context_;
  Llvm_backend *be_;
  Bfunction *function_;
  std::unique_ptr<DIBuildHelper> dibuildhelper_;
  std::map<LabelId, llvm::BasicBlock *> labelmap_;
  bool emitOrphanedCode_;
  bool createDebugMetaData_;
};

GenBlocks::GenBlocks(llvm::LLVMContext &context,
                     Llvm_backend *be,
                     Bfunction *function,
                     Bnode *topNode,
                     llvm::DIScope *scope,
                     bool createDebugMetadata,
                     llvm::BasicBlock *entryBlock)
    : context_(context), be_(be), function_(function),
      dibuildhelper_(nullptr), emitOrphanedCode_(false),
      createDebugMetaData_(createDebugMetadata)
{
  if (createDebugMetaData_) {
    dibuildhelper_.reset(new DIBuildHelper(topNode,
                                           be->typeManager(),
                                           be->linemap(),
                                           be->dibuilder(),
                                           be->getDICompUnit(),
                                           entryBlock));
    dibuildhelper().beginFunction(scope, function);
  }
}

void GenBlocks::finishFunction()
{
  if (createDebugMetaData_)
    dibuildhelper().endFunction(function_);
}

llvm::BasicBlock *GenBlocks::mkLLVMBlock(const std::string &name,
                                         unsigned expl)
{
  std::string tname = be_->namegen(name, expl);
  llvm::Function *func = function()->function();
  return llvm::BasicBlock::Create(context_, tname, func);
}

llvm::BasicBlock *GenBlocks::getBlockForLabel(LabelId lab) {
  auto it = labelmap_.find(lab);
  if (it != labelmap_.end())
    return it->second;
  llvm::BasicBlock *bb = mkLLVMBlock("label", lab);
  labelmap_[lab] = bb;
  return bb;
}

llvm::BasicBlock *GenBlocks::walkExpr(llvm::BasicBlock *curblock,
                                      Bexpression *expr)
{
  // Visit children first
  const std::vector<Bnode *> &kids = expr->children();
  for (auto &child : kids)
    curblock = walk(child, curblock);

  // Now visit instructions for this expr
  for (auto inst : expr->instructions()) {
    if (createDebugMetaData_)
      dibuildhelper().processExprInst(expr, inst);
    if (curblock)
      curblock->getInstList().push_back(inst);
    else
      delete inst;
  }
  return curblock;
}

llvm::BasicBlock *GenBlocks::genIf(Bstatement *ifst,
                                   llvm::BasicBlock *curblock)
{
  assert(ifst->flavor() == N_IfStmt);

  Bexpression *cond = ifst->getIfStmtCondition();
  Bstatement *trueStmt = ifst->getIfStmtTrueBlock();
  Bstatement *falseStmt = ifst->getIfStmtFalseBlock();

  // Walk condition first
  curblock = walkExpr(curblock, cond);

  // Create true block
  llvm::BasicBlock *tblock = mkLLVMBlock("then");

  // Push fallthrough block
  llvm::BasicBlock *ft = mkLLVMBlock("fallthrough");

  // Create false block if present
  llvm::BasicBlock *fblock = ft;
  if (falseStmt)
    fblock = mkLLVMBlock("else");

  // Insert conditional branch into current block
  llvm::Value *cval = cond->value();
  llvm::BranchInst::Create(tblock, fblock, cval, curblock);

  // Visit true block
  llvm::BasicBlock *tsucc = walk(trueStmt, tblock);
  if (tsucc && ! tsucc->getTerminator())
    llvm::BranchInst::Create(ft, tsucc);

  // Walk false block if present
  if (falseStmt) {
    llvm::BasicBlock *fsucc = fsucc = walk(falseStmt, fblock);
    if (fsucc && ! fsucc->getTerminator())
      llvm::BranchInst::Create(ft, fsucc);
  }

  return ft;
}

llvm::BasicBlock *GenBlocks::genSwitch(Bstatement *swst,
                                       llvm::BasicBlock *curblock)
{
  assert(swst->flavor() == N_SwitchStmt);

  // Walk switch value first
  Bexpression *swval = swst->getSwitchStmtValue();
  curblock = walkExpr(curblock, swval);

  // Unpack switch
  unsigned ncases = swst->getSwitchStmtNumCases();

  // No need to walk switch value expressions -- they should all be constants.

  // Create blocks
  llvm::BasicBlock *defBB = nullptr;
  std::vector<llvm::BasicBlock *> blocks(ncases);
  for (unsigned idx = 0; idx < ncases; ++idx) {
    std::vector<Bexpression *> thiscase =
        swst->getSwitchStmtNthCase(idx);
    bool isDefault = (thiscase.size() == 0);
    std::string bname(isDefault ? "default" : "case");
    blocks[idx] = mkLLVMBlock(bname);
    if (isDefault) {
      assert(! defBB);
      defBB = blocks[idx];
    }
  }
  llvm::BasicBlock *epilogBB = mkLLVMBlock("epilog");
  if (!defBB)
    defBB = epilogBB;

  LIRBuilder builder(context_, llvm::ConstantFolder());

  // Walk statement/block
  for (unsigned idx = 0; idx < ncases; ++idx) {
    Bstatement *st = swst->getSwitchStmtNthStmt(idx);
    walk(st, blocks[idx]);
    if (! blocks[idx]->getTerminator()) {
      builder.SetInsertPoint(blocks[idx]);
      builder.CreateBr(epilogBB);
    }
  }

  // Create switch
  builder.SetInsertPoint(curblock);
  llvm::SwitchInst *swinst = builder.CreateSwitch(swval->value(), defBB);

  // Connect values with blocks
  for (unsigned idx = 0; idx < blocks.size(); ++idx) {
    std::vector<Bexpression *> thiscase =
        swst->getSwitchStmtNthCase(idx);
    for (auto &exp : thiscase) {
      llvm::ConstantInt *ci = llvm::cast<llvm::ConstantInt>(exp->value());
      swinst->addCase(ci, blocks[idx]);
    }
  }

  return epilogBB;
}

llvm::BasicBlock *GenBlocks::walk(Bnode *node,
                                  llvm::BasicBlock *curblock)
{
  Bexpression *expr = node->castToBexpression();
  if (expr)
    return walkExpr(curblock, expr);
  Bstatement *stmt = node->castToBstatement();
  assert(stmt);
  switch (stmt->flavor()) {
    case N_ExprStmt: {
      curblock = walkExpr(curblock, stmt->getExprStmtExpr());
      break;
    }
    case N_BlockStmt: {
      Bblock *bblock = stmt->castToBblock();
      if (createDebugMetaData_)
        dibuildhelper().beginLexicalBlock(bblock);
      for (auto &st : stmt->getChildStmts())
        curblock = walk(st, curblock);
      if (createDebugMetaData_)
        dibuildhelper().endLexicalBlock(bblock);
      break;
    }
    case N_IfStmt: {
      curblock = genIf(stmt, curblock);
      break;
    }
    case N_SwitchStmt: {
      curblock = genSwitch(stmt, curblock);
      break;
    }
    case N_GotoStmt: {
      llvm::BasicBlock *lbb = getBlockForLabel(stmt->getGotoStmtTargetLabel());
      if (curblock && ! curblock->getTerminator())
        llvm::BranchInst::Create(lbb, curblock);
      if (emitOrphanedCode_) {
        std::string n = be_->namegen("orphan");
        llvm::Function *func = function()->function();
        llvm::BasicBlock *orphan =
            llvm::BasicBlock::Create(context_, n, func, lbb);
        curblock = orphan;
      } else {
        curblock = nullptr;
      }
      break;
    }
    case N_LabelStmt: {
      llvm::BasicBlock *lbb =
          getBlockForLabel(stmt->getLabelStmtDefinedLabel());
      if (curblock)
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
  GenBlocks gb(context_, this, function, code_stmt,
               getDICompUnit(), createDebugMetaData_, entryBlock);
  llvm::BasicBlock *block = gb.walk(code_stmt, entryBlock);
  gb.finishFunction();

  // Fix up epilog block if needed
  fixupEpilogBlog(function, block);

  // debugging
  if (traceLevel() > 0) {
    std::cerr << "LLVM function dump:\n";
    function->function()->dump();
  }

  // Free up statement storage (stmts no longer needed at this point)
  nbuilder_.freeStmts();

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

  finalizeExportData();

  // At the moment there isn't anything to do here with the
  // inputs we're being passed.

}

// Post-process export data to escape quotes, etc, writing bytes
// to the specified stringstream.
static void postProcessExportDataChunk(const char *bytes,
                                       unsigned int size,
                                       std::stringstream &ss)
{
  std::map<char, std::string> rewrites = { { '\\', "\\\\" },
                                           { '\0', "\\000" },
                                           { '\n', "\\n" },
                                           { '"', "\\\"" } };

  for (unsigned idx = 0; idx < size; ++idx) {
    const char byte = bytes[idx];
    auto it = rewrites.find(byte);
    if (it != rewrites.end())
      ss << it->second;
    else
      ss << byte;
  }
}

// Finalize export data.

void Llvm_backend::finalizeExportData()
{
  // Calling this here, for lack of a better spot
  if (dibuilder_.get())
    dibuilder_->finalize();

  assert(! exportDataFinalized_);
  exportDataFinalized_ = true;
  module().appendModuleInlineAsm("\t.text\n");

  if (traceLevel() > 1) {
    std::cerr << "Export data emitted:\n";
    std::cerr << module().getModuleInlineAsm();
  }
}

// This is called by the Go frontend proper to add data to the
// section containing Go export data.

void Llvm_backend::write_export_data(const char *bytes, unsigned int size)
{
  // FIXME: this is hacky and currently very ELF-specific. Better to
  // add real support in MC object file layer.

  assert(! exportDataFinalized_);

  if (! exportDataStarted_) {
    exportDataStarted_ = true;
    const char *preamble = "\t.section \".go_export\",\"e\",@progbits";
    module().appendModuleInlineAsm(preamble);
  }

  std::stringstream ss;
  ss << "\t.ascii \"";
  postProcessExportDataChunk(bytes, size, ss);
  ss << "\"\n";
  module().appendModuleInlineAsm(ss.str());
}


// Convert an identifier for use in an error message.
// TODO(tham): scan the identifier to determine if contains
// only ASCII or printable UTF-8, then perform character set
// conversions if needed.

const char *go_localize_identifier(const char *ident) { return ident; }

// Return a new backend generator.

Backend *go_get_backend(llvm::LLVMContext &context) {
  return new Llvm_backend(context, nullptr, nullptr);
}
