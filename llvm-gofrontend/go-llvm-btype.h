//===-- go-llvm.h - LLVM implementation of gofrontend 'Btype' class -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines Llvm_backend and related classes
//
//===----------------------------------------------------------------------===//

#ifndef LLVMGOFRONTEND_GO_LLVM_BTYPE_H
#define LLVMGOFRONTEND_GO_LLVM_BTYPE_H

// Currently these need to be included before backend.h
#include "go-linemap.h"
#include "go-location.h"
#include "go-llvm-btype.h"

#include "backend.h"

namespace llvm {
class Type;
class Value;
class raw_ostream;
class DISubroutineType;
}

class BStructType;
class BArrayType;
class BPointerType;
class BIntegerType;
class BFloatType;
class BFunctionType;

// Btype wraps llvm::Type

class Btype {
 public:
  enum TyFlavor {
    ArrayT, FloatT, FunctionT, IntegerT, PointerT, StructT, AuxT
  };
  Btype(TyFlavor flavor, llvm::Type *type, Location location)
      : type_(type), location_(location), flavor_(flavor),
        isPlaceholder_(false) { }
  virtual ~Btype() { }

  TyFlavor flavor() const { return flavor_; }
  Location location() const { return location_; }
  void setLocation(Location loc) { location_ = loc; }

  // Underlying LLVM type.
  llvm::Type *type() const { return type_; }
  void setType(llvm::Type *t) { assert(t); type_ = t; }

  // Name of type if named.
  const std::string &name() const { return name_; }
  void setName(const std::string &name) { name_ = name; }

  // Whether this type is a placeholder. This can be set for type
  // explicitly created as placeholders (for example, something
  // returned from ::placeholder_pointer_type()) or it can be set in
  // cases where a primary type creation call is made by some sub-type
  // is a placeholder (for example, invoking ::pointer_type() with an
  // element type that is still a placeholder).
  bool isPlaceholder() const { return isPlaceholder_; }
  void setPlaceholder(bool v) { isPlaceholder_ = v; }

  // Similar to the above, but returns true for placeholder
  // struct types.
  bool isUnresolvedPlaceholder() const;

  // Create a shallow copy of this type
  Btype *clone() const;

  // debugging
  void dump() const;

  // dump to raw ostream buffer
  void osdump(llvm::raw_ostream &os, unsigned ilevel) const;

  // test for structural equality
  bool equal(const Btype &other) const;

  // hash
  unsigned hash() const;

  // Cast to derived class (these return NULL if the type
  // does not have the appropriate flavor).
  inline BStructType *castToBStructType();
  inline BArrayType *castToBArrayType();
  inline BPointerType *castToBPointerType();
  inline BIntegerType *castToBIntegerType();
  inline BFloatType *castToBFloatType();
  inline BFunctionType *castToBFunctionType();
  inline const BStructType *castToBStructType() const;
  inline const BArrayType *castToBArrayType() const;
  inline const BPointerType *castToBPointerType() const;
  inline const BIntegerType *castToBIntegerType() const;
  inline const BFloatType *castToBFloatType() const;
  inline const BFunctionType *castToBFunctionType() const;

 private:
  Btype() : type_(NULL) {}
  std::string name_;
  llvm::Type *type_;
  Location location_;
  TyFlavor flavor_;
  bool isPlaceholder_;
};

class BIntegerType : public Btype {
 public:
  BIntegerType(bool isUnsigned, unsigned bits,
               llvm::Type *type, Location location)
      : Btype(IntegerT, type, location),
        bits_(bits), isUnsigned_(isUnsigned) { }

  bool isUnsigned() const { return isUnsigned_; }
  unsigned bits() const { return bits_; }

  // Create a shallow copy of this type
  Btype *clone() const {
    return new BIntegerType(isUnsigned_, bits_, type(), location());
  }

 private:
  unsigned bits_;
  bool isUnsigned_;
};

inline BIntegerType *Btype::castToBIntegerType() {
  return (flavor_ == IntegerT ? static_cast<BIntegerType *>(this)
          : nullptr);
}

inline const BIntegerType *Btype::castToBIntegerType() const {
  return (flavor_ == IntegerT ? static_cast<const BIntegerType *>(this)
          : nullptr);
}

class BFloatType : public Btype {
 public:
  BFloatType(unsigned bits, llvm::Type *type, Location location)
      : Btype(FloatT, type, location), bits_(bits) { }

  unsigned bits() const { return bits_; }

  // Create a shallow copy of this type
  Btype *clone() const {
    return new BFloatType(bits_, type(), location());
  }

 private:
  unsigned bits_;
};

inline BFloatType *Btype::castToBFloatType() {
  return (flavor_ == FloatT ? static_cast<BFloatType *>(this)
          : nullptr);
}

inline const BFloatType *Btype::castToBFloatType() const {
  return (flavor_ == FloatT ? static_cast<const BFloatType *>(this)
          : nullptr);
}

class BStructType : public Btype {
 public:

  // For concrete struct types
  BStructType(const std::vector<Backend::Btyped_identifier> &fields,
              llvm::Type *type, Location location)
      : Btype(StructT, type, location), fields_(fields) { }

  // For placeholder struct types
  BStructType(const std::string &name, Location location)
      : Btype(StructT, nullptr, location)
  {
    setPlaceholder(true);
    setName(name);
  }

  Btype *fieldType(unsigned idx) const {
    return fields_[idx].btype;
  }
  void setFieldType(unsigned idx, Btype *t) {
    assert(t);
    fields_[idx].btype = t;
  }
  const std::string &fieldName(unsigned idx) const {
    return fields_[idx].name;
  }
  const std::vector<Backend::Btyped_identifier> &fields() const {
    return fields_;
  }
  void setFields(const std::vector<Backend::Btyped_identifier> &fields) {
    fields_ = fields;
  }

  // Create a shallow copy of this type
  Btype *clone() const {
    return new BStructType(fields_, type(), location());
  }

 private:
  std::vector<Backend::Btyped_identifier> fields_;
};

inline BStructType *Btype::castToBStructType() {
  return (flavor_ == StructT ? static_cast<BStructType *>(this)
          : nullptr);
}

inline const BStructType *Btype::castToBStructType() const {
  return (flavor_ == StructT ? static_cast<const BStructType *>(this)
          : nullptr);
}

class BArrayType : public Btype {
 public:
  // For concrete array types
  BArrayType(Btype *elemType, Bexpression *nelements,
             llvm::Type *type, Location location)
      : Btype(ArrayT, type, location),
        elemType_(elemType), nelements_(nelements)
  {
    if (elemType_->isPlaceholder())
      setPlaceholder(true);
  }

  // For placeholder array types
  BArrayType(const std::string &name, Location location)
      : Btype(ArrayT, nullptr, location),
        elemType_(nullptr), nelements_(nullptr)
  {
    setPlaceholder(true);
    setName(name);
  }

  Btype *elemType() const {
    return elemType_;
  }
  void setElemType(Btype *t) {
    assert(t);
    elemType_ = t;
    if (elemType_->isPlaceholder())
      setPlaceholder(true);
  }
  Bexpression *nelements() const {
    return nelements_;
  }
  void setNelements(Bexpression *nel) { nelements_ = nel; }
  uint64_t nelSize() const;

  // Create a shallow copy of this type
  Btype *clone() const {
    return new BArrayType(elemType_, nelements_, type(), location());
  }

 private:
  Btype *elemType_;
  Bexpression *nelements_;
};

inline BArrayType *Btype::castToBArrayType() {
  return (flavor_ == ArrayT ? static_cast<BArrayType *>(this)
          : nullptr);
}

inline const BArrayType *Btype::castToBArrayType() const {
  return (flavor_ == ArrayT ? static_cast<const BArrayType *>(this)
          : nullptr);
}

class BPointerType : public Btype {
 public:
  // For concrete pointer types
  BPointerType(Btype *toType, llvm::Type *type, Location location)
      : Btype(PointerT, type, location), toType_(toType) {
    if (toType_->isPlaceholder())
      setPlaceholder(true);
  }

  // For placeholder pointers
  BPointerType(const std::string &name, Location location)
      : Btype(PointerT, nullptr, location), toType_(nullptr) {
    setPlaceholder(true);
    setName(name);
  }

  Btype *toType() const {
    return toType_;
  }
  void setToType(Btype *to) {
    toType_ = to;
  }

  // Create a shallow copy of this type
  Btype *clone() const {
    return new BPointerType(toType_, type(), location());
  }

 private:
  Btype *toType_;
};

inline BPointerType *Btype::castToBPointerType() {
  return (flavor_ == PointerT ? static_cast<BPointerType *>(this)
          : nullptr);
}

inline const BPointerType *Btype::castToBPointerType() const {
  return (flavor_ == PointerT ? static_cast<const BPointerType *>(this)
          : nullptr);
}

class BFunctionType : public Btype {
 public:
  BFunctionType(Btype *receiverType,
                const std::vector<Btype *> &paramTypes,
                const std::vector<Btype *> &resultTypes,
                Btype *rtype,
                llvm::Type *type,
                bool followsCabi,
                Location location)
      : Btype(FunctionT, type, location), receiverType_(receiverType),
        paramTypes_(paramTypes), resultTypes_(resultTypes),
        rtype_(rtype), followsCabi_(followsCabi) {
    assert(!receiverType || paramTypes[0] == receiverType);
  }

  Btype *resultType() const { return rtype_; }
  Btype *receiverType() const { return receiverType_; }

  // Does this function type follow ABI rules?
  bool followsCabi() const { return followsCabi_; }

  // Note that if the receiver type is non-null, it will
  // occupy the first slot in paramTypes.
  const std::vector<Btype *> &paramTypes() const { return paramTypes_; }

  // Create a shallow copy of this type
  Btype *clone() const {
    return new BFunctionType(receiverType_, paramTypes_, resultTypes_, rtype_,
                             type(), followsCabi(), location());
  }

 private:
  Btype *receiverType_;
  std::vector<Btype *> paramTypes_;
  std::vector<Btype *> resultTypes_;
  Btype *rtype_;
  bool followsCabi_;
};

inline BFunctionType *Btype::castToBFunctionType() {
  return (flavor_ == FunctionT ? static_cast<BFunctionType *>(this)
          : nullptr);
}
inline const BFunctionType *Btype::castToBFunctionType() const {
  return (flavor_ == FunctionT ? static_cast<const BFunctionType *>(this)
          : nullptr);
}

#endif // LLVMGOFRONTEND_GO_LLVM_BTYPE_H
