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
// be found in the corresponding code in Clang. in addition
//
//===----------------------------------------------------------------------===//

#ifndef LLVMGOFRONTEND_GO_LLVM_CABI_ORACLE_H
#define LLVMGOFRONTEND_GO_LLVM_CABI_ORACLE_H

#include "go-llvm-btype.h"

#include "llvm/IR/CallingConv.h"

class TypeManager;
class EightByteInfo;
class ABIState;

namespace llvm {
class DataLayout;
class FunctionType;
}

// Disposition of a specific function argument or function return value.

enum ABIParamDisp : uint8_t  {

  // Pass argument directly (not in memory).
  ParmDirect,

  // Ignore (typically for zero-sized structs)
  ParmIgnore,

  // Pass argument in memory
  ParmIndirect,

};

// Attributes on parameters. These correspond directly to the LLVM attrs
// of the same name.

enum ABIParamAttr : uint8_t  {
  AttrNone=0,
  AttrStructReturn,
  AttrByVal,
  AttrNest,
  AttrZext,
  AttrSext
};

// Container class for storing info on how a specific parameter is
// passed to a function. A given parameter may wind up occupying
// multiple parameter slots in the cooked (ABI-specific) signature of
// the LLVM function. For example:
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

class ABIParamInfo {
 public:
  ABIParamInfo(const std::vector<llvm::Type *> &abiTypes,
               ABIParamDisp disp,
               int sigOffset)
      : abiTypes_(abiTypes), disp_(disp),
        attr_(AttrNone), sigOffset_(sigOffset) {
    assert(disp == ParmDirect);
    assert(sigOffset >= 0);
  }
  ABIParamInfo(llvm::Type *abiType,
               ABIParamDisp disp,
               ABIParamAttr attr,
               int sigOffset)
      : disp_(disp), attr_(attr), sigOffset_(sigOffset) {
    abiTypes_.push_back(abiType);
    assert(sigOffset >= -1);
  }

  unsigned numArgSlots() const { return abiTypes_.size(); }
  llvm::Type *abiType() const {
    assert(numArgSlots() == 1);
    return abiTypes_[0];
  }
  const std::vector<llvm::Type *> &abiTypes() const { return abiTypes_; }
  ABIParamDisp disp() const { return disp_; }
  ABIParamAttr attr() const { return attr_; }
  int sigOffset() const { return sigOffset_; }

 private:
  std::vector<llvm::Type *> abiTypes_;
  ABIParamDisp disp_;
  ABIParamAttr attr_;
  unsigned sigOffset_;
};

// This class helps with determining the correct ABI-adjusted function
// signature given the high level signature of a function (argument
// types and return types), along with the disposition function args
// and returns (whether they are in memory or passed directly (and/or
// whether coercion or sext/zext is required).

class CABIOracle {
 public:
  CABIOracle(BFunctionType *ft,
             TypeManager *typeManager,
             llvm::CallingConv::ID cc);

  // Returns TRUE if this cc supported, FALSE otherwise.
  bool supported() const;

  // Return the appropriate "cooked" LLVM function type for this
  // abstract function type.
  llvm::FunctionType *getFunctionTypeForABI();

  // Given the index of a parameter in the abstract function type,
  // return info of the param w.r.t. the ABI (how it is passed).
  const ABIParamInfo &paramInfo(unsigned idx);

  // Return info on transmission of return value.
  const ABIParamInfo &returnInfo();

 private:
  BFunctionType *bFcnType_;
  llvm::FunctionType *fcnTypeForABI_;
  TypeManager *typeManager_;
  llvm::CallingConv::ID conv_;
  std::vector<ABIParamInfo> infov_;

  struct ABIState {
    ABIState() : availIntRegs(6), availSSERegs(8), argCount(0) { }
    void addDirectIntArg() {
      if (availIntRegs)
        availIntRegs -= 1;
      argCount += 1;
    }
    void addDirectSSEArg() {
      if (availSSERegs)
        availSSERegs -= 1;
      argCount += 1;
    }
    void addIndirectArg() {
      argCount += 1;
    }
    unsigned availIntRegs;
    unsigned availSSERegs;
    unsigned argCount;
  };

  void compute();
  ABIParamInfo computeABIReturn(Btype *resultType, ABIState &state);
  ABIParamInfo computeABIParam(Btype *pType, ABIState &state);
  bool canPassDirectly(unsigned regsInt, unsigned regsSSE, ABIState &state);
  const llvm::DataLayout *datalayout() const;
  TypeManager *tm() const { return typeManager_; }
  ABIParamDisp classifyArgType(llvm::Type *type);
};

#endif // LLVMGOFRONTEND_GO_LLVM_CABI_ORACLE_H
