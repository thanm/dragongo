//===- llvm/tools/dragongo/unittests/BackendCore/TestUtils.cpp ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TestUtils.h"
#include "llvm/IR/DebugInfo.h"

namespace goBackendUnitTests {

std::string trimsp(const std::string &s) {
  size_t firstsp = s.find_first_not_of(" \n");
  if (firstsp == std::string::npos)
    return s;
  size_t lastsp = s.find_last_not_of(" \n");
  return s.substr(firstsp, (lastsp - firstsp + 1));
}

std::vector<std::string> tokenize(const std::string &s) {
  std::vector<std::string> tokens;

  const std::string del(" \t\n");
  size_t np = s.find_first_not_of(del, 0);
  size_t pos = s.find_first_of(del, np);
  while (pos != std::string::npos || np != std::string::npos) {
    tokens.push_back(s.substr(np, pos - np));
    np = s.find_first_not_of(del, pos);
    pos = s.find_first_of(del, np);
  }
  return tokens;
}

std::string vectostr(const std::vector<std::string> &tv) {
  std::stringstream ss;
  unsigned wc = 0;
  for (auto s : tv)
    ss << " " << wc++ << "[" << s << "]";
  return ss.str();
}

bool difftokens(const std::string &expected, const std::string &result,
                std::string &diffreason)
{
  std::vector<std::string> expv = tokenize(expected);
  std::vector<std::string> resv = tokenize(result);
  unsigned mins = std::min(expv.size(), resv.size());
  unsigned maxs = std::max(expv.size(), resv.size());
  std::stringstream ss;
  bool rval = true;
  if (expv.size() != resv.size()) {
    ss << "lengths differ (" << expv.size() << " vs " << resv.size()
       << ") extra " << (expv.size() > resv.size() ? "result" : "expected")
       << " tokens: ";
    for (unsigned idx = 0; idx < maxs; ++idx) {
      if (idx >= mins)
        ss << (idx < expv.size() ? expv[idx] : resv[idx]) << " ";
    }
    ss << "\n";
    rval = false;
  }
  for (unsigned idx = 0; idx < mins; ++idx) {
    if (expv[idx] != resv[idx]) {
      ss << "token vector diff at slot " << idx << " (expected '" << expv[idx]
         << "' result '" << resv[idx] << "')";
      rval = false;
      break;
    }
  }
  if (! rval)
    diffreason = ss.str();
  return rval;
}

bool containstokens(const std::string &text, const std::string &pat)
{
  std::vector<std::string> textToks = tokenize(text);
  std::vector<std::string> patToks = tokenize(pat);
  for (unsigned ti = 0; ti < textToks.size(); ++ti) {
    bool failed = false;
    for (unsigned tic = ti, pi = 0; pi < patToks.size(); ++pi, ++tic) {
      if (tic >= textToks.size() || patToks[pi] != textToks[tic]) {
        failed = true;
        break;
      }
    }
    if (failed)
      continue;
    return true;
  }
  return false;
}

std::string repr(llvm::Value *val) {
  std::string res;
  llvm::raw_string_ostream os(res);
  if (!val)
    os << "<null_value>";
  else
    val->print(os);
  return trimsp(os.str());
}

std::string repr(llvm::Type *t) {
  std::string res;
  llvm::raw_string_ostream os(res);
  if (t == NULL) {
    os << "<null_type>";
  } else {
    t->print(os);
  }
  return trimsp(os.str());
}

Btype *mkBackendStruct(Backend *be, ...)
{
  va_list ap;
  va_start(ap, be);

  std::vector<Backend::Btyped_identifier> fields;
  Btype *ct = va_arg(ap, Btype *);
  while (ct != nullptr) {
    Location loc;
    const char *fieldName = va_arg(ap, const char *);
    assert(fieldName);
    fields.push_back(Backend::Btyped_identifier(fieldName, ct, loc));
    ct = va_arg(ap, Btype *);
  }
  return be->struct_type(fields);
}

Btype *mkBackendThreeFieldStruct(Backend *be) {
  return mkBackendStruct(be,
                         be->bool_type(), "f1",
                         be->pointer_type(be->float_type(32)), "f2",
                         be->integer_type(true, 64), "f3",
                         nullptr);
}

llvm::StructType *mkLlvmStruct(llvm::LLVMContext *context, ...)
{
  va_list ap;
  va_start(ap, context);

  llvm::SmallVector<llvm::Type *, 64> smv;
  llvm::Type *typ = va_arg(ap, llvm::Type *);
  assert(typ);
  while (typ != nullptr) {
    smv.push_back(typ);
    typ = va_arg(ap, llvm::Type *);
  }
  return llvm::StructType::get(*context, smv);
}

llvm::StructType *mkLlvmThreeFieldStruct(llvm::LLVMContext &context) {
  llvm::Type *flt = llvm::Type::getFloatTy(context);
  return mkLlvmStruct(&context,
                      llvm::Type::getInt8Ty(context),
                      llvm::PointerType::get(flt, 0),
                      llvm::IntegerType::get(context, 64),
                      nullptr);
}

Btype *mkTwoFieldStruct(Backend *be, Btype *t1, Btype *t2)
{
  return mkBackendStruct(be, t1, "f1", t2, "f2", nullptr);
}

llvm::Type *mkTwoFieldLLvmStruct(llvm::LLVMContext &context,
                                 llvm::Type *t1, llvm::Type *t2)
{
  return mkLlvmStruct(&context, t1, t2, nullptr);
}

Backend::Btyped_identifier mkid(Btype *t) {
  static unsigned ctr = 1;
  char buf[128];
  sprintf(buf, "id%u", ctr++);
  return Backend::Btyped_identifier(buf, t, Location());
}

BFunctionType *mkFuncTyp(Backend *be, ...) {
  va_list ap;

  va_start(ap, be);

  Backend::Btyped_identifier receiver("rec", NULL, Location());
  std::vector<Backend::Btyped_identifier> results;
  std::vector<Backend::Btyped_identifier> params;
  Btype *result_type = NULL;

  unsigned tok = va_arg(ap, unsigned);
  while (tok != L_END) {
    switch (tok) {
    case L_RCV:
      receiver.btype = va_arg(ap, Btype *);
      break;
    case L_PARM:
      params.push_back(mkid(va_arg(ap, Btype *)));
      break;
    case L_RES:
      results.push_back(mkid(va_arg(ap, Btype *)));
      break;
    case L_RES_ST:
      result_type = va_arg(ap, Btype *);
      break;
    default: {
      assert(false && "internal error");
      return NULL;
    }
    }
    tok = va_arg(ap, unsigned);
  }

  if (results.size() > 1)
    result_type = be->struct_type(results);

  Btype *ft = be->function_type(receiver, params, results,
                                result_type, Location());
  return ft->castToBFunctionType();
}

llvm::Type *mkLLFuncTyp(llvm::LLVMContext *context, ...) {
  va_list ap;

  llvm::SmallVector<llvm::Type *, 4> params(0);
  llvm::SmallVector<llvm::Type *, 4> results(0);
  llvm::Type *recv_typ = NULL;
  llvm::Type *res_styp = NULL;

  va_start(ap, context);
  unsigned tok = va_arg(ap, unsigned);
  while (tok != L_END) {
    switch (tok) {
    case L_RCV:
      recv_typ = va_arg(ap, llvm::Type *);
      break;
    case L_PARM:
      params.push_back(va_arg(ap, llvm::Type *));
      break;
    case L_RES:
      results.push_back(va_arg(ap, llvm::Type *));
      break;
    case L_RES_ST:
      res_styp = va_arg(ap, llvm::Type *);
      break;
    default: {
      assert(false && "internal error");
      return NULL;
    }
    }
    tok = va_arg(ap, unsigned);
  }

  llvm::SmallVector<llvm::Type *, 4> elems(0);
  llvm::Type *llit = llvm::IntegerType::get(*context, 8);
  elems.push_back(llvm::PointerType::get(llit, 0));
  if (recv_typ)
    elems.push_back(recv_typ);
  for (auto pt : params)
    elems.push_back(pt);

  llvm::Type *rtyp = NULL;
  if (results.empty())
    rtyp = llvm::Type::getVoidTy(*context);
  else if (results.size() == 1)
    rtyp = results[0];
  else {
    rtyp = res_styp;
  }
  return llvm::FunctionType::get(rtyp, elems, false);
}

bool llvmTypesEquiv(llvm::Type *t1, llvm::Type *t2)
{
  if (t1->getTypeID() != t2->getTypeID())
    return false;
  if (t1->getNumContainedTypes() != t2->getNumContainedTypes())
    return false;
  for (unsigned idx = 0; idx < t1->getNumContainedTypes(); ++idx)
    if (!llvmTypesEquiv(t1->getContainedType(idx),
                        t2->getContainedType(idx)))
      return false;
  return true;
}

Bfunction *mkFunci32o64(Backend *be, const char *fname, bool mkParams) {
  Btype *bi64t = be->integer_type(false, 64);
  Btype *bi32t = be->integer_type(false, 32);
  Btype *bpi64t = be->pointer_type(bi64t);
  Btype *befty =
      mkFuncTyp(be,
                L_PARM, bi32t, L_PARM, bi32t, L_PARM, bpi64t,
                L_RES, bi64t, L_END);
  bool visible = true;
  bool is_declaration = false;
  bool is_inl = true;
  bool split_stack = true;
  bool unique_sec = false;
  Location loc;
  Bfunction *func = be->function(befty, fname, fname, visible,
                                 is_declaration, is_inl,
                                 split_stack, unique_sec, loc);
  if (mkParams) {
    be->parameter_variable(func, "param1", bi32t, false, loc);
    be->parameter_variable(func, "param2", bi32t, false, loc);
    be->parameter_variable(func, "param3", bpi64t, false, loc);
  }
  return func;
}

Bfunction *mkFuncFromType(Backend *be, const char *fname, BFunctionType *befty)
{
  bool visible = true;
  bool is_declaration = false;
  bool is_inl = true;
  bool split_stack = true;
  bool unique_sec = false;
  Location loc;
  Bfunction *func = be->function(befty, fname, fname, visible,
                                 is_declaration, is_inl,
                                 split_stack, unique_sec, loc);
  const std::vector<Btype *> &paramTypes = befty->paramTypes();
  for (unsigned idx = 0; idx < paramTypes.size(); ++idx) {
    std::stringstream ss;
    ss << "p" << idx;
    be->parameter_variable(func, ss.str(), paramTypes[idx], false, loc);
  }
  return func;
}

Bexpression *mkUIntConst(Backend *be, uint64_t val, unsigned bits) {
  mpz_t mpz_val;
  memset(&mpz_val, '0', sizeof(mpz_val));
  mpz_init_set_ui(mpz_val, val);
  Btype *but = be->integer_type(true, bits);
  Bexpression *rval = be->integer_constant_expression(but, mpz_val);
  mpz_clear(mpz_val);
  return rval;
}

Bexpression *mkIntConst(Backend *be, int64_t val, unsigned bits) {
  mpz_t mpz_val;
  memset(&mpz_val, '0', sizeof(mpz_val));
  mpz_init_set_si(mpz_val, val);
  Btype *bit = be->integer_type(false, bits);
  Bexpression *rval = be->integer_constant_expression(bit, mpz_val);
  mpz_clear(mpz_val);
  return rval;
}

Bexpression *mkInt64Const(Backend *be, int64_t val) {
  return mkIntConst(be, val, 64);
}

Bexpression *mkUint64Const(Backend *be, uint64_t val) {
  return mkUIntConst(be, val, 64);
}

Bexpression *mkInt32Const(Backend *be, int32_t val) {
  return mkIntConst(be, val, 32);
}

Bexpression *mkFloat64Const(Backend *be, double val) {
  mpfr_t mpfr_val;

  mpfr_init(mpfr_val);
  mpfr_set_d(mpfr_val, val, GMP_RNDN);
  Btype *bf64t = be->float_type(64);
  Bexpression *beval = be->float_constant_expression(bf64t, mpfr_val);
  mpfr_clear(mpfr_val);
  return beval;
}

// Return func desc type
Btype *mkFuncDescType(Backend *be)
{
  assert(be);
  Btype *bt = be->bool_type();
  Btype *pbt = be->pointer_type(bt);
  Btype *uintptrt = be->integer_type(true, be->type_size(pbt)*8);
  Btype *fdescst = mkBackendStruct(be, uintptrt, "f1", nullptr);
  return fdescst;
}

Bexpression *mkFuncDescExpr(Backend *be, Bfunction *fcn)
{
  assert(be);
  assert(fcn);
  Location loc;
  Btype *bt = be->bool_type();
  Btype *pbt = be->pointer_type(bt);
  Btype *uintptrt = be->integer_type(true, be->type_size(pbt)*8);
  Btype *fdescst = mkFuncDescType(be);
  Bexpression *fp = be->function_code_expression(fcn, loc);
  Bexpression *fpuint = be->convert_expression(uintptrt, fp, loc);
  std::vector<Bexpression *> vals;
  vals.push_back(fpuint);
  return be->constructor_expression(fdescst, vals, loc);
}

Bblock *mkBlockFromStmt(Backend *be, Bfunction *func, Bstatement *st) {
  const std::vector<Bvariable *> empty;
  Bblock *b = be->block(func, nullptr, empty, Location(), Location());
  std::vector<Bstatement *> stlist;
  stlist.push_back(st);
  be->block_add_statements(b, stlist);
  return b;
}

Bstatement *addStmtToBlock(Backend *be, Bblock *block, Bstatement *st) {
  std::vector<Bstatement *> stlist;
  stlist.push_back(st);
  be->block_add_statements(block, stlist);
  return st;
}

Bstatement *addExprToBlock(Backend *be, Bfunction *func,
                           Bblock *block, Bexpression *e) {
  Bstatement *es = be->expression_statement(func, e);
  std::vector<Bstatement *> stlist;
  stlist.push_back(es);
  be->block_add_statements(block, stlist);
  return es;
}

class NodeReprVisitor {
 public:
  NodeReprVisitor() : os_(str_) { }

  std::string result() { return os_.str(); }

  void visitNodePre(Bnode *node) { }

  void visitNodePost(Bnode *node) {
    assert(node);
    Bexpression *expr = node->castToBexpression();
    if (expr) {
      for (auto inst : expr->instructions()) {
        inst->print(os_);
        os_ << "\n";
      }
    }
    assert(node->flavor() != N_IfStmt &&
           node->flavor() != N_GotoStmt &&
           node->flavor() != N_LabelStmt &&
           node->flavor() != N_SwitchStmt);
  }

 private:
  std::string str_;
  llvm::raw_string_ostream os_;
};

std::string repr(Bnode *node) {
  if (!node)
    return "<null Bnode ptr>";

  NodeReprVisitor vis;
  simple_walk_nodes(node, vis);
  return trimsp(vis.result());
}

FcnTestHarness::FcnTestHarness(const char *fcnName)
    : context_()
    , be_(new Llvm_backend(context_, nullptr, nullptr))
    , func_(nullptr)
    , entryBlock_(nullptr)
    , curBlock_(nullptr)
    , nextLabel_(nullptr)
    , lineNum_(1)
    , finished_(false)
    , returnAdded_(false)
    , emitDumpFilesOnDiff_(false)
{
  // establish initial file so as to make verifier happy
  be_->linemap()->start_file("unit_testing.go", 1);
  loc_ = be_->linemap()->get_location(lineNum_);

  // Eager function creation if name not specified
  if (fcnName) {
    func_ = mkFunci32o64(be(), fcnName);
    entryBlock_ = be()->block(func_, nullptr, emptyVarList_, loc_, loc_);
    curBlock_ = be()->block(func_, nullptr, emptyVarList_, loc_, loc_);
  }

  // debugging
  if (getenv("DRAGONGO_UNITTESTS_BACKENDCORE_EMITDUMPFILES"))
    emitDumpFilesOnDiff_ = true;
}

FcnTestHarness::~FcnTestHarness()
{
  assert(finished_);
}

Location FcnTestHarness::newloc()
{
  loc_ = be_->linemap()->get_location(++lineNum_);
  return loc_;
}

Bfunction *FcnTestHarness::mkFunction(const char *fcnName, BFunctionType *befty)
{
  func_ = mkFuncFromType(be(), fcnName, befty);
  entryBlock_ = be()->block(func_, nullptr, emptyVarList_, loc_, loc_);
  curBlock_ = be()->block(func_, nullptr, emptyVarList_, loc_, loc_);
  return func_;
}

Bvariable *FcnTestHarness::mkLocal(const char *name,
                                   Btype *typ,
                                   Bexpression *init)
{
  assert(func_);
  Bvariable *v = be()->local_variable(func_, name, typ, true, loc_);
  if (!init)
    init = be()->zero_expression(typ);
  Bstatement *is = be()->init_statement(func_, v, init);
  addStmtToBlock(be(), curBlock_, is);
  return v;
}

void FcnTestHarness::mkAssign(Bexpression *lhs,
                              Bexpression *rhs,
                              AppendDisp disp)
{
  assert(func_);
  Bstatement *as = be()->assignment_statement(func_, lhs, rhs, loc_);
  if (disp == YesAppend)
    addStmtToBlock(be(), curBlock_, as);
}

Bstatement *FcnTestHarness::mkExprStmt(Bexpression *expr, AppendDisp disp)
{
  assert(func_);
  Bstatement *es = be()->expression_statement(func_, expr);
  if (disp == YesAppend)
    addStmtToBlock(be(), curBlock_, es);
  return es;
}

Bexpression *FcnTestHarness::mkCallExpr(Backend *be, Bfunction *fun, ...)
{
  va_list ap;

  va_start(ap, fun);
  Bexpression *fn = be->function_code_expression(fun, loc_);
  std::vector<Bexpression *> args;
  Bexpression *e = va_arg(ap, Bexpression *);
  while (e) {
    args.push_back(e);
    e = va_arg(ap, Bexpression *);
  }
  Bexpression *call = be->call_expression(func_, fn, args, nullptr, loc_);
  return call;
}

Bstatement *FcnTestHarness::mkReturn(Bexpression *expr, AppendDisp disp)
{
  std::vector<Bexpression *> vals;
  vals.push_back(expr);
  return mkReturn(vals, disp);
}

Bstatement *FcnTestHarness::mkReturn(const std::vector<Bexpression *> &vals,
                                     AppendDisp disp)
{
  assert(func_);
  Bstatement *ret = be()->return_statement(func_, vals, loc_);
  if (disp == YesAppend) {
    addStmtToBlock(be(), curBlock_, ret);
    returnAdded_ = true;
  }
  return ret;
}

Bstatement *FcnTestHarness::mkIf(Bexpression *cond,
                                 Bstatement *trueStmt,
                                 Bstatement *falseStmt,
                                 AppendDisp disp)
{
  assert(func_);
  Bblock *trueBlock = mkBlockFromStmt(be(), func_, trueStmt);
  Bblock *falseBlock = nullptr;
  if (falseStmt)
    falseBlock = mkBlockFromStmt(be(), func_, falseStmt);
  Bstatement *ifst = be()->if_statement(func_, cond,
                                        trueBlock, falseBlock, loc_);
  if (disp == YesAppend)
    addStmtToBlock(be(), curBlock_, ifst);
  return ifst;
}

Bstatement *FcnTestHarness::mkSwitch(Bexpression *swval,
                       const std::vector<std::vector<Bexpression*> >& cases,
                       const std::vector<Bstatement*>& statements,
                                     AppendDisp disp)
{
  assert(func_);
  Bstatement *swst = be()->switch_statement(func_, swval,
                                            cases, statements, loc_);
  if (disp == YesAppend)
    addStmtToBlock(be(), curBlock_, swst);
  return swst;
}


void FcnTestHarness::addStmt(Bstatement *stmt)
{
  assert(func_);
  addStmtToBlock(be(), curBlock_, stmt);
}

void FcnTestHarness::newBlock(std::vector<Bvariable *> *varlist)
{
  assert(func_);

  if (!varlist)
    varlist = &emptyVarList_;

  // Create label for new block and append jump to current block
  std::string lab = be()->namegen("_lab");
  Blabel *blab = be()->label(func_, lab, loc_);
  Bstatement *gots = be()->goto_statement(blab, loc_);
  addStmtToBlock(be(), curBlock_, gots);

  // Turn current block into statement and tack onto entry block. Weird,
  // but this is the pardigm for gofrontend.
  Bstatement *bst = be()->block_statement(curBlock_);
  if (nextLabel_) {
    Bstatement *ldef = be()->label_definition_statement(nextLabel_);
    bst = be()->compound_statement(ldef, bst);
  }
  addStmtToBlock(be(), entryBlock_, bst);
  nextLabel_ = blab;
  returnAdded_ = false;

  // New block
  curBlock_ = be()->block(func_, nullptr, *varlist, loc_, loc_);
}

static void emitStringToDumpFile(const char *tag,
                                 unsigned version,
                                 const std::string &payload)
{
  std::stringstream ss;
  ss << "/tmp/" << tag << ".dump." << version << ".txt";
  FILE *fp = fopen(ss.str().c_str(), "w");
  if (fp) {
    fprintf(fp, "%s\n", payload.c_str());
    fclose(fp);
    std::cerr << "emitted dump file " << ss.str() << "\n";
  }
}

static void complainOnNequal(const std::string &reason,
                             const std::string &expected,
                             const std::string &actual,
                             bool emitDump)
{
  std::cerr << reason << "\n";
  std::cerr << "expected dump:\n" << expected << "\n";
  std::cerr << "statement dump:\n" << actual << "\n";
  if (emitDump) {
    static unsigned filecount;
    emitStringToDumpFile("expected", filecount, expected);
    emitStringToDumpFile("actual", filecount, actual);
    filecount++;
  }
}

bool FcnTestHarness::expectStmt(Bstatement *st, const std::string &expected)
{
 std::string reason;
 std::string actual(repr(st));
  bool equal = difftokens(expected, actual, reason);
  if (! equal)
    complainOnNequal(reason, expected, actual, emitDumpFilesOnDiff_);
  return equal;
}

bool FcnTestHarness::expectValue(llvm::Value *val, const std::string &expected)
{
  std::string reason;
  std::string actual(repr(val));
  bool equal = difftokens(expected, actual, reason);
  if (! equal)
    complainOnNequal(reason, expected, actual, emitDumpFilesOnDiff_);
  return equal;
}

bool FcnTestHarness::expectBlock(const std::string &expected)
{
  std::string reason;
  std::string actual(repr(curBlock_));
  bool equal = difftokens(expected, actual, reason);
  if (! equal)
    complainOnNequal(reason, expected, actual, emitDumpFilesOnDiff_);
  return equal;
}

bool FcnTestHarness::expectRepr(Bnode *node, const std::string &expected)
{
  std::string reason;
  std::string actual(repr(node));
  bool equal = difftokens(expected, actual, reason);
  if (! equal)
    complainOnNequal(reason, expected, actual, emitDumpFilesOnDiff_);
  return equal;
}

bool FcnTestHarness::finish(DebugDisposition whatToDoWithDebugInfo)
{
  // Emit a label def for the pending block if needed
  Bstatement *bst = be()->block_statement(curBlock_);
  if (nextLabel_) {
    Bstatement *ldef = be()->label_definition_statement(nextLabel_);
    bst = be()->compound_statement(ldef, bst);
  }

  // Add current block as stmt to entry block
  addStmtToBlock(be(), entryBlock_, bst);

  // Set function body
  be()->function_set_body(func_, entryBlock_);

  // Finalize export data. This has the side effect of finalizing
  // debug meta-data, which we need to do before invoking the verifier.
  be()->finalizeExportData();

  // Strip debug info now if requested
  if (whatToDoWithDebugInfo == StripDebugInfo)
    llvm::StripDebugInfo(be()->module());

  // Verify module
  bool broken = llvm::verifyModule(be()->module(), &llvm::dbgs());

  // Mark finished
  finished_ = true;
  curBlock_ = entryBlock_;

  return broken;
}

} // end namespace goBackEndUnitTests
