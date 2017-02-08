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
