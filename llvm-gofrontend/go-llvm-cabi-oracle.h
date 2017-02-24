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

namespace llvm {
class FunctionType;
}

// Disposition of a specific function argument or function return value.

enum ABIParamDisp : uint8_t  {

  // Pass argument directly (not in memory).
  Direct,

  // Ignore (typically for zero-sized structs)
  Ignore,

  // Pass argument in memory
  Indirect,

};

// This class helps with determining the correct ABI-adjusted function
// signature given the high level signature of a function (argument types
// and return types), along with the disposition function args and returns
// (whether they are in memory or passed directly (and/or whether coercion
// or sext/zext is required).

class CABIOracle {
 public:
  CABIOracle(BFunctionType *ft, llvm::CallingConv::ID cc);

  // Returns TRUE if this cc supported, FALSE otherwise.
  bool supported();

  // Return the appropriate "cooked" LLVM function type for this
  // abstract function type.
  llvm::FunctionType *getFunctionTypeForABI();

  // Given the index of a parameter in the abstract function type,
  // return disposition of the param w.r.t. the ABI (how it is passed).
  ABIParamDisp paramDisposition(unsigned idx);

  // Return proper disposition for return value.
  ABIParamDisp returnDisposition();

 private:
  BFunctionType *abstractFcnType_;
  llvm::FunctionType *fcnTypeForABI_;
  llvm::CallingConv::ID conv_;
  std::vector<ABIParamDisp> dispv_;

  void compute();
};

#endif // LLVMGOFRONTEND_GO_LLVM_CABI_ORACLE_H
