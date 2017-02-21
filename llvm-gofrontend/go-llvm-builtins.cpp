//===-- go-llvm-builtins.cpp - BuiltinTable implementation ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for BuiltinTable and related classes
//
//===----------------------------------------------------------------------===//

#include "go-llvm-builtins.h"
#include "go-llvm-bfunction.h"
#include "go-llvm-typemanager.h"

void BuiltinEntry::setBfunction(Bfunction *bfunc)
{
  assert(! bfunction_.get());
  bfunction_.reset(bfunc);
}

void BuiltinTable::registerIntrinsicBuiltin(const char *name,
                                            const char *libname,
                                            llvm::Intrinsic::ID intrinsicId,
                                            const BuiltinEntryTypeVec &overloadTypes)
{
  assert(lookup(name) == nullptr);
  assert(libname == nullptr || lookup(libname) == nullptr);
  unsigned idx = entries_.size();
  entries_.push_back(BuiltinEntry(intrinsicId, name,
                                  libname ? libname : "",
                                  overloadTypes));
  tab_[std::string(name)] = idx;
  if (libname)
    tab_[std::string(libname)] = idx;
}

void BuiltinTable::registerLibCallBuiltin(const char *name,
                                          const char *libname,
                                          llvm::LibFunc libfunc,
                                          const BuiltinEntryTypeVec &paramTypes)
{
  assert(lookup(name) == nullptr);
  assert(libname == nullptr || lookup(libname) == nullptr);
  unsigned idx = entries_.size();
  entries_.push_back(BuiltinEntry(libfunc, name,
                                  libname ? libname : "",
                                  paramTypes));
  tab_[std::string(name)] = idx;
  if (libname)
    tab_[std::string(libname)] = idx;
}

BuiltinEntry *BuiltinTable::lookup(const std::string &name)
{
  auto it = tab_.find(name);
  if (it == tab_.end())
    return nullptr;
  unsigned idx = it->second;
  assert(idx < entries_.size());
  return &entries_[idx];
}

void BuiltinTable::defineAllBuiltins() {
  defineSyncFetchAndAddBuiltins();
  defineIntrinsicBuiltins();
  defineTrigBuiltins();
}

void BuiltinTable::defineIntrinsicBuiltins() {
  llvm::Type *llvmBoolType = tman_->llvmBoolType();
  llvm::Type *llvmPtrType = tman_->llvmPtrType();
  llvm::Type *llvmInt32Type = tman_->llvmInt32Type();
  llvm::Type *llvmIntegerType = tman_->llvmIntegerType();
  llvm::Type *llvmSizeType = tman_->llvmSizeType();
  llvm::Type *llvmInt64Type = tman_->llvmInt64Type();

  defineIntrinsicBuiltin("__builtin_trap", nullptr, llvm::Intrinsic::trap,
                         nullptr);

  defineIntrinsicBuiltin("__builtin_return_address", nullptr,
                         llvm::Intrinsic::returnaddress, llvmPtrType,
                         llvmInt32Type, nullptr);
  defineIntrinsicBuiltin("__builtin_frame_address", nullptr,
                         llvm::Intrinsic::frameaddress, llvmPtrType,
                         llvmInt32Type, nullptr);

  defineIntrinsicBuiltin("__builtin_expect", nullptr, llvm::Intrinsic::expect,
                         llvmIntegerType, nullptr);

  defineLibcallBuiltin("__builtin_memcmp", "memcmp",
                       llvm::LibFunc::LibFunc_memcmp,
                       llvmInt32Type, llvmPtrType, llvmPtrType,
                       llvmSizeType, nullptr);

  defineLibcallBuiltin("__builtin_memcpy", "memcpy",
                       llvm::LibFunc::LibFunc_memcpy,
                       llvmPtrType, llvmPtrType, llvmPtrType,
                       llvmSizeType, llvmInt32Type, llvmBoolType, nullptr);

  defineLibcallBuiltin("__builtin_memmove", "memmove",
                       llvm::LibFunc::LibFunc_memmove,
                       llvmPtrType, llvmPtrType, llvmPtrType,
                       llvmSizeType, nullptr);

  // go runtime refers to this intrinsic as "ctz", however the LLVM
  // equivalent is named "cttz".
  defineIntrinsicBuiltin("__builtin_ctz", "ctz", llvm::Intrinsic::cttz,
                         llvmIntegerType, nullptr);

  // go runtime refers to this intrinsic as "ctzll", however the LLVM
  // equivalent is named "cttz".
  defineIntrinsicBuiltin("__builtin_ctzll", "ctzll", llvm::Intrinsic::cttz,
                         llvmInt64Type, nullptr);

  // go runtime refers to this intrinsic as "bswap32", however the LLVM
  // equivalent is named just "bswap"
  defineIntrinsicBuiltin("__builtin_bswap32", "bswap32", llvm::Intrinsic::bswap,
                         llvmInt32Type, nullptr);

  // go runtime refers to this intrinsic as "bswap64", however the LLVM
  // equivalent is named just "bswap"
  defineIntrinsicBuiltin("__builtin_bswap64", "bswap64", llvm::Intrinsic::bswap,
                         llvmInt64Type, nullptr);
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
  llvm::LibFunc lf;
} mathfuncdesc;
}

void BuiltinTable::defineTrigBuiltins() {
  llvm::Type *llvmDoubleType = tman_->llvmDoubleType();
  llvm::Type *llvmLongDoubleType = tman_->llvmLongDoubleType();
  llvm::Type *llvmIntegerType = tman_->llvmIntegerType();

  BuiltinEntryTypeVec onearg_double(2);
  onearg_double[0] = llvmDoubleType;
  onearg_double[1] = llvmDoubleType;

  BuiltinEntryTypeVec onearg_long_double(2);
  onearg_long_double[0] = llvmLongDoubleType;
  onearg_long_double[1] = llvmLongDoubleType;

  BuiltinEntryTypeVec twoargs_double(3);
  twoargs_double[0] = llvmDoubleType;
  twoargs_double[1] = llvmDoubleType;
  twoargs_double[2] = llvmDoubleType;

  BuiltinEntryTypeVec twoargs_long_double(3);
  twoargs_long_double[0] = llvmLongDoubleType;
  twoargs_long_double[1] = llvmLongDoubleType;
  twoargs_long_double[2] = llvmLongDoubleType;

  BuiltinEntryTypeVec mixed_double(3);
  mixed_double[0] = llvmDoubleType;
  mixed_double[1] = llvmDoubleType;
  mixed_double[2] = llvmIntegerType;

  BuiltinEntryTypeVec mixed_long_double(3);
  mixed_long_double[0] = llvmLongDoubleType;
  mixed_long_double[1] = llvmLongDoubleType;
  mixed_long_double[2] = llvmIntegerType;

  std::vector<BuiltinEntryTypeVec *> signatures = {
      &onearg_double, &twoargs_double, &mixed_double};
  std::vector<BuiltinEntryTypeVec *> lsignatures = {
      &onearg_long_double, &twoargs_long_double, &mixed_long_double};

  static const mathfuncdesc funcs[] = {
      {"acos", OneArg, llvm::LibFunc::LibFunc_acos},
      {"asin", OneArg, llvm::LibFunc::LibFunc_asin},
      {"atan", OneArg, llvm::LibFunc::LibFunc_atan},
      {"atan2", TwoArgs, llvm::LibFunc::LibFunc_atan2},
      {"ceil", OneArg, llvm::LibFunc::LibFunc_ceil},
      {"cos", OneArg, llvm::LibFunc::LibFunc_cos},
      {"exp", OneArg, llvm::LibFunc::LibFunc_exp},
      {"expm1", OneArg, llvm::LibFunc::LibFunc_expm1},
      {"fabs", OneArg, llvm::LibFunc::LibFunc_fabs},
      {"floor", OneArg, llvm::LibFunc::LibFunc_floor},
      {"fmod", TwoArgs, llvm::LibFunc::LibFunc_fmod},
      {"log", OneArg, llvm::LibFunc::LibFunc_log},
      {"log1p", OneArg, llvm::LibFunc::LibFunc_log1p},
      {"log10", OneArg, llvm::LibFunc::LibFunc_log10},
      {"log2", OneArg, llvm::LibFunc::LibFunc_log2},
      {"sin", OneArg, llvm::LibFunc::LibFunc_sin},
      {"sqrt", OneArg, llvm::LibFunc::LibFunc_sqrt},
      {"tan", OneArg, llvm::LibFunc::LibFunc_tan},
      {"trunc", OneArg, llvm::LibFunc::LibFunc_trunc},
      {"ldexp", TwoMixed, llvm::LibFunc::LibFunc_trunc},
  };

  const unsigned nfuncs = sizeof(funcs) / sizeof(mathfuncdesc);
  for (unsigned idx = 0; idx < nfuncs; ++idx) {
    const mathfuncdesc &d = funcs[idx];
    char bbuf[128];
    char lbuf[128];

    sprintf(bbuf, "__builtin_%s", d.name);
    BuiltinEntryTypeVec *sig = signatures[d.nargs];
    defineLibcallBuiltin(bbuf, d.name, *sig, d.lf);
    sprintf(lbuf, "%sl", d.name);
    sprintf(bbuf, "__builtin_%s", lbuf);
    BuiltinEntryTypeVec *lsig = lsignatures[d.nargs];
    defineLibcallBuiltin(bbuf, lbuf, *lsig, d.lf);
  }
}

void BuiltinTable::defineSyncFetchAndAddBuiltins() {
  std::vector<unsigned> sizes = {1, 2, 4, 8};
  for (auto sz : sizes) {
    char nbuf[64];
    sprintf(nbuf, "__sync_fetch_and_add_%u", sz);
    llvm::Type *it = llvm::IntegerType::get(tman_->context(), sz << 3);
    llvm::PointerType *pit = llvm::PointerType::get(it, tman_->addressSpace());
    defineLibcallBuiltin(nbuf, nullptr,  // name, libname
                         BuiltinEntry::NotInTargetLib, // Libfunc ID
                         tman_->llvmVoidType(),  // result type
                         pit, it,        // param types
                         nullptr);
  }
}

void BuiltinTable::defineLibcallBuiltin(const char *name,
                                        const char *libname,
                                        unsigned libfunc, ...)
{
  va_list ap;
  BuiltinEntryTypeVec types(0);
  va_start(ap, libfunc);
  llvm::Type *resultType = va_arg(ap, llvm::Type *);
  types.push_back(resultType);
  llvm::Type *parmType = va_arg(ap, llvm::Type *);
  while (parmType) {
    types.push_back(parmType);
    parmType = va_arg(ap, llvm::Type *);
  }
  llvm::LibFunc lf = static_cast<llvm::LibFunc>(libfunc);
  registerLibCallBuiltin(name, libname, lf, types);
}

void BuiltinTable::defineLibcallBuiltin(const char *name,
                                        const char *libname,
                                        BuiltinEntryTypeVec &types,
                                        unsigned libfunc)
{
  llvm::LibFunc lf = static_cast<llvm::LibFunc>(libfunc);
  registerLibCallBuiltin(name, libname, lf, types);
}

void BuiltinTable::defineIntrinsicBuiltin(const char *name, const char *libname,
                                          unsigned intrinsicID, ...) {
  va_list ap;
  BuiltinEntryTypeVec overloadTypes;
  va_start(ap, intrinsicID);
  llvm::Type *oType = va_arg(ap, llvm::Type *);
  while (oType) {
    overloadTypes.push_back(oType);
    oType = va_arg(ap, llvm::Type *);
  }
  llvm::Intrinsic::ID iid = static_cast<llvm::Intrinsic::ID>(intrinsicID);
  registerIntrinsicBuiltin(name, libname, iid, overloadTypes);
}
