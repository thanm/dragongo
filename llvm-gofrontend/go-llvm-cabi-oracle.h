//===-- go-llvm-cabi-oracle.h - decls for 'CABIOracle' class -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines CABIOracle class. This class helps assist in the process of
// determining how values are passed to and returned from functions: whether
// in memory, directly, or via some sort of type coercion or sign extension.
//
// There are many possible complications, permutations, and oddities when
// it comes to runtime calling conventions; the code here currently supports
// only x86_64 SysV, which gets rid of many of the corner cases that can
// be found in the corresponding code in Clang.
//
//===----------------------------------------------------------------------===//

#ifndef LLVMGOFRONTEND_GO_LLVM_CABI_ORACLE_H
#define LLVMGOFRONTEND_GO_LLVM_CABI_ORACLE_H

#include "go-llvm-btype.h"

class TypeManager;
class EightByteInfo;
class ABIState;

namespace llvm {
class DataLayout;
class FunctionType;
class raw_ostream;
}

// Disposition of a specific function argument or function return value.

enum CABIParamDisp : uint8_t  {

  // Pass argument directly (not in memory).
  ParmDirect,

  // Ignore (typically for zero-sized structs)
  ParmIgnore,

  // Pass argument in memory
  ParmIndirect,

};

// Attributes on parameters. These correspond directly to the LLVM attrs
// of the same name.

enum CABIParamAttr : uint8_t  {
  AttrNone=0,
  AttrStructReturn,
  AttrByVal,
  AttrNest,
  AttrZext,
  AttrSext,
};

// Container class for storing info on how a specific parameter is
// passed to a function. A given parameter may wind up occupying
// multiple slots in the cooked (ABI-specific) signature of the LLVM
// function. For example:
//
//       type blah struct {
//          x float64
//          u,v,y,z uint8
//       }
//
//       func foo(p1 blah, p2 *int) int { ...
//         ...
//       }
//
// Here parameter p1 will be passed directly (by value in registers),
// however the signature of the function will have two params
// corresponding to the contents of "p1", e.g.
//
//      declare int @foo(double, int32, *int32)
//
// In the object below, sigOffset() returns of the index of the param
// (or first param) within the lowered fcn signature used to pass the param
// in question. For the function above, sigOffset() for "p1" would
// be 0 and for "p2" would be 2. A sigOffset value of -1 is present
// in the case of a fcn return value, or in an "empty" parm (ex: type
// of empty struct).

class CABIParamInfo {
 public:
  CABIParamInfo(const std::vector<llvm::Type *> &abiTypes,
               CABIParamDisp disp,
               CABIParamAttr attr,
               int sigOffset)
      : abiTypes_(abiTypes), disp_(disp),
        attr_(attr), sigOffset_(sigOffset) {
    assert(disp == ParmDirect);
    assert(sigOffset >= 0);
  }
  CABIParamInfo(llvm::Type *abiType,
               CABIParamDisp disp,
               CABIParamAttr attr,
               int sigOffset)
      : disp_(disp), attr_(attr), sigOffset_(sigOffset) {
    abiTypes_.push_back(abiType);
    assert(sigOffset >= -1);
  }
  CABIParamInfo(const CABIParamInfo &src)
      : abiTypes_(src.abiTypes_),
        disp_(src.disp_),
        attr_(src.attr_),
        sigOffset_(src.sigOffset_) { }

  unsigned numArgSlots() const { return abiTypes_.size(); }
  llvm::Type *abiType() const {
    assert(numArgSlots() == 1);
    return abiTypes_[0];
  }
  const std::vector<llvm::Type *> &abiTypes() const { return abiTypes_; }
  CABIParamDisp disp() const { return disp_; }
  CABIParamAttr attr() const { return attr_; }
  int sigOffset() const { return sigOffset_; }

  // Return a struct with fields corresponding to the ABI type(s)
  // for this param (may be a 1-element struct or a 2-element struct).
  llvm::Type *computeABIStructType(TypeManager *tm) const;

  void dump();
  void osdump(llvm::raw_ostream &os);

 private:
  std::vector<llvm::Type *> abiTypes_;
  CABIParamDisp disp_;
  CABIParamAttr attr_;
  unsigned sigOffset_;
};

// This class helps with determining the correct ABI-adjusted function
// signature given the high level signature of a function (argument
// types and return types), along with the disposition function args
// and returns (whether they are in memory or passed directly (and/or
// whether coercion or sext/zext is required).

class CABIOracle {
 public:

  // Given information on the param types and result type for a
  // function, create an oracle object that can answer C ABI
  // queries about the function.
  CABIOracle(const std::vector<Btype *> &fcnParamTypes,
             Btype *fcnResultType,
             bool followsCabi,
             TypeManager *typeManager);

  // This constructor draws param/result info from an existing BFunctionType
  CABIOracle(BFunctionType *ft,
             TypeManager *typeManager);

  // Returns TRUE if this cc supported, FALSE otherwise.
  bool supported() const;

  // Return the appropriate "cooked" LLVM function type for this
  // abstract function type.
  llvm::FunctionType *getFunctionTypeForABI();

  // Given the index of a parameter in the abstract function type,
  // return info on how the param is handled with respects to the ABI.
  const CABIParamInfo &paramInfo(unsigned idx);

  // Return info on transmission of return value.
  const CABIParamInfo &returnInfo();

  // Return info on the static chain parameter for the function.
  const CABIParamInfo &chainInfo();

  // Type manager used with this oracle.
  TypeManager *tm() const { return typeManager_; }

  // Various dump methods.
  void dump();
  std::string toString();
  void osdump(llvm::raw_ostream &os);

 private:
  std::vector<Btype *> fcnParamTypes_;
  Btype *fcnResultType_;
  llvm::FunctionType *fcnTypeForABI_;
  TypeManager *typeManager_;
  std::vector<CABIParamInfo> infov_;
  bool followsCabi_;

  void analyze();
  void analyzeRaw();
  CABIParamInfo analyzeABIReturn(Btype *resultType, ABIState &state);
  CABIParamInfo analyzeABIParam(Btype *pType, ABIState &state);
  bool canPassDirectly(unsigned regsInt, unsigned regsSSE, ABIState &state);
  const llvm::DataLayout *datalayout() const;
  CABIParamDisp classifyArgType(llvm::Type *type);
};

#endif // LLVMGOFRONTEND_GO_LLVM_CABI_ORACLE_H
