//===- llvm/tools/dragongo/unittests/BackendCore/BackendCoreTests.cpp -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "gtest/gtest.h"

#include "go-llvm-backend.h"

// Currently these need to be included before backend.h
#include "go-location.h"
#include "go-linemap.h"

#include "backend.h"
#include "go-llvm.h"

#include <stdarg.h>

using namespace llvm;

namespace {

std::string repr(Type *t) {
  std::string res;
  llvm::raw_string_ostream OS(res);
  if (t == NULL) {
    OS << "<null>";
  } else {
    t->print(OS);
  }
  return OS.str();
}

TEST(BackendCoreTests, MakeBackend) {
  LLVMContext C;

  std::unique_ptr<Backend> makeit(go_get_backend(C));

}

TEST(BackendCoreTests, ScalarTypes) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  Btype *et = be->error_type();
  ASSERT_TRUE(et != NULL);
  Btype *vt = be->void_type();
  ASSERT_TRUE(vt != NULL);
  ASSERT_TRUE(vt != et);
  Btype *bt = be->bool_type();
  ASSERT_TRUE(bt != NULL);
  Btype *pbt = be->pointer_type(bt);
  ASSERT_TRUE(pbt != NULL);
  ASSERT_TRUE(pbt->type()->isPointerTy());

  std::vector<bool> isuns = {false, true};
  std::vector<int> ibits = {8, 16, 32, 64, 128};
  for (auto uns : isuns) {
    for (auto nbits : ibits) {
      Btype *it = be->integer_type(uns, nbits);
      ASSERT_TRUE(it != NULL);
      ASSERT_TRUE(it->type()->isIntegerTy());
    }
  }

  std::vector<int> fbits = {32, 64, 128};
  for (auto nbits : fbits) {
    Btype *ft = be->float_type(nbits);
    ASSERT_TRUE(ft != NULL);
    ASSERT_TRUE(ft->type()->isFloatingPointTy());
  }
}

//
// Create this struct using backend interfaces:
//
//    struct {
//       bool f1;
//       float *f2;
//       uint64_t f3;
//    }

static Btype *mkBackendThreeFieldStruct(Backend *be)
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

static StructType *mkLlvmThreeFieldStruct(LLVMContext &context)
{
  SmallVector<Type *, 3> smv(3);
  smv[0] = Type::getInt1Ty(context);
  smv[1] = PointerType::get(Type::getFloatTy(context), 0);
  smv[2] = IntegerType::get(context, 64);
  return StructType::get(context, smv);
}

TEST(BackendCoreTests, StructTypes) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  // Empty struct
  std::vector<Backend::Btyped_identifier> nofields;
  Btype *emptyst = be->struct_type(nofields);
  SmallVector<Type *, 3> smv_empty(0);
  Type *llvm_emptyst = StructType::get(C, smv_empty);
  ASSERT_TRUE(llvm_emptyst != NULL);
  ASSERT_TRUE(emptyst != NULL);
  ASSERT_EQ(llvm_emptyst, emptyst->type());

  // Three-field struct
  Btype *best = mkBackendThreeFieldStruct(be.get());
  Type *llst = mkLlvmThreeFieldStruct(C);
  ASSERT_TRUE(best != NULL);
  ASSERT_TRUE(llst != NULL);
  ASSERT_EQ(llst, best->type());
  ASSERT_EQ(repr(best->type()), "{ i1, float*, i64 }");

  // If a field has error type, entire struct has error type
  std::vector<Backend::Btyped_identifier> fields = {
    Backend::Btyped_identifier("f1", be->bool_type(), Location()),
    Backend::Btyped_identifier("fe", be->error_type(), Location())
  };
  Btype *badst = be->struct_type(fields);
  ASSERT_TRUE(badst != NULL);
  ASSERT_EQ(badst, be->error_type());

  // Llvm_backend should be caching and reusing anonymous types
  ASSERT_EQ(mkBackendThreeFieldStruct(be.get()),
            mkBackendThreeFieldStruct(be.get()));
}

static Btype *mkTwoFieldStruct(Backend *be, Btype *t1, Btype *t2)
{
  std::vector<Backend::Btyped_identifier> fields = {
    Backend::Btyped_identifier("f1", t1, Location()),
    Backend::Btyped_identifier("f2", t2, Location())
  };
  return be->struct_type(fields);
}

static Type *mkTwoFieldLLvmStruct(LLVMContext &context, Type *t1, Type *t2) {
  SmallVector<Type *, 2> smv(2);
  smv[0] = t1;
  smv[1] = t2;
  return StructType::get(context, smv);
}

TEST(BackendCoreTests, ComplexTypes) {
  LLVMContext C;

  Type *ft = Type::getFloatTy(C);
  Type *dt = Type::getDoubleTy(C);

  std::unique_ptr<Backend> be(go_get_backend(C));
  Btype *c32 = be->complex_type(64);
  ASSERT_TRUE(c32 != NULL);
  ASSERT_EQ(c32->type(), mkTwoFieldLLvmStruct(C, ft, ft));
  Btype *c64 = be->complex_type(128);
  ASSERT_TRUE(c64 != NULL);
  ASSERT_EQ(c64->type(), mkTwoFieldLLvmStruct(C, dt, dt));
}

Backend::Btyped_identifier mkid(Btype *t)
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

static Btype *mkFuncTyp(Backend *be, ...)
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

static Type *mkLLFuncTyp(LLVMContext *context, ...)
{
  va_list ap;

  SmallVector<Type *, 4> params(0);
  SmallVector<Type *, 4> results(0);
  Type *recv_typ = NULL;
  Type *res_styp = NULL;

  va_start(ap, context);
  unsigned tok = va_arg(ap, unsigned);
  while (tok != L_END) {
    switch(tok) {
      case L_RCV:
        recv_typ = va_arg(ap, Type *);
        break;
      case L_PARM:
        params.push_back(va_arg(ap, Type *));
        break;
      case L_RES:
        results.push_back(va_arg(ap, Type *));
        break;
      case L_RES_ST:
        res_styp = va_arg(ap, Type *);
        break;
      default: {
        assert(false && "internal error");
        return NULL;
      }
    }
    tok = va_arg(ap, unsigned);
  }

  SmallVector<Type *, 4> elems(0);
  if (recv_typ)
    elems.push_back(recv_typ);
  for (auto pt : params)
    elems.push_back(pt);

  Type *rtyp = NULL;
  if (results.empty())
    rtyp = Type::getVoidTy(*context);
  else if (results.size() == 1)
    rtyp = results[0];
  else {
    rtyp = res_styp;
  }
  return FunctionType::get(rtyp, elems, false);
}

TEST(BackendCoreTests, FunctionTypes) {
  LLVMContext C;

  Type *i64t = IntegerType::get(C, 64);
  Type *i32t = IntegerType::get(C, 32);

  std::unique_ptr<Backend> be(go_get_backend(C));

  // func foo() {}
  Btype *emptyf = mkFuncTyp(be.get(), L_END);
  Type *llemptyf = mkLLFuncTyp(&C, L_END);
  ASSERT_TRUE(llemptyf != NULL && emptyf != NULL);
  ASSERT_TRUE(llemptyf == emptyf->type());

  {
    // func (Blah) foo() {}
    Btype *befn = mkFuncTyp(be.get(),
                            L_RCV, mkBackendThreeFieldStruct(be.get()),
                            L_END);
    Type *llfn = mkLLFuncTyp(&C,
                             L_RCV, mkLlvmThreeFieldStruct(C),
                             L_END);
    ASSERT_TRUE(befn != NULL && llfn != NULL);
    ASSERT_TRUE(befn->type() == llfn);
  }

  {
    // func foo(x int64) {}
    Btype *befn = mkFuncTyp(be.get(),
                            L_PARM, be->integer_type(false, 64),
                            L_END);
    Type *llfn = mkLLFuncTyp(&C,
                             L_PARM, i64t,
                             L_END);
    ASSERT_TRUE(befn != NULL && llfn != NULL);
    ASSERT_TRUE(befn->type() == llfn);
  }

  {
    // func foo() int64 {}
    Btype *befn = mkFuncTyp(be.get(),
                            L_RES, be->integer_type(false, 64),
                            L_END);
    Type *llfn = mkLLFuncTyp(&C,
                             L_RES, i64t,
                             L_END);
    ASSERT_TRUE(befn != NULL && llfn != NULL);
    ASSERT_TRUE(befn->type() == llfn);
  }

  {
    // func (Blah) foo(int32, int32, int32) (int64, int64) {}
    Btype *bi64t = be->integer_type(false, 64);
    Btype *bi32t = be->integer_type(false, 32);
    Btype *befn = mkFuncTyp(be.get(),
                            L_RCV, mkBackendThreeFieldStruct(be.get()),
                            L_PARM, bi32t,
                            L_PARM, bi32t,
                            L_PARM, bi32t,
                            L_RES, bi64t, // ignored
                            L_RES, bi64t, // ignored
                            L_RES_ST, mkTwoFieldStruct(be.get(), bi64t, bi64t),
                            L_END);
    Type *llfn = mkLLFuncTyp(&C,
                             L_RCV, mkLlvmThreeFieldStruct(C),
                             L_PARM, i32t,
                             L_PARM, i32t,
                             L_PARM, i32t,
                             L_RES, i64t, // ignored
                             L_RES, i64t, // ignored
                             L_RES_ST, mkTwoFieldLLvmStruct(C, i64t, i64t),
                             L_END);
    ASSERT_TRUE(befn != NULL && llfn != NULL);
    ASSERT_TRUE(befn->type() == llfn);
  }
}

TEST(BackendCoreTests, PlaceholderTypes) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));

  // Create a placeholder pointer type
  Location loc;
  Btype *phpt1 = be->placeholder_pointer_type("ph1", loc, false);
  ASSERT_TRUE(phpt1 != NULL);
  ASSERT_TRUE(phpt1->type()->isPointerTy());

  // Placeholder pointer types should not be cached
  Btype *phpt2 = be->placeholder_pointer_type("ph", loc, false);
  Btype *phpt3 = be->placeholder_pointer_type("ph", loc, false);
  ASSERT_TRUE(phpt2 != phpt3);
  ASSERT_TRUE(phpt2->type() != phpt3->type());

  // Replace placeholder pointer type
  Btype *pst = be->pointer_type(mkBackendThreeFieldStruct(be.get()));
  be->set_placeholder_pointer_type(phpt1, pst);
  ASSERT_TRUE(phpt1->type()->isPointerTy());
  PointerType *llpt = cast<PointerType>(phpt1->type());
  ASSERT_TRUE(llpt->getElementType()->isStructTy());

  // Placeholder struct type
  Btype *phst1 = be->placeholder_struct_type("ph", loc);

  // Replace placeholder struct type
  std::vector<Backend::Btyped_identifier> fields = {
    Backend::Btyped_identifier("f1", be->integer_type(false, 64), Location()),
    Backend::Btyped_identifier("f2", be->integer_type(false, 64), Location())
  };
  be->set_placeholder_struct_type(phst1, fields);
  Type *i64t = IntegerType::get(C, 64);
  ASSERT_TRUE(phst1->type() == mkTwoFieldLLvmStruct(C, i64t, i64t));

  // Circular pointer support
  Btype *php4 = be->placeholder_pointer_type("ph", loc, false);
  Btype *cpt = be->circular_pointer_type(php4, false);
  be->set_placeholder_pointer_type(php4, cpt);
}

TEST(BackendCoreTests, ArrayTypes) {
  LLVMContext C;

  // array types not yet implemented, since we don't have
  // expressions yet

}

TEST(BackendCoreTests, NamedTypes) {
  LLVMContext C;

  std::unique_ptr<Backend> be(go_get_backend(C));
  Location loc;
  Btype *nt = be->named_type("named_int32", be->integer_type(false, 32), loc);
  ASSERT_TRUE(nt != NULL);
  Btype *nt2 = be->named_type("another_int32", be->integer_type(false, 32), loc);
  ASSERT_TRUE(nt2 != NULL);
  ASSERT_TRUE(nt != nt2);
}

TEST(BackendCoreTests, TypeUtils) {
  LLVMContext C;

  // Type size and alignment. Size seems to be in bits, whereas
  // alignment is in bytes.
  std::unique_ptr<Backend> be(go_get_backend(C));
  Btype *i8t = be->integer_type(false, 8);
  ASSERT_EQ(be->type_size(i8t), int64_t(8));
  ASSERT_EQ(be->type_alignment(i8t), 1);

  // Slightly more complicated example
  Btype *u64 = be->integer_type(true, 64);
  Btype *st = mkTwoFieldStruct(be.get(), u64, u64);
  ASSERT_EQ(be->type_size(st), int64_t(128));
  ASSERT_EQ(be->type_alignment(st), 8);

  // type field alignment
  Btype *u32 = be->integer_type(true, 32);
  ASSERT_EQ(be->type_field_alignment(u32), 4);
}

}
