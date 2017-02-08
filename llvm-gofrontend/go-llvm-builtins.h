//===-- go-llvm-builtins.h - decls for 'BuiltinTable' class ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines BuiltinTable and related classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVMGOFRONTEND_GO_LLVM_BUILTINTABLE_H
#define LLVMGOFRONTEND_GO_LLVM_BUILTINTABLE_H

#include <unordered_map>

#include "llvm/Analysis/TargetLibraryInfo.h"

typedef llvm::SmallVector<llvm::Type *, 16> BuiltinEntryTypeVec;

class Bfunction;

class BuiltinEntry {
 public:
  enum BuiltinFlavor { IntrinsicBuiltin, LibcallBuiltin };
  static const auto NotInTargetLib = llvm::LibFunc::NumLibFuncs;

  BuiltinFlavor flavor() const { return flavor_; }
  const std::string &name() const { return name_; }
  const std::string &libname() const { return libname_; }
  llvm::Intrinsic::ID intrinsicId() const { return intrinsicId_; }
  llvm::LibFunc libfunc() const { return libfunc_; }
  const BuiltinEntryTypeVec &types() const { return types_; }
  Bfunction *bfunction() const { return bfunction_.get(); }
  void setBfunction(Bfunction *bfunc);

  BuiltinEntry(llvm::Intrinsic::ID intrinsicId,
               const std::string &name,
               const std::string &libname,
               const BuiltinEntryTypeVec &ovltypes)
      : name_(name), libname_(libname), flavor_(IntrinsicBuiltin),
        intrinsicId_(intrinsicId), libfunc_(NotInTargetLib),
        bfunction_(nullptr), types_(ovltypes) { }
  BuiltinEntry(llvm::LibFunc libfunc,
               const std::string &name,
               const std::string &libname,
               const BuiltinEntryTypeVec &paramtypes)
      : name_(name), libname_(libname), flavor_(LibcallBuiltin),
        intrinsicId_(), libfunc_(libfunc), bfunction_(nullptr),
        types_(paramtypes) { }

 private:
  std::string name_;
  std::string libname_;
  BuiltinFlavor flavor_;
  llvm::Intrinsic::ID intrinsicId_;
  llvm::LibFunc libfunc_;
  std::unique_ptr<Bfunction> bfunction_;
  BuiltinEntryTypeVec types_;
};

class BuiltinTable {
 public:
  BuiltinTable() { }
  ~BuiltinTable() { }

  void registerIntrinsicBuiltin(const char *name,
                                const char *libname,
                                llvm::Intrinsic::ID intrinsicId,
                                const BuiltinEntryTypeVec &overloadTypes);
  void registerLibCallBuiltin(const char *name,
                              const char *libname,
                              llvm::LibFunc libfunc,
                              const BuiltinEntryTypeVec &paramTypes);

  BuiltinEntry *lookup(const std::string &name);

 private:
  std::unordered_map<std::string, unsigned> tab_;
  std::vector<BuiltinEntry> entries_;
};

#endif // LLVMGOFRONTEND_GO_LLVM_BUILTINTABLE_H
