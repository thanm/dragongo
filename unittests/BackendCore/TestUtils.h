//===- llvm/tools/dragongo/unittests/BackendCore/TestUtils.h --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef DRAGONGO_UNITTESTS_BACKENDCORE_TESTUTILS_H
#define DRAGONGO_UNITTESTS_BACKENDCORE_TESTUTILS_H

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

// Currently these need to be included before backend.h
#include "go-linemap.h"
#include "go-location.h"

#include "backend.h"
#include "go-llvm.h"

#include "DiffUtils.h"

#include <stdarg.h>

#define RAW_RESULT(x) #x

namespace goBackendUnitTests {

// Return string representation of LLVM value (handling null ptr)
std::string repr(llvm::Value *val);

// Return string representation of LLVM type (handling null ptr)
std::string repr(llvm::Type *t);

// Return string representation of Bstatement (handling null ptr)
// Currently only a subset of statement types are supported.
std::string repr(Bstatement *statement);

// Return string representation of Bexpression (handling null ptr)
std::string repr(Bexpression *expr);

// Varargs helper for struct creation. Pass in pairs of
// type/name fields, ending with null ptr.
Btype *mkBackendStruct(Backend *be, ...);

// Similar to the above but with LLVM types (no field names)
llvm::StructType *mkLlvmStruct(llvm::LLVMContext *context, ...);

// Create this struct using backend interfaces:
//
//    struct { bool f1; float *f2; uint64_t f3; }
//
Btype *mkBackendThreeFieldStruct(Backend *be);

// Create this struct using LLVM interfaces:
//
//    struct { bool f1; float *f2; uint64_t f3; }
//
llvm::StructType *mkLlvmThreeFieldStruct(llvm::LLVMContext &context);

// Create a struct Btype with two fields, specified by t1 and t2
Btype *mkTwoFieldStruct(Backend *be, Btype *t1, Btype *t2);

// Create an LLVM struct type with two fields, specified by t1 and t2
llvm::Type *mkTwoFieldLLvmStruct(llvm::LLVMContext &context,
                                 llvm::Type *t1, llvm::Type *t2);

// Check two LLVM types for structural equality. Hacky, but it helps
// to have this for unit testing of placeholder struct types. Note
// that this ignores type names and type attributes (ex: whether a
// function type is varargs).
bool llvmTypesEquiv(llvm::Type *t1, llvm::Type *t2);

// Create Btyped_identifier from type (uses static counter
// to insure unique name each time).
Backend::Btyped_identifier mkid(Btype *t);

typedef enum {
  L_END = 0, // end of list
  L_RCV,     // receiver type follows
  L_PARM,    // arg type follows
  L_RES,     // res type follows
  L_RES_ST,  // res struct type follows
} MkfToken;

// Varargs helper to create Btype function type. The variable arguments
// are token-value pairs using for form above in MkfToken, with pairlist
// terminated by L_END. Example usage:
//
//      Btype *bi32t = be->integer_type(false, 32);
//      Btype *befn = mkFuncTyp(be.get(),
//                            L_PARM, bi32t,
//                            L_PARM, bi32t,
//                            L_RES, bi32t,
//                            L_END);
//
BFunctionType *mkFuncTyp(Backend *be, ...);

// Varargs helper to create llvm function type, similar to "mkFuncTyp"
// above (same token-value pair list).
llvm::Type *mkLLFuncTyp(llvm::LLVMContext *context, ...);

// Returns func:  fname(i1, i2 int32) int64 { }
Bfunction *mkFunci32o64(Backend *be, const char *fname, bool mkParams = true);

// Produce a call expression targeting the specified function. Variable
// args are parameter values, terminated by nullptr.
Bexpression *mkCallExpr(Backend *be, Bfunction *fun, ...);

// Returns function created from type
Bfunction *mkFuncFromType(Backend *be, const char *fname, BFunctionType *befty);

// Manufacture an unsigned 64-bit integer constant
Bexpression *mkUint64Const(Backend *be, uint64_t val);

// Manufacture a signed 64-bit integer constant
Bexpression *mkInt64Const(Backend *be, int64_t val);

// Manufacture a 64-bit float constant
Bexpression *mkFloat64Const(Backend *be, double val);

// Manufacture a signed 32-bit integer constant
Bexpression *mkInt32Const(Backend *be, int32_t val);

// Return func desc type
Btype *mkFuncDescType(Backend *be);

// Create a function descriptor value for specified func
Bexpression *mkFuncDescExpr(Backend *be, Bfunction *fcn);

// Create a basic block from a single statement
Bblock *mkBlockFromStmt(Backend *be, Bfunction *func, Bstatement *st);

// Append stmt to block
Bstatement *addStmtToBlock(Backend *be, Bblock *block, Bstatement *st);

// Adds expression to block as expr statement.
Bstatement *addExprToBlock(Backend *be, Bfunction *f,
                           Bblock *bl, Bexpression *e);

class FcnTestHarness {
 public:
  // Create test harness. If name specified, then create a
  // default function "fcnName(i1, i2 int32) int64".
  FcnTestHarness(const char *fcnName = nullptr);
  ~FcnTestHarness();

  // Create function to work on
  Bfunction *mkFunction(const char *fcnName, BFunctionType *befty);

  // Return pointer to backend
  Llvm_backend *be() { return be_.get(); }

  // Return current function
  Bfunction *func() const { return func_; }

  // Return current block
  Bblock *block() const { return curBlock_; }

  // Create a local variable in the function.
  Bvariable *mkLocal(const char *name, Btype *typ, Bexpression *init = nullptr);

  // Whether to append created stmt to current block
  enum AppendDisp { YesAppend, NoAppend };

  // Create an assignment LHS = RHS and append to block
  void mkAssign(Bexpression *lhs, Bexpression *rhs, AppendDisp d = YesAppend);

  // Create and append an expression stmt
  Bstatement *mkExprStmt(Bexpression *expr, AppendDisp disp = YesAppend);

  // Append a return stmt to block
  Bstatement *mkReturn(Bexpression *expr, AppendDisp disp = YesAppend);

  // Append a multi-value return stmt to block
  Bstatement *mkReturn(const std::vector<Bexpression *> &vals,
                       AppendDisp disp = YesAppend);

  // Create and append an "if" statement.
  Bstatement *mkIf(Bexpression *cond, Bstatement *trueStmt,
                   Bstatement *falseStmt, AppendDisp disp = YesAppend);

  // Create and append a switch statement
  Bstatement *mkSwitch(Bexpression *swval,
                       const std::vector<std::vector<Bexpression*> >& cases,
                       const std::vector<Bstatement*>& statements,
                       AppendDisp disp = YesAppend);

  // Add a previously created statement to the current block
  void addStmt(Bstatement *stmt);

  // Create a new block (prev block will jump to new one)
  void newBlock();

  // Verify that block contains specified contents. Return false
  // and emit diagnostics if not.
  bool expectBlock(const std::string &expected);

  // Verify that stmt contains specified contents. Return false
  // and emit diagnostics if not.
  bool expectStmt(Bstatement *st, const std::string &expected);

  // Verify that value contains specified contents. Return false
  // and emit diagnostics if not.
  bool expectValue(llvm::Value *val, const std::string &expected);

  // Finish function:
  // - attach current block to function
  // - verify module, returning TRUE if module fails to verify
  bool finish();

 private:
  llvm::LLVMContext context_;
  std::unique_ptr<Llvm_backend> be_;
  Bfunction *func_;
  std::vector<Bvariable *> emptyVarList_;
  Location loc_;
  Bblock *entryBlock_;
  Bblock *curBlock_;
  Blabel *nextLabel_;
  bool finished_;
  bool returnAdded_;
};

} // end namespace goBackendUnitTests

#endif // !defined(#define DRAGONGO_UNITTESTS_BACKENDCORE_TESTUTILS_H)
