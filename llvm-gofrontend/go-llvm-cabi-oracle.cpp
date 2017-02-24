//===-- go-llvm-cabi-oracle.cpp - implementation of CABIOracle ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for CABIOracle class.
//
//===----------------------------------------------------------------------===//

#if 0

// Pseudocode:
// - classify types according to mem/nomem based on size
// - write an convertToABIType method that converts a given arg
//   to an ABI compatible type (vec, struct, etc)
// - decide what to do about whether try to optimize indirect
//   arg passing as is currently handled in X86_64ABIInfo::getIndirectResult
//   where it relies on back end to "do the right thing"

#endif

#include "go-llvm-cabi-oracle.h"

CABIOracle::CABIOracle(BFunctionType *ft, llvm::CallingConv::ID cc)
    : abstractFcnType_(ft)
    , fcnTypeForABI_(nullptr)
    , conv_(cc)
{
}

bool CABIOracle::supported()
{
  return conv_ == llvm::CallingConv::X86_64_SysV;
}

llvm::FunctionType *CABIOracle::getFunctionTypeForABI()
{
  assert(supported());
  compute();
  return fcnTypeForABI_;
}

ABIParamDisp CABIOracle::paramDisposition(unsigned idx)
{
  assert(supported());
  compute();
  unsigned pidx = idx + 1;
  assert(pidx < dispv_.size());
  return dispv_[pidx];
}

ABIParamDisp CABIOracle::returnDisposition()
{
  assert(supported());
  compute();
  unsigned ridx = 0;
  assert(ridx < dispv_.size());
  return dispv_[ridx];
}

void CABIOracle::compute()
{
  assert(0);
}
