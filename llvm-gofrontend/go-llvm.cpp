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
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"

typedef llvm::IRBuilder<> LIRBuilder;

#define CHKTREE(x) if (checkIntegrity_ && traceLevel()) \
      enforceTreeIntegrity(x)

static const auto NotInTargetLib = llvm::LibFunc::NumLibFuncs;

static void indent(llvm::raw_ostream &os, unsigned ilevel) {
  for (unsigned i = 0; i < ilevel; ++i)
    os << " ";
}

void Btype::dump()
{
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os, 0);
  std::cerr << os.str() << "\n";
}

void Btype::osdump(llvm::raw_ostream &os, unsigned ilevel) {
  indent(os, ilevel);
  if (isUnsigned())
    os << "[uns] ";
  if (! name_.empty())
    os << "'" << name_ << "' ";
  type_->print(os);
}

Bexpression::Bexpression(llvm::Value *value,
                         Btype *btype,
                         const std::string &tag)
    : value_(value), btype_(btype), stmt_(nullptr), tag_(tag)
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
  osdump(os, 0, false);
  std::cerr << os.str();
}

void Bexpression::osdump(llvm::raw_ostream &os, unsigned ilevel, bool terse) {
  bool hitValue = false;
  if (! terse && compositeInitPending()) {
    indent(os, ilevel);
    os << "composite init pending:\n";
    for (auto exp : compositeInitContext().elementExpressions())
      exp->osdump(os, ilevel+2, terse);
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
    stmt()->osdump(os, ilevel+2, terse);
  }
}

ExprListStatement *Llvm_backend::stmtFromExpr(Bfunction *function,
                                              Bexpression *expr) {
  ExprListStatement *st = new ExprListStatement(function);
  st->appendExpression(expr);
  CHKTREE(st);
  return st;
}

void Bstatement::dump() {
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os, 0, false);
  std::cerr << os.str();
}

void Bstatement::osdump(llvm::raw_ostream &os, unsigned ilevel, bool terse)
{
  switch (flavor()) {
  case ST_Compound: {
    CompoundStatement *cst = castToCompoundStatement();
    if (! terse) {
      indent(os, ilevel);
      os << ((void*)this) << " {\n";
    }
    for (auto st : cst->stlist())
      st->osdump(os, ilevel + 2, terse);
    if (! terse) {
      indent(os, ilevel);
      os << "}\n";
    }
    break;
  }
  case ST_ExprList: {
    ExprListStatement *elst = castToExprListStatement();
    if (! terse) {
      indent(os, ilevel);
      os << ((void*)this) << " [\n";
    }
    for (auto expr : elst->expressions())
      expr->osdump(os, ilevel + 2, terse);
    if (! terse) {
      indent(os, ilevel);
      os << "]\n";
    }
    break;
  }
  case ST_IfPlaceholder: {
    IfPHStatement *ifst = castToIfPHStatement();
    indent(os, ilevel);
    os << "if";
    if (! terse)
      os << " " << ((void*) ifst);
    os << ":\n";
    indent(os, ilevel + 2);
    os << "cond:\n";
    ifst->cond()->osdump(os, ilevel + 2, terse);
    if (ifst->trueStmt()) {
      indent(os, ilevel + 2);
      os << "then:\n";
      ifst->trueStmt()->osdump(os, ilevel + 2, terse);
    }
    if (ifst->falseStmt()) {
      indent(os, ilevel + 2);
      os << "else:\n";
      ifst->falseStmt()->osdump(os, ilevel + 2, terse);
    }
    break;
  }
  case ST_Goto: {
    GotoStatement *gst = castToGotoStatement();
    indent(os, ilevel);
    os << "goto L" << gst->targetLabel() << "\n";
    break;
  }
  case ST_Label: {
    LabelStatement *lbst = castToLabelStatement();
    indent(os, ilevel);
    os << "label L" << lbst->definedLabel() << "\n";
    break;
  }

  case ST_SwitchPlaceholder:
    os << "not yet implemented\n";
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
  case ST_ExprList: {
    ExprListStatement *elst = stmt->castToExprListStatement();
    for (auto expr : elst->expressions())
      Bexpression::destroy(expr, which);
    break;
  }
  case ST_IfPlaceholder: {
    IfPHStatement *ifst = stmt->castToIfPHStatement();
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

// This visitor class detects malformed IR trees, specifically cases
// where the same Bexpression or Bstatement is pointed to by more than
// containing expr/stmt. For example suppose we have a couple of assignment
// statements
//
//      x = y
//      z = y
//
// where the Bexpression corresponding to "y" is pointed to both by
// the first assignment stmt and by the second assignment stmt.

class IntegrityVisitor {
 public:
  IntegrityVisitor(const Llvm_backend *be, bool incPtrs)
      : be_(be), ss_(str_), includePointers_(incPtrs) { }

  bool visit(Bexpression *e);
  bool visit(Bstatement *e);
  std::string msg() { return ss_.str(); }

 private:
  const Llvm_backend *be_;
  std::unordered_map<llvm::Instruction *, Bexpression *> iparent_;
  typedef std::pair<Bexpression *, Bstatement *> eors;
  std::unordered_map<Bstatement *, eors> sparent_;
  std::unordered_map<Bexpression *, eors> eparent_;
  std::string str_;
  llvm::raw_string_ostream ss_;
  bool includePointers_;

 private:
  bool setParent(Bexpression *child, const eors &parent);
  bool setParent(Bstatement *child, const eors &parent);
  bool setParent(llvm::Instruction *inst, Bexpression *exprParent);
  eors makeExprParent(Bexpression *expr);
  eors makeStmtParent(Bstatement *stmt);
  void dumpTag(const char *tag, void *ptr);
  void dump(Bexpression *expr);
  void dump(Bstatement *stmt);
  void dump(llvm::Instruction *inst);
  void dump(const eors &pair);
};

IntegrityVisitor::eors IntegrityVisitor::makeExprParent(Bexpression *expr) {
  Bstatement *stmt = nullptr;
  eors rval(std::make_pair(expr, stmt));
  return rval;
}

IntegrityVisitor::eors IntegrityVisitor::makeStmtParent(Bstatement *stmt) {
  Bexpression *expr = nullptr;
  eors rval(std::make_pair(expr, stmt));
  return rval;
}

void IntegrityVisitor::dumpTag(const char *tag, void *ptr) {
  ss_ << tag << ": ";
  if (includePointers_)
    ss_ << ptr;
  ss_ << "\n";
}

void IntegrityVisitor::dump(Bexpression *expr) {
  dumpTag("expr", (void*) expr);
  expr->osdump(ss_, 0, false);
}

void IntegrityVisitor::dump(Bstatement *stmt) {
  dumpTag("stmt", (void*) stmt);
  stmt->osdump(ss_, 0, false);
}

void IntegrityVisitor::dump(llvm::Instruction *inst) {
  dumpTag("inst", (void*) inst);
  inst->print(ss_);
  ss_ << "\n";
}

void IntegrityVisitor::dump(const IntegrityVisitor::eors &pair) {
  Bexpression *expr = pair.first;
  Bstatement *stmt = pair.second;
  if (expr)
    dump(expr);
  else
    dump(stmt);
}

bool IntegrityVisitor::setParent(Bstatement *child, const eors &parent)
{
  auto it = sparent_.find(child);
  if (it != sparent_.end()) {
    ss_ << "error: statement has multiple parents\n";
    ss_ << "child stmt:\n";
    dump(child);
    ss_ << "parent 1:\n";
    dump(it->second);
    ss_ << "parent 2:\n";
    dump(parent);
    return false;
  }
  sparent_[child] = parent;
  return true;
}

bool IntegrityVisitor::setParent(Bexpression *child, const eors &parent)
{
  if (be_->moduleScopeValue(child->value(), child->btype()))
    return true;
  auto it = eparent_.find(child);
  if (it != eparent_.end()) {
    ss_ << "error: expression has multiple parents\n";
    ss_ << "child expr:\n";
    dump(child);
    ss_ << "parent 1:\n";
    dump(it->second);
    ss_ << "parent 2:\n";
    dump(parent);
    return false;
  }
  eparent_[child] = parent;
  return true;
}

bool IntegrityVisitor::setParent(llvm::Instruction *inst,
                                 Bexpression *exprParent)
{
  auto it = iparent_.find(inst);
  if (it != iparent_.end()) {
    ss_ << "error: instruction has multiple parents\n";
    dump(inst);
    ss_ << "parent 1:\n";
    dump(it->second);
    ss_ << "parent 2:\n";
    dump(exprParent);
    return false;
  }
  iparent_[inst] = exprParent;
  return true;
}

bool IntegrityVisitor::visit(Bexpression *expr)
{
  bool rval = true;
  if (expr->stmt()) {
    eors parthis = makeExprParent(expr);
    rval = (setParent(expr->stmt(), parthis) && rval);
    visit(expr->stmt());
  }
  if (expr->compositeInitPending()) {
    eors parthis = makeExprParent(expr);
    for (auto initval : expr->compositeInitContext().elementExpressions())
      rval = (setParent(initval, parthis) && rval);
  }
  for (auto inst : expr->instructions())
    rval = (setParent(inst, expr) && rval);
  return rval;
}

bool IntegrityVisitor::visit(Bstatement *stmt)
{
  bool rval = true;
  switch (stmt->flavor()) {
    case Bstatement::ST_Compound: {
      CompoundStatement *cst = stmt->castToCompoundStatement();
      for (auto st : cst->stlist())
        rval = (visit(st) && rval);
      eors parcst = makeStmtParent(cst);
      for (auto st : cst->stlist()) {
        rval = (setParent(st, parcst) && rval);
      }
      break;
    }
    case Bstatement::ST_ExprList: {
      ExprListStatement *elst = stmt->castToExprListStatement();
      for (auto expr : elst->expressions()) {
        rval = (visit(expr) && rval);
        eors stparent(std::make_pair(nullptr, elst));
        rval = (setParent(expr, stparent) && rval);
      }
      break;
    }
    case Bstatement::ST_IfPlaceholder: {
      IfPHStatement *ifst = stmt->castToIfPHStatement();
      eors parif = makeStmtParent(ifst);
      rval = (visit(ifst->cond()) && rval);
      rval = (setParent(ifst->cond(), parif) && rval);
      if (ifst->trueStmt()) {
        rval = (visit(ifst->trueStmt()) && rval);
        rval = (setParent(ifst->trueStmt(), parif) && rval);
      }
      if (ifst->falseStmt()) {
        rval = (visit(ifst->falseStmt()) && rval);
        rval = (setParent(ifst->falseStmt(), parif) && rval);
      }
      break;
    }
    case Bstatement::ST_Label:
    case Bstatement::ST_Goto:
      // not interesting
      break;
    case Bstatement::ST_SwitchPlaceholder:
      assert(false && "not yet implemented\n");
      break;
  }
  return rval;
}

void
Llvm_backend::verifyModule()
{
  bool broken = llvm::verifyModule(*module_.get(), &llvm::dbgs());
  assert(!broken && "Module not well-formed.");
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

Bfunction::Bfunction(llvm::Function *f, const std::string &asmName)
    : function_(f), asmName_(asmName), labelCount_(0),
      splitStack_(YesSplit) {}

Bfunction::~Bfunction() {
  for (auto ais : allocas_)
    delete ais;
  for (auto &kv : argToVal_)
    delete kv.second;
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
  argToVal_.clear();

  // Append allocas for local variables
  for (auto aa : allocas_)
    entry->getInstList().push_back(aa);
  allocas_.clear();

  // Param spills
  for (auto sp : spills)
    entry->getInstList().push_back(sp);
}

Blabel *Bfunction::newLabel() {
  Blabel *lb = new Blabel(this, labelCount_++);
  labelmap_.push_back(nullptr);
  labels_.push_back(lb);
  return lb;
}

Bstatement *Bfunction::newLabelDefStatement(Blabel *label) {
  assert(label);
  LabelStatement *st = new LabelStatement(label->function(), label->label());
  assert(labelmap_[label->label()] == nullptr);
  labelmap_[label->label()] = st;
  return st;
}

Bstatement *Bfunction::newGotoStatement(Blabel *label, Location location) {
  Bfunction *fn = label->function();
  GotoStatement *st = new GotoStatement(fn, label->label(), location);
  return st;
}

Llvm_backend::Llvm_backend(llvm::LLVMContext &context)
    : context_(context)
    , module_(new llvm::Module("gomodule", context))
    , datalayout_(module_->getDataLayout())
    , addressSpace_(0)
    , traceLevel_(0)
    , checkIntegrity_(true)
    , complexFloatType_(nullptr)
    , complexDoubleType_(nullptr)
    , errorType_(nullptr)
    , stringType_(nullptr)
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

  // For builtin creation
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

  // Predefined string type
  llvm::Type *st = llvm::PointerType::get(llvmInt8Type_, addressSpace_);
  stringType_ = makeAnonType(st);

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
  errorFunction_.reset(new Bfunction(ef, ""));

  // Reuse the error function as the value for error_expression
  errorExpression_.reset(
      new Bexpression(errorFunction_->function(), errorType_));

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
  for (auto &kv : valueVarMap_)
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

  if (isNew && traceLevel() > 1) {
    std::cerr << "\n^ struct type "
              << ((void*)st) << " [llvm type "
              << ((void*) lst) << "] fields:";
    for (unsigned i = 0; i < fields.size(); ++i)
      std::cerr << i << ": " << ((void*)fields[i].btype) << "\n";
    st->dump();
  }

  if (isNew) {
    for (unsigned i = 0; i < fields.size(); ++i) {
      structplusindextype spi(std::make_pair(st, i));
      auto it = fieldTypeMap_.find(spi);
      assert(it == fieldTypeMap_.end());
      fieldTypeMap_[spi] = fields[i].btype;
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

  llvm::Type *llpt = llvm::PointerType::get(lltot, addressSpace_);
  bool isNew = (anonTypemap_.find(llpt) == anonTypemap_.end());
  Btype *result = makeAnonType(llvm::PointerType::get(lltot, addressSpace_));
  if (isNew)
    pointerTypeMap_[result] = to_type;
  return result;
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
  Btype *rbtype = nullptr;
  if (results.empty())
    rbtype = makeAnonType(llvm::Type::getVoidTy(context_));
  else if (results.size() == 1) {
    if (results.front().btype == errorType_)
      return errorType_;
    rbtype = results.front().btype;
  } else {
    assert(result_struct != nullptr);
    rbtype = result_struct;
  }
  assert(rbtype != nullptr);
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
  bool isNew = (anonTypemap_.find(llft) == anonTypemap_.end());
  Btype *bt = makeAnonType(llft);
  if (isNew) {
    Btype *pft = pointer_type(bt);
    funcReturnTypeMap_[pft] = rbtype;
  }
  return bt;
}

Btype *Llvm_backend::array_type(Btype *element_btype, Bexpression *length) {
  if (length == errorExpression_.get() || element_btype == errorType_)
    return errorType_;

  llvm::ConstantInt *lc = llvm::dyn_cast<llvm::ConstantInt>(length->value());
  assert(lc);
  uint64_t asize = lc->getValue().getZExtValue();

  llvm::Type *llat = llvm::ArrayType::get(element_btype->type(), asize);
  Btype *ar_btype = makeAnonType(llat);
  bool isNew = (arrayElemTypeMap_.find(ar_btype) == arrayElemTypeMap_.end());
  if (isNew)
    arrayElemTypeMap_[ar_btype] = element_btype;
  return ar_btype;
}

Btype *Llvm_backend::functionReturnType(Btype *typ)
{
  assert(typ);
  assert(typ->type()->isPointerTy());
  auto it = funcReturnTypeMap_.find(typ);
  assert(it != funcReturnTypeMap_.end());
  return it->second;
}

Btype *Llvm_backend::elementTypeByIndex(Btype *btype, unsigned field_index)
{
  assert(btype);
  if (btype->type()->isStructTy()) {
    structplusindextype spi(std::make_pair(btype, field_index));
    auto it = fieldTypeMap_.find(spi);
    assert(it != fieldTypeMap_.end());
    return it->second;
  } else {
    auto it = arrayElemTypeMap_.find(btype);
    assert(it != arrayElemTypeMap_.end());
    return it->second;
  }
}

// LLVM doesn't directly support placeholder types other than opaque
// structs, so the general strategy for placeholders is to create an
// opaque struct (corresponding to the thing being pointed to) and then
// make a pointer to it. Since LLVM allows only a single opaque struct
// type with a given name within a given context, we generally throw
// out the name/location information passed into the placeholder type
// creation routines.

llvm::Type *Llvm_backend::makeOpaqueLlvmType(const char *tag) {
  std::string tname(namegen(tag));
  return llvm::StructType::create(context_, tname);
}

// Create a placeholder for a pointer type.

Btype *Llvm_backend::placeholder_pointer_type(const std::string &name,
                                              Location location, bool) {
  llvm::Type *opaque = makeOpaqueLlvmType("PT");
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

  if (traceLevel() > 1) {
    std::cerr << "\n^ placeholder pointer "
              << ((void*)placeholder) << " [llvm type "
              << ((void*) placeholder->type()) << "]"
              << " redirected to " << ((void*) to_type)
              << " [llvm type " << ((void*) to_type->type())
              << "]\n";
    placeholder->dump();
    to_type->dump();
  }
  assert(to_type->type()->isPointerTy());
  if (placeholders_.find(placeholder) == placeholders_.end()) {
    assert(placeholder->type() == to_type->type());
  } else {
    updatePlaceholderUnderlyingType(placeholder, to_type->type());
  }

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

// Create a placeholder for a struct type.

Btype *Llvm_backend::placeholder_struct_type(const std::string &name,
                                             Location location) {
  return makePlaceholderType(makeOpaqueLlvmType("ST"));
}

// Fill in the fields of a placeholder struct type.

bool Llvm_backend::set_placeholder_struct_type(
    Btype *placeholder, const std::vector<Btyped_identifier> &fields) {
  if (placeholder == errorType_)
    return false;
  Btype *stype = struct_type(fields);

  if (traceLevel() > 1) {
    std::cerr << "\n^ placeholder struct "
              << ((void*)placeholder) << " [llvm type "
              << ((void*) placeholder->type())
              << "] redirected to " << ((void*) stype)
              << " [llvm type " << ((void*) stype->type())
              << "]\n";
    placeholder->dump();
    stype->dump();
  }

  updatePlaceholderUnderlyingType(placeholder, stype->type());
  // update the field type map to insure that the placeholder has the same info
  for (unsigned i = 0; i < fields.size(); ++i) {
    structplusindextype spi(std::make_pair(stype, i));
    auto it = fieldTypeMap_.find(spi);
    assert(it != fieldTypeMap_.end());
    structplusindextype ppi(std::make_pair(placeholder, i));
    fieldTypeMap_[ppi] = it->second;
  }
  return true;
}

// Create a placeholder for an array type.

Btype *Llvm_backend::placeholder_array_type(const std::string &name,
                                            Location location) {
  return makePlaceholderType(makeOpaqueLlvmType("AT"));
}

// Fill in the components of a placeholder array type.

bool Llvm_backend::set_placeholder_array_type(Btype *placeholder,
                                              Btype *element_btype,
                                              Bexpression *length) {
  if (placeholder == errorType_ || element_btype == errorType_ ||
      length == errorExpression_.get())
    return false;

  if (traceLevel() > 1) {
    std::string ls;
    llvm::raw_string_ostream os(ls);
    length->osdump(os, 0);
    std::cerr << "\n^ placeholder array "
              << ((void*)placeholder) << " [llvm type "
              << ((void*) placeholder->type())
              << "] element type updated to " << ((void*) element_btype)
              << " [llvm type " << ((void*) element_btype->type())
              << "] length: " << ls
              << "\n";
    placeholder->dump();
    element_btype->dump();
  }

  Btype *atype = array_type(element_btype, length);
  updatePlaceholderUnderlyingType(placeholder, atype->type());

  // update the array element type map to insure that the placeholder
  // has the same info
  assert(arrayElemTypeMap_.find(placeholder) == arrayElemTypeMap_.end());
  arrayElemTypeMap_[placeholder] = element_btype;
  return true;
}

// Return a named version of a type.

Btype *Llvm_backend::named_type(const std::string &name,
                                Btype *btype,
                                Location location)
{
  // TODO: add support for debug metadata

  // In the LLVM type world, all types are nameless except for so-called
  // identified struct types. Record the fact that we have a named
  // type with a specified LLVM type using a side data structure. NB: this
  // may turn out to not be needed, but for now it is helpful with debugging.

  named_llvm_type cand(name, btype->type());
  auto it = namedTypemap_.find(cand);
  if (it != namedTypemap_.end())
    return it->second;
  Btype *rval = new Btype(btype->type(), name);
  if (btype->isUnsigned())
    rval->setUnsigned();
  namedTypemap_[cand] = rval;

  if (traceLevel() > 1) {
    std::cerr << "\n^ named type '" << name << "' "
              << ((void*)rval) << " [llvm type "
              << ((void*) btype->type()) << "]\n";
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
  Btype *rval = makeAnonType(circ_ptr_typ);
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
  Bfunction *bfunc = new Bfunction(fcn, name);
  assert(builtinMap_.find(name) == builtinMap_.end());
  builtinMap_[name] = bfunc;
  if (libname) {
    Bfunction *bfunc = new Bfunction(fcn, libname);
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

Bexpression *Llvm_backend::makeValueExpression(llvm::Value *val, Btype *btype,
                                               ValExprScope scope) {
  assert(val || scope == LocalScope);
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

// Return the zero value for a type.

Bexpression *Llvm_backend::zero_expression(Btype *btype) {
  if (btype == errorType_)
    return errorExpression_.get();
  llvm::Value *zeroval = llvm::Constant::getNullValue(btype->type());
  return makeValueExpression(zeroval, btype, GlobalScope);
}

Bexpression *Llvm_backend::error_expression() { return errorExpression_.get(); }

Bexpression *Llvm_backend::nil_pointer_expression() {

  // What type should we assign a NIL pointer expression? This
  // is something of a head-scratcher. For now use uintptr.
  llvm::Type *pti = llvm::PointerType::get(llvmIntegerType_, addressSpace_);
  Btype *uintptrt = makeAnonType(pti);
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
  Bexpression *rval = makeExpression(ldinst, loadResultType, space, nullptr);
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
          makeValueExpression(expr->value(), btype, LocalScope);
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

  // Gofrontend tends to take the address of things like strings
  // and arrays, which is an issue here since an array type in LLVM
  // is already effectively a pointer (you can feed it directly into
  // a GEP as opposed to having to take the address of it first).
  // Bypass the effects of the address operator here if this is the
  // case. This is hacky, maybe I can come up with a better solution
  // for this issue(?).
  if (llvm::isa<llvm::ConstantArray>(bexpr->value()))
    return bexpr;
  if (bexpr->value()->getType() == stringType_->type() &&
      llvm::isa<llvm::Constant>(bexpr->value()))
    return bexpr;

  // Create new expression with proper type.
  Btype *pt = pointer_type(bexpr->btype());
  Bexpression *rval =
      makeValueExpression(bexpr->value(), pt, LocalScope);
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
    return loadFromExpr(expr, btype, Location());
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
      makeValueExpression(var->value(), var->getType(), LocalScope);
  varexp->setTag(var->getName().c_str());
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

Bexpression *Llvm_backend::string_constant_expression(const std::string &val)
{
  if (val.size() == 0) {
    llvm::Value *zer = llvm::Constant::getNullValue(stringType_->type());
    return makeValueExpression(zer, stringType_, GlobalScope);
  }

  // At the moment strings are not commoned.
  bool doAddNull = true;
  llvm::Constant *scon =
      llvm::ConstantDataArray::getString(context_,
                                         llvm::StringRef(val),
                                         doAddNull);
  Bvariable *svar =
      makeModuleVar(makeAnonType(scon->getType()),
                    "", "", Location(), MV_Constant, MV_DefaultSection,
                    MV_NotInComdat, MV_DefaultVisibility,
                    llvm::GlobalValue::PrivateLinkage, scon, 1);
  llvm::Constant *varval = llvm::cast<llvm::Constant>(svar->value());
  llvm::Constant *bitcast =
      llvm::ConstantExpr::getBitCast(varval, stringType_->type());
  return makeValueExpression(bitcast, stringType_, LocalScope);
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

// This helper is designed to handle a couple of the cases that
// we see from the front end for pointer conversions. The FE introduces
// pointer type conversions (ex: *int8 => *int64) frequently when
// creating initializers for things like type descriptors; this
// translates fairly directly to the LLVM equivalent.
//
// Return value for this routine is NULL if the cast in question
// is not acceptable, or the correct "to" type if it is.

llvm::Type *Llvm_backend::isAcceptableBitcastConvert(Bexpression *expr,
                                                     llvm::Type *fromType,
                                                     llvm::Type *toType)
{
  // Case 1: function pointer cast
  if (fromType->isPointerTy() && toType->isPointerTy()) {
    if (expr->varExprPending()) {
      llvm::Type *ptt = llvm::PointerType::get(toType, addressSpace_);
      llvm::Type *pet = llvm::PointerType::get(expr->btype()->type(),
                                               addressSpace_);
      if (fromType == pet)
        return ptt;
    }
    return toType;
  }
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
  if (type->type() == expr->value()->getType())
    return expr;

  // When the frontend casts something to function type, what this
  // really means in the LLVM realm is "pointer to function" type
  if (type->type()->isFunctionTy())
    type = pointer_type(type);

  LIRBuilder builder(context_, llvm::ConstantFolder());

  // Pointer type to pointer-sized-integer type. Comes up when
  // converting function pointer to function descriptor (during
  // creation of function descriptor vals) or constant array to
  // uintptr (as part of GC symbol initializer creation), and in other
  // places in FE-generated code (ex: array index checks).
  if (val->getType()->isPointerTy() && type->type() == llvmIntegerType_) {
    std::string tname(namegen("pticast"));
    llvm::Value *pticast = builder.CreatePtrToInt(val, type->type(), tname);
    ValExprScope scope =
        moduleScopeValue(val, expr->btype()) ? GlobalScope : LocalScope;
    Bexpression *rval = makeValueExpression(pticast, type, scope);
    rval->appendInstructions(expr->instructions());
    return rval;
  }

  // For pointer conversions (ex: *int32 => *int64) create an
  // appropriate bitcast.
  llvm::Type *toType =
      isAcceptableBitcastConvert(expr, val->getType(), type->type());
  if (toType) {
    std::string tag("cast");
    llvm::Value *bitcast = builder.CreateBitCast(val, toType, tag);
    Bexpression *rval = makeValueExpression(bitcast, type, LocalScope);
    rval->appendInstructions(expr->instructions());
    if (llvm::isa<llvm::Instruction>(bitcast))
      rval->appendInstruction(llvm::cast<llvm::Instruction>(bitcast));
    if (expr->varExprPending())
      rval->setVarExprPending(expr->varContext());
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
  Btype *fpBtype = makeAnonType(bfunc->function()->getType());

  // Return a pointer-to-function value expression
  return makeValueExpression(bfunc->function(), fpBtype, GlobalScope);
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
  Bexpression *rval = makeValueExpression(gep, bft, LocalScope);
  rval->appendInstructions(bstruct->instructions());
  if (llvm::isa<llvm::Instruction>(gep))
    rval->appendInstruction(llvm::cast<llvm::Instruction>(gep));
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
                       makeValueExpression(nilval, void_type(), LocalScope));
  Bexpression *result = compound_expression(ifStmt, rval, location);
  return result;
}

#if 0
  std::string tname(namegen("select"));
  llvm::Value *sel = builder.CreateSelect(condition->value(),
                                          then_expr->value(),
                                          else_expr->value(), tname);
  ValExprScope scope =
      (moduleScopeValue(then_expr->value(), then_expr->btype()) &&
       moduleScopeValue(else_expr->value(), else_expr->btype()) ?
       GlobalScope : LocalScope);
  Bexpression *rval = makeValueExpression(sel, btype, scope);
  rval->appendInstructions(condition->instructions());
  rval->appendInstructions(then_expr->instructions());
  rval->appendInstructions(else_expr->instructions());
  if (llvm::isa<llvm::Instruction>(sel))
    rval->appendInstruction(llvm::cast<llvm::Instruction>(sel));
  return rval;
#endif

// Return an expression for the unary operation OP EXPR.

Bexpression *Llvm_backend::unary_expression(Operator op, Bexpression *expr,
                                            Location location) {
  assert(false && "Llvm_backend::unary_expression not yet impl");
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
  llvm::Type *ltype = left->value()->getType();
  llvm::Type *rtype = right->value()->getType();
  assert(ltype == rtype);
  assert(bltype->isUnsigned() == brtype->isUnsigned());
  bool isUnsigned = bltype->isUnsigned();
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
      val = builder.CreateFCmp(pred, left->value(), right->value(),
                                 namegen("fcmp"));
    else
      val = builder.CreateICmp(pred, left->value(), right->value(),
                                 namegen("icmp"));
    break;
  }
  case OPERATOR_MINUS: {
    if (ltype->isFloatingPointTy())
      val = builder.CreateFSub(left->value(), right->value(),
                                 namegen("fsub"));
    else
      val = builder.CreateSub(left->value(), right->value(),
                                namegen("sub"));
    break;
  }
  case OPERATOR_PLUS: {
    if (ltype->isFloatingPointTy())
      val = builder.CreateFAdd(left->value(), right->value(),
                                 namegen("fadd"));
    else
      val = builder.CreateAdd(left->value(), right->value(),
                                namegen("add"));
    break;
  }
  case OPERATOR_OROR: {
    // Note that the FE will have already expanded out || in a control
    // flow context (short circuiting)
    assert(!ltype->isFloatingPointTy());
    val = builder.CreateOr(left->value(), right->value(), namegen("ior"));
    break;
  }
  case OPERATOR_ANDAND: {
    // Note that the FE will have already expanded out && in a control
    // flow context (short circuiting)
    assert(!ltype->isFloatingPointTy());
    val = builder.CreateAnd(left->value(), right->value(), namegen("iand"));
    break;
  }
  default:
    std::cerr << "Op " << op << "unhandled\n";
    assert(false);
  }

  return makeExpression(val, bltype, left, right, nullptr);
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
  Bexpression *ccon = makeValueExpression(nilval, btype, LocalScope);
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

    // This is unpleasant, but seems to be needed given the extensive
    // use of placeholder types in gofrontend.
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
  Bexpression *bcon = makeValueExpression(scon, btype, GlobalScope);
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
  Bexpression *rval = makeExpression(gep, bet, barray, index, nullptr);
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

  if (fn_expr->varExprPending())
    fn_expr = resolveVarContext(fn_expr);

  // Expect pointer-to-function type here
  assert(fn_expr->btype()->type()->isPointerTy());
  llvm::PointerType *pt =
      llvm::cast<llvm::PointerType>(fn_expr->btype()->type());
  llvm::FunctionType *llft =
      llvm::cast<llvm::FunctionType>(pt->getElementType());

  // FIXME: do something with location

  // Unpack / resolve arguments
  llvm::SmallVector<llvm::Value *, 64> llargs;
  llvm::SmallVector<Bexpression *, 64> resolvedArgs;
  for (auto fnarg : fn_args) {
    Bexpression *resarg = resolveVarContext(fnarg);
    resolvedArgs.push_back(resarg);
    llargs.push_back(resarg->value());
  }

  // Return type
  Btype *rbtype = functionReturnType(fn_expr->btype());

  // FIXME: create struct to hold result from multi-return call

  std::string callname(llft->getReturnType()->isVoidTy() ? "" : namegen("call"));
  llvm::CallInst *call =
      llvm::CallInst::Create(llft, fn_expr->value(), llargs, callname);
  Bexpression *rval =
      makeValueExpression(call, rbtype, LocalScope);
  rval->appendInstructions(fn_expr->instructions());
  for (auto resarg : resolvedArgs)
    rval->appendInstructions(resarg->instructions());
  rval->appendInstruction(call);
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
  if (init->compositeInitPending()) {
    init = resolveCompositeInit(init, bfunction, var->value());
    ExprListStatement *els = new ExprListStatement(bfunction);
    els->appendExpression(init);
    CHKTREE(els);
    return els;
  }
  init = resolveVarContext(init);
  Bstatement *st = makeAssignment(bfunction, var->value(),
                                  nullptr, init, Location());
  CHKTREE(st);
  return st;
}

Bexpression *Llvm_backend::makeExpression(llvm::Value *value,
                                          Btype *btype,
                                          Bexpression *srcExpr,
                                          ...)
{
  Bexpression *result = makeValueExpression(value, btype, LocalScope);
  Bexpression *src = srcExpr;
  va_list ap;
  std::set<llvm::Instruction *> visited;
  va_start(ap, srcExpr);
  while (src) {
    result->incorporateStmt(src->stmt());
    for (auto inst : src->instructions()) {
      assert(inst->getParent() == nullptr);
      result->appendInstruction(inst);
      assert(visited.find(inst) == visited.end());
      visited.insert(inst);
    }
    src = va_arg(ap, Bexpression *);
  }
  if (llvm::isa<llvm::Instruction>(value)) {
    // We need this guard to deal with situations where we've done
    // something like x | 0, in which the IRBuilder will simply return
    // the left operand.
    llvm::Instruction *inst = llvm::cast<llvm::Instruction>(value);
      if (visited.find(inst) == visited.end())
        result->appendInstruction(inst);
  }
  return result;
}

Bstatement *Llvm_backend::makeAssignment(Bfunction *function,
                                         llvm::Value *lval, Bexpression *lhs,
                                         Bexpression *rhs, Location location) {
  llvm::PointerType *pt = llvm::cast<llvm::PointerType>(lval->getType());
  assert(pt);
  llvm::Value *rval = rhs->value();
  assert(rval->getType() == pt->getElementType());

  // These cases should have been handled in the caller
  assert(!rhs->varExprPending() && !rhs->compositeInitPending());

  // FIXME: alignment?
  llvm::Instruction *si = new llvm::StoreInst(rval, lval);

  Bexpression *stexp = makeExpression(si, rhs->btype(), rhs, lhs, nullptr);
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
  Bexpression *rexp = makeExpression(ri, btype, toret, nullptr);
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
  IfPHStatement *ifst =
      new IfPHStatement(bfunction, condition, then_block, else_block, location);
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
  assert(s1->function() == s2->function());
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

  llvm::GlobalValue::LinkageTypes linkage =
      (is_external || is_hidden ? llvm::GlobalValue::ExternalLinkage
       : llvm::GlobalValue::InternalLinkage);
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

  Bfunction *bfunc = new Bfunction(fcn, asm_name);

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
  return new Llvm_backend(context);
}
