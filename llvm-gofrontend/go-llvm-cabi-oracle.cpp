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

#include "go-llvm-cabi-oracle.h"
#include "go-llvm-typemanager.h"

#include "llvm/IR/DataLayout.h"
#include "llvm/Support/raw_ostream.h"

//......................................................................

// Returns number of bytes needed to hold the data in an object of
// this type. For example, typeSize() of "struct { float x; char c; }"
// will be 5 bytes.

static uint64_t typeSize(llvm::Type *t, TypeManager *tm)
{
  unsigned bits = tm->datalayout()->getTypeSizeInBits(t);
  assert((bits & 7) == 0);
  return bits / 8;
}

// Returns the offset in bytes between successive objects of a
// given type as stored in memory (for example, in an array). This
// includes alignment padding. For example, allocSize() of
// "struct { float x; char c; }" will be 8 bytes.
static uint64_t allocSize(llvm::Type *t, TypeManager *tm)
{
  return tm->datalayout()->getTypeAllocSize(t);
}

// Given an LLVM type, classify it according to whether it would
// need to be passed in an integer or SSE register (or if it is
// some combination of entirely empty structs/arrays).

enum TypDisp { FlavSSE, FlavInt, FlavEmpty };

// Here "meet" is in the dataflow anlysis sense (meet operator on lattice
// values).

static TypDisp dispMeet(TypDisp d1, TypDisp d2) {
  if (d1 == d2)
    return d1;
  if (d1 == FlavEmpty)
    return d2;
  if (d2 == FlavEmpty)
    return d1;
  if (d1 == FlavSSE && d2 == FlavSSE)
    return FlavSSE;
  return FlavInt;
}

static TypDisp getTypDisp(llvm::Type *typ) {
  if (typ->isFloatTy() || typ->isDoubleTy())
    return FlavSSE;
  if (typ->isArrayTy()) {
    llvm::ArrayType *at = llvm::cast<llvm::ArrayType>(typ);
    return getTypDisp(at->getTypeAtIndex(0u));
  }
  if (typ->isStructTy()) {
    llvm::StructType *st = llvm::cast<llvm::StructType>(typ);
    TypDisp disp = FlavEmpty;
    for (unsigned idx = 0; idx < st->getNumElements(); idx++)
      disp = dispMeet(getTypDisp(st->getElementType(idx)), disp);
    return disp;
  }
  return FlavInt;
}

//......................................................................

// Q: split off this class into a separate file, so as to unit test it?

// The AMD64 ABI classification scheme for aggregates (section 3.2.3
// third page) talks about dividing up the contents of an array or
// struct info 8-byte chunks or regions; this container class holds
// info on a region.  A given 8-byte region may contain something
// simple like a field of type "double", a pair of floats, or there
// may be a mix of floating point and integer fields within a struct.
// The "types" and "offsets" hold the LLVM types and offsets of
// elements relative to the start of the object; "abiDirectType" holds
// the type used to pass the elements if we're passing the entire
// object directly (in registers) as opposed in on the stack.

struct EightByteRegion {
  EightByteRegion() : abiDirectType(nullptr), attr(AttrNone) { }
  TypDisp getRegionTypDisp() const;

  std::vector<llvm::Type*> types;
  std::vector<uint64_t> offsets;
  llvm::Type *abiDirectType;
  ABIParamAttr attr;
};

TypDisp EightByteRegion::getRegionTypDisp() const {
  TypDisp disp = FlavEmpty;
  for (auto &t : types)
    disp = dispMeet(getTypDisp(t), disp);
  return disp;
}

// This class is a container for zero or more EightByteRegion objects
// that correspond to the contents of an object of some type that
// we're going to be passing to or returning from a function.

class EightByteInfo {
 public:
  EightByteInfo(Btype *bt, TypeManager *tm);

  std::vector<EightByteRegion> &regions() { return ebrs_; }
  void getRegisterRequirements(unsigned *numInt, unsigned *numSSE);

 private:
  std::vector<EightByteRegion> ebrs_;
  TypeManager *typeManager_;

  void explodeStruct(BStructType *bst);
  void explodeArray(BArrayType *bat);
  void incorporateScalar(Btype *bt);
  void computeABITypes();

  TypeManager *tm() const { return typeManager_; }
};

EightByteInfo::EightByteInfo(Btype *bt, TypeManager *tmgr)
    : typeManager_(tmgr)
{
  BStructType *bst = bt->castToBStructType();
  BArrayType *bat = bt->castToBArrayType();
  if (bst) {
    explodeStruct(bst);
  } else if (bat) {
    explodeArray(bat);
  } else {
    incorporateScalar(bt);
  }
  assert(ebrs_.size() <= 2);
  computeABITypes();
}

// Given a struct type, explode it into 0, 1, or two EightByteInfo
// descriptors. Examples of the contents of EightByteInfo structs
// for various Go types follow. The first type (empty struct) results
// in a single EightByteInfo struct with empty vectors. The second
// type results in a single EightByteInfo, and the third yields two
// EightByteInfos.
//
//    Go struct type:              Computed EightByteInfo:
//                                 C types:           offsets:
//
//    type empty struct { }  [0]   <no types>         <no offsets>
//
//    type foo struct {
//      f1 uint8;            [0]   unsigned char      0
//      f2 uint16;                 unsigned short     16
//      f4 float32;                float              32
//    }
//
//    type bar struct {
//      f1 double;            [0]  double             0
//      f2 uint8;             [1]  unsigned char      64
//      f3 int16;                  short              70
//    }
//

void EightByteInfo::explodeStruct(BStructType *bst)
{
  assert(allocSize(bst->type(), tm()) <= 16);
  unsigned numFields = bst->fields().size();

  // collect offsets and field types
  unsigned curOffset = 0;
  EightByteRegion *cur8 = nullptr;
  for (unsigned fidx = 0; fidx < numFields; ++fidx) {
    unsigned offset = typeManager_->typeFieldOffset(bst, fidx);
    if (cur8 == nullptr || (offset >= 8 && curOffset < 8)) {
      ebrs_.push_back(EightByteRegion());
      cur8 = &ebrs_.back();
    }
    // note that field type here may be a struct or array
    cur8->types.push_back(bst->fieldType(fidx)->type());
    cur8->offsets.push_back(offset);
    curOffset += offset;
  }
}

// Given an array type, explode it into 0, 1, or two EightByteInfo
// descriptors. Examples appear below; the first array type results
// in a single EightByteInfo with empty type/offset vectors, then the second
// array type results in a single EightByteInfo, and the thrd array
// type results in two EightByteInfo structs:
//
//    Go type:                     Computed EightByteInfo:
//                                 C types:           offsets:
//
//    [0]float32              [0]  <no types>         <no offsets>
//
//    [3]uint8                [0]  unsigned char      0
//                                 unsigned char      8
//                                 unsigned char      16
//
//    [6]uint16               [0]  unsigned short     0
//                                 unsigned short     16
//                                 unsigned short     32
//                                 unsigned short     48
//                            [1]  unsigned short     64
//                                 unsigned short     70

void EightByteInfo::explodeArray(BArrayType *bat)
{
  assert(allocSize(bat->type(), tm()) <= 16);
  EightByteRegion *cur8 = nullptr;
  unsigned curOffset = 0;
  unsigned elSize = typeSize(bat->elemType()->type(), tm());
  for (unsigned elidx = 0; elidx < bat->nelSize(); ++elidx) {
    unsigned offset = elidx * elSize;
    if (cur8 == nullptr || (offset >= 8 && curOffset < 8)) {
      ebrs_.push_back(EightByteRegion());
      cur8 = &ebrs_.back();
    }
    // note that elem type here may be composite
    cur8->types.push_back(bat->elemType()->type());
    cur8->offsets.push_back(offset);
    curOffset += offset;
  }
}

void EightByteInfo::incorporateScalar(Btype *bt)
{
  assert(allocSize(bt->type(), tm()) <= 8);
  EightByteRegion ebr;
  ebr.types.push_back(bt->type());
  ebr.offsets.push_back(0u);
  ebr.abiDirectType = bt->type();
  BIntegerType *bit = bt->castToBIntegerType();
  if (bit && typeSize(bit->type(), tm()) < 64)
    ebr.attr = (bit->isUnsigned() ? AttrZext : AttrSext);
  ebrs_.push_back(ebr);
}

void EightByteInfo::getRegisterRequirements(unsigned *numInt, unsigned *numSSE)
{
  *numInt = 0;
  *numSSE = 0;
  for (auto &ebr : ebrs_)
    if (ebr.getRegionTypDisp() == FlavSSE)
      *numSSE += 1;
    else
      *numInt += 1;
}

// Compute abi types for each eight-byte region

void EightByteInfo::computeABITypes()
{
  for (auto &ebr : ebrs_) {
    if (ebr.abiDirectType != nullptr)
      continue;
    TypDisp regionDisp = ebr.getRegionTypDisp();
    if (regionDisp == FlavSSE) {
      // Case 1: two floats -> vector
      if (ebr.types.size() == 2)
        ebr.abiDirectType = tm()->llvmTwoFloatVecType();
      else if (ebr.types.size() == 1)
        ebr.abiDirectType = tm()->llvmDoubleType();
      else {
        assert(false && "this should never happen");
      }
    } else {
      unsigned nel = ebr.offsets.size();
      unsigned bytes = ebr.offsets[nel-1] - ebr.offsets[0] +
          typeSize(ebr.types[nel-1], tm());
      assert(bytes && bytes <= 8);
      ebr.abiDirectType = tm()->llvmArbitraryIntegerType(bytes);
    }
  }
}

//......................................................................

void ABIParamInfo::dump()
{
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os);
  std::cerr << os.str();
}

void ABIParamInfo::osdump(llvm::raw_ostream &os)
{
  os << (disp() == ParmDirect ? "Direct" :
         (disp() == ParmIgnore ? "Ignore" :
          (disp() == ParmIndirect ? "Indirect" : "<unknown>")));
  if (attr() != AttrNone)
    os << (attr() == AttrStructReturn ? " AttrStructReturn" :
           (attr() == AttrByVal ? " AttrByVal" :
            (attr() == AttrNest ? " AttrNest" :
             (attr() == AttrZext ? " AttrZext" :
              (attr() == AttrSext ? " AttrSext" : "<unknown>")))));
  os << " { ";
  unsigned idx = 0;
  for (auto &abit : abiTypes_) {
    os << (idx++ != 0 ? ", " : "");
    abit->print(os);
  }
  os << " }";
  os << " sigOffset: " << sigOffset() << "\n";
}

//......................................................................

// Helper struct to track state information during ABI param
// classification. Keeps track of arg count (args in final ABI-cooked
// signature) along with available int/sse regs.

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

//......................................................................

CABIOracle::CABIOracle(BFunctionType *ft,
                       TypeManager *typeManager,
                       llvm::CallingConv::ID cc)
    : bFcnType_(ft)
    , fcnTypeForABI_(nullptr)
    , typeManager_(typeManager)
    , conv_(cc)
{
}

bool CABIOracle::supported() const
{
  return conv_ == llvm::CallingConv::X86_64_SysV;
}

const llvm::DataLayout *CABIOracle::datalayout() const
{
  return typeManager_->datalayout();
}

llvm::FunctionType *CABIOracle::getFunctionTypeForABI()
{
  assert(supported());
  compute();
  return fcnTypeForABI_;
}

const ABIParamInfo &CABIOracle::paramInfo(unsigned idx)
{
  assert(supported());
  compute();
  unsigned pidx = idx + 1;
  assert(pidx < infov_.size());
  return infov_[pidx];
}

const ABIParamInfo &CABIOracle::returnInfo()
{
  assert(supported());
  compute();
  unsigned ridx = 0;
  assert(ridx < infov_.size());
  return infov_[ridx];
}

void CABIOracle::dump()
{
  std::cerr << toString();
}

std::string CABIOracle::toString()
{
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os);
  return os.str();
}

void CABIOracle::osdump(llvm::raw_ostream &os)
{
  compute();
  os << "Return: ";
  infov_[0].osdump(os);
  for (unsigned pidx = 1; pidx < infov_.size(); pidx++) {
    os << "Param " << pidx << ": ";
    infov_[pidx].osdump(os);
  }
}

// For full C++ (with long double, unions, vector types) the
// rules here are a good deal more complicated, but for Go
// it all boils down to the size of the type.

ABIParamDisp CABIOracle::classifyArgType(llvm::Type *type)
{
  uint64_t sz = allocSize(type, tm());
  return (sz == 0 ? ParmIgnore : ((sz <= 16) ? ParmDirect : ParmIndirect));
}

ABIParamInfo CABIOracle::computeABIReturn(Btype *resultType, ABIState &state)
{
  llvm::Type *rtyp = resultType->type();
  ABIParamDisp rdisp = (rtyp == tm()->llvmVoidType() ?
                        ParmIgnore : classifyArgType(rtyp));

  if (rdisp == ParmIgnore) {
    // This corresponds to a function with no returns or
    // returning an empty composite.
    llvm::Type *voidType = tm()->llvmVoidType();
    return ABIParamInfo(voidType, ParmIgnore, AttrNone, -1);
  }

  if (rdisp == ParmIndirect) {
    // Return value will be passed in memory, via a hidden
    // struct return param.
    llvm::Type *ptrTyp = tm()->makeLLVMPointerType(rtyp);
    return ABIParamInfo(ptrTyp, ParmIndirect, AttrStructReturn, 0);
  }

  // Figure out what to do in the direct case
  assert(rdisp == ParmDirect);
  EightByteInfo ebi(resultType, tm());
  auto &regions = ebi.regions();
  if (regions.size() == 1) {
    // Single value
    return ABIParamInfo(regions[0].abiDirectType,
                        ParmDirect, regions[0].attr, -1);
  }

  // Two-element struct
  assert(regions.size() == 2);
  llvm::Type *abiTyp =
      tm()->makeLLVMTwoElementStructType(regions[0].abiDirectType,
                                         regions[1].abiDirectType);
  return ABIParamInfo(abiTyp, ParmDirect, AttrNone, -1);
}

bool CABIOracle::canPassDirectly(unsigned regsInt,
                                 unsigned regsSSE,
                                 ABIState &state)
{
  if (regsInt + regsSSE == 1)
    return true;
  if (regsInt <= state.availIntRegs && regsSSE <= state.availSSERegs)
    return true;
  return false;
}

ABIParamInfo CABIOracle::computeABIParam(Btype *paramType, ABIState &state)
{
  llvm::Type *ptyp = paramType->type();
  assert(paramType->flavor() != Btype::AuxT);

  ABIParamDisp pdisp = classifyArgType(ptyp);

  if (pdisp == ParmIgnore) {
    // Empty struct or array
    llvm::Type *voidType = tm()->llvmVoidType();
    return ABIParamInfo(voidType, ParmIgnore, AttrNone, -1);
  }

  int sigOff = state.argCount;

  if (pdisp == ParmIndirect) {
    // Value will be passed in memory
    llvm::Type *ptrTyp = tm()->makeLLVMPointerType(ptyp);
    state.addIndirectArg();
    return ABIParamInfo(ptrTyp, ParmIndirect, AttrByVal, sigOff);
  }

  // Figure out what to do in the direct case
  assert(pdisp == ParmDirect);
  EightByteInfo ebi(paramType, tm());

  // Figure out how many registers it would take to pass this parm directly
  unsigned regsInt = 0, regsSSE = 0;
  ebi.getRegisterRequirements(&regsInt, &regsSSE);

  // Make direct/indirect decision
  ABIParamAttr attr = AttrNone;
  if (canPassDirectly(regsInt, regsSSE, state)) {
    std::vector<llvm::Type *> abiTypes;
    for (auto &ebr : ebi.regions()) {
      abiTypes.push_back(ebr.abiDirectType);
      if (ebr.attr != AttrNone) {
        assert(attr == AttrNone || attr == ebr.attr);
        attr = ebr.attr;
      }
      if (ebr.getRegionTypDisp() == FlavSSE)
        state.addDirectSSEArg();
      else
        state.addDirectIntArg();
    }
    return ABIParamInfo(abiTypes, ParmDirect, attr, sigOff);
  } else {
    state.addIndirectArg();
    llvm::Type *ptrTyp = tm()->makeLLVMPointerType(ptyp);
    return ABIParamInfo(ptrTyp, ParmDirect, AttrByVal, sigOff);
  }
}

// This driver function carries out the various classification steps
// described in the AMD64 ABI Draft 0.99.8 document, section 3.2.3,
// 4th page and thereabouts.

void CABIOracle::compute()
{
  if (fcnTypeForABI_)
    return;

  ABIState state;

  // First slot in the info vector will be for the return.
  infov_.push_back(computeABIReturn(bFcnType_->resultType(), state));

  // Now process the params.
  const std::vector<Btype *> &paramTypes = bFcnType_->paramTypes();
  for (unsigned idx = 0; idx < paramTypes.size(); ++idx) {
    Btype *pType = paramTypes[idx];
    auto d = computeABIParam(pType, state);
    infov_.push_back(d);
  }

  llvm::SmallVector<llvm::Type *, 8> elems(0);
  llvm::Type *rtyp = infov_[0].abiType();
  for (unsigned pidx = 1; pidx < infov_.size(); pidx++) {
    if (infov_[pidx].disp() == ParmIgnore)
      continue;
    for (auto &abit : infov_[pidx].abiTypes())
      elems.push_back(abit);
  }
  const bool isVarargs = false;
  fcnTypeForABI_ = llvm::FunctionType::get(rtyp, elems, isVarargs);
}
