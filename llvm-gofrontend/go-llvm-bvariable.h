//===-- go-llvm-bvariable.h - decls for gofrontend 'Bvariable' class --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines Bvariable and related classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVMGOFRONTEND_GO_LLVM_BVARIABLE_H
#define LLVMGOFRONTEND_GO_LLVM_BVARIABLE_H

// Currently these need to be included before backend.h
#include "go-linemap.h"
#include "go-location.h"

#include "go-llvm-btype.h"

//#include "backend.h"

namespace llvm {
class Value;
}

// Back end variable class

enum WhichVar { ParamVar, GlobalVar, LocalVar, ErrorVar };

class Bvariable {
public:
  explicit Bvariable(Btype *type, Location location, const std::string &name,
                     WhichVar which, bool address_taken, llvm::Value *value)
      : name_(name), location_(location), value_(value), type_(type),
        which_(which), addrtaken_(address_taken) {}

  // Common to all varieties of variables
  Location getLocation() { return location_; }
  Btype *getType() { return type_; }
  const std::string &getName() { return name_; }
  llvm::Value *value() { return value_; }
  bool addrtaken() { return addrtaken_; }
  WhichVar flavor() const { return which_; }

private:
  Bvariable() = delete;
  const std::string name_;
  Location location_;
  llvm::Value *value_;
  Btype *type_;
  WhichVar which_;
  bool addrtaken_;

  friend class Llvm_backend;
};

#endif // LLVMGOFRONTEND_GO_LLVM_BVARAIBLE_H
