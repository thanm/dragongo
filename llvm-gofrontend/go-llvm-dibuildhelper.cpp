//===-- go-llvm-dibuildhelper.cpp - implementation of DIBuildHelper -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for DIBuildHelper class.
//
//===----------------------------------------------------------------------===//


#include "go-llvm-dibuildhelper.h"
#include "go-llvm-typemanager.h"
#include "go-llvm-bexpression.h"

#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"

DIBuildHelper::DIBuildHelper(TypeManager *typemanager,
                             Llvm_linemap *linemap,
                             llvm::DIBuilder &builder,
                             llvm::DIScope *moduleScope)
    : typemanager_(typemanager), linemap_(linemap),
      dibuilder_(builder), moduleScope_(moduleScope)
{
  pushDIScope(moduleScope);
}

llvm::DIFile *DIBuildHelper::diFileFromLocation(Location location)
{
#if 0
  llvm::StringRef parentPath = llvm::sys::path::parent_path(NewPath.str());
#endif
  std::string locfile = linemap()->location_file(location);
  return dibuilder().createFile(locfile, "/something");
}

llvm::DIScope *DIBuildHelper::currentDIScope()
{
  assert(diScopeStack_.size());
  return diScopeStack_.back();
}

llvm::DIScope *DIBuildHelper::popDIScope()
{
  assert(diScopeStack_.size());
  llvm::DIScope *popped = diScopeStack_.back();
  diScopeStack_.pop_back();
  return popped;
}

void DIBuildHelper::pushDIScope(llvm::DIScope *scope)
{
  assert(scope);
  diScopeStack_.push_back(scope);
}
