//===- llvm/tools/dragongo/unittests/BackendCore/TestUtils.h --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef DRAGONGO_UNITTESTS_BACKENDCORE_TESTUTILS_H
#define DRAGONGO_UNITTESTS_BACKENDCORE_TESTUTILS_H

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/raw_ostream.h"

// Currently these need to be included before backend.h
#include "go-location.h"
#include "go-linemap.h"
#include "backend.h"

#include "go-llvm.h"

#include <stdarg.h>

inline std::string trimsp(const std::string &s)
{
  size_t firstsp = s.find_first_not_of(' ');
  if (firstsp == std::string::npos)
    return s;
  size_t lastsp = s.find_last_not_of(' ');
  return s.substr(firstsp, (lastsp - firstsp + 1));
}

inline std::string repr(llvm::Value *val)
{
  std::string res;
  llvm::raw_string_ostream os(res);
  if (!val)
    os << "<null_value>";
  else
    val->print(os);
  return trimsp(os.str());
}

inline std::string repr(llvm::Type *t) {
  std::string res;
  llvm::raw_string_ostream os(res);
  if (t == NULL) {
    os << "<null_type>";
  } else {
    t->print(os);
  }
  return trimsp(os.str());
}

//
// Create this struct using backend interfaces:
//
//    struct {
//       bool f1;
//       float *f2;
//       uint64_t f3;
//    }

inline Btype *mkBackendThreeFieldStruct(Backend *be)
{
  Btype *pfloat = be->pointer_type(be->float_type(32));
  Btype *u64 = be->integer_type(true, 64);
  std::vector<Backend::Btyped_identifier> fields = {
    Backend::Btyped_identifier("f1", be->bool_type(), Location()),
    Backend::Btyped_identifier("f2", pfloat, Location()),
    Backend::Btyped_identifier("f3", u64, Location())
  };
  return be->struct_type(fields);
}

//
// Create this struct using LLVM interfaces:
//
//    struct {
//       bool f1;
//       float *f2;
//       uint64_t f3;
//    }

inline llvm::StructType *mkLlvmThreeFieldStruct(llvm::LLVMContext &context)
{
  llvm::SmallVector<llvm::Type *, 3> smv(3);
  smv[0] = llvm::Type::getInt1Ty(context);
  smv[1] = llvm::PointerType::get(llvm::Type::getFloatTy(context), 0);
  smv[2] = llvm::IntegerType::get(context, 64);
  return llvm::StructType::get(context, smv);
}

inline Btype *mkTwoFieldStruct(Backend *be, Btype *t1, Btype *t2)
{
  std::vector<Backend::Btyped_identifier> fields = {
    Backend::Btyped_identifier("f1", t1, Location()),
    Backend::Btyped_identifier("f2", t2, Location())
  };
  return be->struct_type(fields);
}

inline llvm::Type *mkTwoFieldLLvmStruct(llvm::LLVMContext &context, llvm::Type *t1, llvm::Type *t2) {
  llvm::SmallVector<llvm::Type *, 2> smv(2);
  smv[0] = t1;
  smv[1] = t2;
  return llvm::StructType::get(context, smv);
}

inline Backend::Btyped_identifier mkid(Btype *t)
{
  static unsigned ctr = 1;
  char buf[128];
  sprintf(buf, "id%u", ctr++);
  return Backend::Btyped_identifier(buf, t, Location());
}

typedef enum {
  L_END=0,   // end of list
  L_RCV,     // receiver type follows
  L_PARM,    // arg type follows
  L_RES,     // res type follows
  L_RES_ST,   // res struct type follows
} MkfToken;

inline Btype *mkFuncTyp(Backend *be, ...)
{
  va_list ap;

  va_start(ap, be);

  Backend::Btyped_identifier receiver("rec", NULL, Location());
  std::vector<Backend::Btyped_identifier> results;
  std::vector<Backend::Btyped_identifier> params;
  Btype *result_type = NULL;

  unsigned tok = va_arg(ap, unsigned);
  while (tok != L_END) {
    switch(tok) {
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

inline llvm::Type *mkLLFuncTyp(llvm::LLVMContext *context, ...)
{
  va_list ap;

  llvm::SmallVector<llvm::Type *, 4> params(0);
  llvm::SmallVector<llvm::Type *, 4> results(0);
  llvm::Type *recv_typ = NULL;
  llvm::Type *res_styp = NULL;

  va_start(ap, context);
  unsigned tok = va_arg(ap, unsigned);
  while (tok != L_END) {
    switch(tok) {
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

// Returns func:  foo(i1, i2 int32) int64 { }

inline Bfunction *mkFunci32o64(Backend *be, const char *fname) {
  Btype *bi64t = be->integer_type(false, 64);
  Btype *bi32t = be->integer_type(false, 32);
  Btype *befty = mkFuncTyp(be,
                           L_PARM, bi32t,
                           L_PARM, bi32t,
                           L_RES, bi64t,
                           L_END);
  bool visible = true;
  bool is_declaration = false;
  bool is_inl = true;
  bool split_stack = true;
  bool unique_sec = false;
  Location loc;
  return be->function(befty, fname, fname, visible,
                      is_declaration, is_inl, split_stack, unique_sec, loc);
}

// Manufacture an unsigned 64-bit integer constant

inline Bexpression *mkUint64Const(Backend *be, uint64_t val)
{
  mpz_t mpz_val;
  memset(&mpz_val, '0', sizeof(mpz_val));
  mpz_init_set_ui(mpz_val, val);
  Btype *bu64t = be->integer_type(true, 64);
  Bexpression *rval = be->integer_constant_expression(bu64t, mpz_val);
  mpz_clear(mpz_val);
  return rval;
}

// Manufacture a signed 64-bit integer constant

inline Bexpression *mkInt64Const(Backend *be, int64_t val)
{
  mpz_t mpz_val;
  memset(&mpz_val, '0', sizeof(mpz_val));
  mpz_init_set_si(mpz_val, val);
  Btype *bi64t = be->integer_type(false, 64);
  Bexpression *rval = be->integer_constant_expression(bi64t, mpz_val);
  mpz_clear(mpz_val);
  return rval;
}

// Create a basic block from a single statement

inline Bblock *mkBlockFromStmt(Backend *be, Bfunction *func, Bstatement *st)
{
  const std::vector<Bvariable*> empty;
  Bblock *b = be->block(func, nullptr, empty, Location(), Location());
  std::vector<Bstatement*> stlist;
  stlist.push_back(st);
  be->block_add_statements(b, stlist);
  return b;
}

// Works only for InstList stmts

inline std::string repr(Bstatement *statement)
{
  if (!statement)
    return "<null Bstatement>";
  InstListStatement *ilst = statement->castToInstListStatement();
  if (!ilst)
    return "<not an InstListStatement>";
  std::stringstream ss;
  for (auto inst : ilst->instructions())
    ss << repr(inst);
  return ss.str();
}

// Cleanup of statements created during unit testing.

class StmtCleanup {
 public:
  StmtCleanup(Backend *be) : be_(be) { }
  ~StmtCleanup() {
    for (auto s : statements_)
      if (s != be_->error_statement())
        Bstatement::destroy(s, DelBoth);
  }

  void add(Bstatement *s) { statements_.push_back(s); }

 protected:
  void del(Bstatement *s) {
    if (s == be_->error_statement())
      return;
    switch(s->flavor()) {
      case Bstatement::ST_Compound: {
        CompoundStatement *cst = s->castToCompoundStatement();
        for (auto st : cst->stlist())
          del(st);
        break;
      }
      case Bstatement::ST_InstList: {
        InstListStatement *ilst = s->castToInstListStatement();
        for (auto inst : ilst->instructions()) {
          delete inst;
        }
        break;
      }
      default:
        assert(false && "Not yet implemented");
    }
    delete s;
  }

 private:
  std::vector<Bstatement *> statements_;
  Backend *be_;
};


#endif // !defined(#define DRAGONGO_UNITTESTS_BACKENDCORE_TESTUTILS_H)
