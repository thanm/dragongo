//===-- go-llvm-dibuild.h - DIBuildHelper class interfaces  ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines DIBuildHelper class.
//
//===----------------------------------------------------------------------===//

#ifndef GO_LLVM_DIBUILDHELPER_H
#define GO_LLVM_DIBUILDHELPER_H

#include <vector>
#include <unordered_map>

#include "go-location.h"

namespace llvm {
class DIBuilder;
class DIScope;
class DIFile;
class DILocation;
class DebugLoc;
class DIType;
class Type;
}

class Btype;
class Llvm_linemap;
class TypeManager;

// This class helps with managing the process of generating debug meta-data.
// It carries around pointers to objects that are needed (ex: linemap,
// typemanager, DIBuilder, etc) and keeps track of stack of DIScopes.

class DIBuildHelper {
 public:
  DIBuildHelper(TypeManager *typemanager,
                Llvm_linemap *linemap,
                llvm::DIBuilder &builder,
                llvm::DIScope *moduleScope);

  llvm::DIFile *diFileFromLocation(Location location);

  llvm::DebugLoc debugLocFromLocation(Location location);

  // Return module scope
  llvm::DIScope *moduleScope() const { return moduleScope_; }

  // Return top of DI scope stack
  llvm::DIScope *currentDIScope();

  // Push / pop scope
  llvm::DIScope *popDIScope();
  void pushDIScope(llvm::DIScope *);

  // Various getters
  llvm::DIBuilder &dibuilder() { return dibuilder_; }
  Llvm_linemap *linemap() { return linemap_; }
  TypeManager *typemanager() { return typemanager_; }

  // Type cache, to deal with cycles
  std::unordered_map<Btype *, llvm::DIType*> &typeCache() {
    return typeCache_;
  }
  std::unordered_map<llvm::DIType *, llvm::DIType*> &typeReplacements() {
    return typeReplacements_;
  }

 private:
  TypeManager *typemanager_;
  Llvm_linemap *linemap_;
  llvm::DIBuilder &dibuilder_;
  llvm::DIScope *moduleScope_;
  std::vector<llvm::DIScope*> diScopeStack_;
  std::unordered_map<Btype *, llvm::DIType*> typeCache_;
  std::unordered_map<llvm::DIType *, llvm::DIType*> typeReplacements_;
};

#endif // !defined(GO_LLVM_DIBUILDHELPER_H)
