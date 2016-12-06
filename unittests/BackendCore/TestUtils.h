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
#include "llvm/Support/raw_ostream.h"

// Currently these need to be included before backend.h
#include "go-linemap.h"
#include "go-location.h"

#include "backend.h"
#include "go-llvm.h"

#include <stdarg.h>

#define RAW_RESULT(x) #x

namespace goBackendUnitTests {

// Trim leading and trailing spaces
std::string trimsp(const std::string &s);

// Split specified string into tokens (with whitespace as delimiter)
std::vector<std::string> tokenize(const std::string &s);

// Join together vector of strings to single string, separate with spaces
std::string vectostr(const std::vector<std::string> &tv);

// Diff two token vectors, returning TRUE if they are identical
// or FALSE if different (and setting diffreason to explanation of diff)

bool difftokens(const std::vector<std::string> &tv1,
                const std::vector<std::string> &tv2,
                std::string &diffreason);

// Return string representation of LLVM value (handling null ptr)
std::string repr(llvm::Value *val);

// Return string representation of LLVM type (handling null ptr)
std::string repr(llvm::Type *t);

// Return string representation of Bstatement (handling null ptr)
// Currently only a subset of statement types are supported.
std::string repr(Bstatement *statement);

// Return string representation of Bexpression (handling null ptr)
std::string repr(Bexpression *expr);

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
Btype *mkFuncTyp(Backend *be, ...);

// Varargs helper to create llvm function type, similar to "mkFuncTyp"
// above (same token-value pair list).
llvm::Type *mkLLFuncTyp(llvm::LLVMContext *context, ...);

// Returns func:  fname(i1, i2 int32) int64 { }
Bfunction *mkFunci32o64(Backend *be, const char *fname, bool mkParams = true);

// Manufacture an unsigned 64-bit integer constant
Bexpression *mkUint64Const(Backend *be, uint64_t val);

// Manufacture a signed 64-bit integer constant
Bexpression *mkInt64Const(Backend *be, int64_t val);

// Manufacture a 64-bit float constant
Bexpression *mkFloat64Const(Backend *be, double val);

// Create a basic block from a single statement
Bblock *mkBlockFromStmt(Backend *be, Bfunction *func, Bstatement *st);

// Append stmt to block
void addStmtToBlock(Backend *be, Bblock *block, Bstatement *st);

// Adds expression to block as expr statement.
void addExprToBlock(Backend *be, Bblock *block, Bexpression *e);

// Cleanup of statements and expressions created during unit testing.

class IRCleanup {
public:
  IRCleanup(Backend *be) : be_(be) {}
  ~IRCleanup() {
    for (auto s : statements_)
      if (s != be_->error_statement())
        Bstatement::destroy(s, DelBoth);
    for (auto e : expressions_)
      if (e != be_->error_expression())
        Bexpression::destroy(e, DelBoth);
  }

  void add(Bstatement *s) { statements_.push_back(s); }
  void add(Bexpression *e) { expressions_.push_back(e); }

private:
  std::vector<Bstatement *> statements_;
  std::vector<Bexpression *> expressions_;
  Backend *be_;
};

} // end namespace goBackendUnitTests

#endif // !defined(#define DRAGONGO_UNITTESTS_BACKENDCORE_TESTUTILS_H)
