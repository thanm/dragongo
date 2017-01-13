//===- llvm/tools/dragongo/unittests/BackendCore/TestUtils.cpp ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TestUtils.h"

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
  assert(ct);
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

Btype *mkFuncTyp(Backend *be, ...) {
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
      assert("internal error");
      return NULL;
    }
    }
    tok = va_arg(ap, unsigned);
  }
  Location loc;
  return be->function_type(receiver, params, results, result_type, loc);
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

Bfunction *mkFuncFromType(Backend *be, const char *fname, Btype *befty)
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
  return func;
}

Bexpression *mkUint64Const(Backend *be, uint64_t val) {
  mpz_t mpz_val;
  memset(&mpz_val, '0', sizeof(mpz_val));
  mpz_init_set_ui(mpz_val, val);
  Btype *bu64t = be->integer_type(true, 64);
  Bexpression *rval = be->integer_constant_expression(bu64t, mpz_val);
  mpz_clear(mpz_val);
  return rval;
}

Bexpression *mkInt64Const(Backend *be, int64_t val) {
  mpz_t mpz_val;
  memset(&mpz_val, '0', sizeof(mpz_val));
  mpz_init_set_si(mpz_val, val);
  Btype *bi64t = be->integer_type(false, 64);
  Bexpression *rval = be->integer_constant_expression(bi64t, mpz_val);
  mpz_clear(mpz_val);
  return rval;
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

Bexpression *mkInt32Const(Backend *be, int32_t val)
{
  mpz_t mpz_val;
  memset(&mpz_val, '0', sizeof(mpz_val));
  mpz_init_set_si(mpz_val, int64_t(val));
  Btype *bi32t = be->integer_type(false, 32);
  Bexpression *rval = be->integer_constant_expression(bi32t, mpz_val);
  mpz_clear(mpz_val);
  return rval;
}

// Produce a call expression targeting the specified function. Variable
// args are parameter values, terminated by nullptr.
Bexpression *mkCallExpr(Backend *be, Bfunction *fun, ...)
{
  Location loc;
  va_list ap;

  va_start(ap, fun);
  Bexpression *fn = be->function_code_expression(fun, loc);
  std::vector<Bexpression *> args;
  Bexpression *e = va_arg(ap, Bexpression *);
  while (e) {
    args.push_back(e);
    e = va_arg(ap, Bexpression *);
  }
  Bexpression *call = be->call_expression(fn, args, nullptr, loc);
  return call;
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

std::string repr(Bstatement *statement) {
  if (!statement)
    return "<null Bstatement>";
  std::string res;
  llvm::raw_string_ostream os(res);
  bool terse = true;
  statement->osdump(os, 0, terse);
  return trimsp(os.str());
}

std::string repr(Bexpression *expr) {
  if (!expr)
    return "<null Bexpression>";
  std::string res;
  llvm::raw_string_ostream os(res);
  bool terse = true;
  expr->osdump(os, 0, terse);
  return trimsp(os.str());
}

FcnTestHarness::FcnTestHarness(const char *fcnName)
    : context_()
    , be_(new Llvm_backend(context_))
    , func_(nullptr)
    , entryBlock_(nullptr)
    , curBlock_(nullptr)
    , nextLabel_(nullptr)
    , finished_(false)
    , returnAdded_(false)
{
  // Eager function creation if name not specified
  if (fcnName) {
    func_ = mkFunci32o64(be(), fcnName);
    entryBlock_ = be()->block(func_, nullptr, emptyVarList_, loc_, loc_);
    curBlock_ = be()->block(func_, nullptr, emptyVarList_, loc_, loc_);
  }
}

FcnTestHarness::~FcnTestHarness()
{
  assert(finished_);
}

Bfunction *FcnTestHarness::mkFunction(const char *fcnName, Btype *befty)
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

void FcnTestHarness::mkAssign(Bexpression *lhs, Bexpression *rhs)
{
  assert(func_);
  Bstatement *as = be()->assignment_statement(func_, lhs, rhs, loc_);
  addStmtToBlock(be(), curBlock_, as);
}

Bstatement *FcnTestHarness::mkExprStmt(Bexpression *expr)
{
  assert(func_);
  return addExprToBlock(be(), func_, curBlock_, expr);
}

Bstatement *FcnTestHarness::mkReturn(Bexpression *expr)
{
  assert(func_);
  std::vector<Bexpression *> vals;
  vals.push_back(expr);
  Bstatement *ret = be()->return_statement(func_, vals, loc_);
  addStmtToBlock(be(), curBlock_, ret);
  returnAdded_ = true;
  return ret;
}

void FcnTestHarness::addStmt(Bstatement *stmt)
{
  assert(func_);
  addStmtToBlock(be(), curBlock_, stmt);
}

void FcnTestHarness::newBlock()
{
  assert(func_);

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
  curBlock_ = be()->block(func_, nullptr, emptyVarList_, loc_, loc_);
}

bool FcnTestHarness::expectValue(llvm::Value *val, const std::string &expected)
{
  std::string reason;
  bool equal = difftokens(expected, repr(val), reason);
  if (! equal) {
    std::cerr << reason << "\n";
    std::cerr << "expected dump:\n" << expected << "\n";
    std::cerr << "value dump:\n" << repr(val) << "\n";
  }
  return equal;
}

bool FcnTestHarness::expectBlock(const std::string &expected)
{
  std::string reason;
  bool equal = difftokens(expected, repr(curBlock_), reason);
  if (! equal) {
    std::cerr << reason << "\n";
    std::cerr << "expected dump:\n" << expected << "\n";
    std::cerr << "block dump:\n" << repr(curBlock_) << "\n";
  }
  return equal;
}

bool FcnTestHarness::finish()
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

  // Verify module
  bool broken = llvm::verifyModule(be()->module(), &llvm::dbgs());

  // Mark finished
  finished_ = true;
  curBlock_ = entryBlock_;

  return broken;
}

} // end namespace goBackEndUnitTests
