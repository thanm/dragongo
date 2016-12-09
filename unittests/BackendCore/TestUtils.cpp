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
  size_t firstsp = s.find_first_not_of(' ');
  if (firstsp == std::string::npos)
    return s;
  size_t lastsp = s.find_last_not_of(' ');
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

bool difftokens(const std::vector<std::string> &expv,
                const std::vector<std::string> &resv, std::string &diffreason) {
  std::stringstream ss;
  if (expv.size() != resv.size()) {
    ss << "lengths differ (" << expv.size() << " vs " << resv.size()
       << ") extra " << (expv.size() > resv.size() ? "result" : "expected")
       << " tokens: ";
    unsigned mins = std::min(expv.size(), resv.size());
    unsigned maxs = std::max(expv.size(), resv.size());
    for (unsigned idx = 0; idx < maxs; ++idx) {
      if (idx >= mins)
        ss << (idx < expv.size() ? expv[idx] : resv[idx]) << " ";
    }
    ss << " res:{" << vectostr(resv) << " }";
    diffreason = ss.str();
    return false;
  }
  for (unsigned idx = 0; idx < expv.size(); ++idx) {
    if (expv[idx] != resv[idx]) {
      ss << "token vector diff at slot " << idx << " (expected '" << expv[idx]
         << "' result '" << resv[idx] << "')";
      ss << " res:{" << vectostr(resv) << " }";
      diffreason = ss.str();
      return false;
    }
  }
  return true;
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

Btype *mkBackendThreeFieldStruct(Backend *be) {
  Btype *pfloat = be->pointer_type(be->float_type(32));
  Btype *u64 = be->integer_type(true, 64);
  std::vector<Backend::Btyped_identifier> fields = {
      Backend::Btyped_identifier("f1", be->bool_type(), Location()),
      Backend::Btyped_identifier("f2", pfloat, Location()),
      Backend::Btyped_identifier("f3", u64, Location())};
  return be->struct_type(fields);
}

llvm::StructType *mkLlvmThreeFieldStruct(llvm::LLVMContext &context) {
  llvm::SmallVector<llvm::Type *, 3> smv(3);
  smv[0] = llvm::Type::getInt1Ty(context);
  smv[1] = llvm::PointerType::get(llvm::Type::getFloatTy(context), 0);
  smv[2] = llvm::IntegerType::get(context, 64);
  return llvm::StructType::get(context, smv);
}

Btype *mkTwoFieldStruct(Backend *be, Btype *t1, Btype *t2) {
  std::vector<Backend::Btyped_identifier> fields = {
      Backend::Btyped_identifier("f1", t1, Location()),
      Backend::Btyped_identifier("f2", t2, Location())};
  return be->struct_type(fields);
}

llvm::Type *mkTwoFieldLLvmStruct(llvm::LLVMContext &context, llvm::Type *t1,
                                 llvm::Type *t2) {
  llvm::SmallVector<llvm::Type *, 2> smv(2);
  smv[0] = t1;
  smv[1] = t2;
  return llvm::StructType::get(context, smv);
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
  Btype *befty =
      mkFuncTyp(be, L_PARM, bi32t, L_PARM, bi32t, L_RES, bi64t, L_END);
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
  }
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

Bblock *mkBlockFromStmt(Backend *be, Bfunction *func, Bstatement *st) {
  const std::vector<Bvariable *> empty;
  Bblock *b = be->block(func, nullptr, empty, Location(), Location());
  std::vector<Bstatement *> stlist;
  stlist.push_back(st);
  be->block_add_statements(b, stlist);
  return b;
}

void addStmtToBlock(Backend *be, Bblock *block, Bstatement *st) {
  std::vector<Bstatement *> stlist;
  stlist.push_back(st);
  be->block_add_statements(block, stlist);
}

void addExprToBlock(Backend *be, Bfunction *func,
                    Bblock *block, Bexpression *e) {
  Bstatement *es = be->expression_statement(func, e);
  std::vector<Bstatement *> stlist;
  stlist.push_back(es);
  be->block_add_statements(block, stlist);
}

std::string repr(Bstatement *statement) {
  if (!statement)
    return "<null Bstatement>";
  ExprListStatement *elst = statement->castToExprListStatement();
  if (elst) {
    std::stringstream ss;
    bool first = true;
    for (auto expr : elst->expressions()) {
      if (!first)
        ss << "\n";
      first = false;
      ss << repr(expr);
    }
    return ss.str();
  }
  CompoundStatement *cst = statement->castToCompoundStatement();
  if (cst) {
    std::stringstream ss;
    for (auto st : cst->stlist())
      ss << repr(st) << "\n";
    return ss.str();
  }
  return "<unsupported stmt type>";
}

std::string repr(Bexpression *expr) {
  if (!expr)
    return "<null Bexpression>";
  std::stringstream ss;
  bool first = true;
  for (auto inst : expr->instructions()) {
    if (!first)
      ss << "\n";
    first = false;
    ss << repr(inst);
  }
  if (first)
    ss << repr(expr->value());
  return ss.str();
}

} // end namespace goBackEndUnitTests
