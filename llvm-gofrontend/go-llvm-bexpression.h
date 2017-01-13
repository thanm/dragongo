//===-- go-llvm-bexpression.h - decls for gofrontend 'Bexpression' class --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines Bexpression and related classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVMGOFRONTEND_GO_LLVM_BEXPRESSION_H
#define LLVMGOFRONTEND_GO_LLVM_BEXPRESSION_H

// Currently these need to be included before backend.h
#include "go-linemap.h"
#include "go-location.h"
#include "go-llvm-btype.h"

#include "backend.h"

namespace llvm {
class Instruction;
class Value;
class raw_ostream;
}

class Bstatement;

// Mixin class for a list of instructions

class Binstructions {
public:
  Binstructions() {}
  explicit Binstructions(const std::vector<llvm::Instruction *> instructions)
      : instructions_(instructions) {}

  std::vector<llvm::Instruction *> &instructions() { return instructions_; }
  void appendInstruction(llvm::Instruction *inst) {
    instructions_.push_back(inst);
  }
  void appendInstructions(const std::vector<llvm::Instruction *> &ilist) {
    for (auto inst : ilist)
      instructions_.push_back(inst);
  }

  void clear() { instructions_.clear(); }

private:
  std::vector<llvm::Instruction *> instructions_;
};

// Use when deleting a Bstatement or Bexpression subtree. Controls
// whether to delete just the Bstatement/Bexpression objects, just
// the LLVM instructions they contain, or both.
//
enum WhichDel {
  DelInstructions, // delete only instructions
  DelWrappers,     // delete only wrappers
  DelBoth          // delete wrappers and instructions
};

// The general strategy for handling Bexpression creation is to
// generate the llvm::Instruction representation of a given
// Bexpression at the point where it is create.  For example, if
// Llvm_backend::binary_operator is invoked to create a Bexpression
// for an addition operation, it will eagerly manufacture a new
// llvm::BinaryOperator object and return a new Bexpression that
// encapsulates that object.
//
// This eager strategy works well for the most part, but has to be
// relaxed in some instances, notably variable references and
// composite initializers.  For example, consider the following Go
// code:
//
//        struct X { a, b int64 }
//        func foo(q, r int64, ip *int64, px *X) int64 {
//            r = q
//           r = **&ip
//           ip = &q
//         px.a = px.b
//
// The right hand side expression trees for these statements would look like:
//
//        stmt 1:   varexpr("q")
//        stmt 2:   deref(deref(address(varexpr("ip")))).
//        stmt 3:   address(varexpr("q"))
//        stmt 4:   field(deref(varexpr("px"),'b')
//
// At the point where Llvm_backend::var_expression is called, we don't
// know the context for the consuming instruction. For statement 1, we
// want to generate a load for the varexpr, however in statement 3 it
// would be premature to create the load (since the varexpr is feeding
// into an address operator).
//
// To deal with this, we use this mix-in class below (Bexpression
// inherits from it) to record whether the subtree in question
// contains a root variable expression, and if so, the number of
// "address" operators that have been applied, and whether the
// original varexpr appears in an assignment (lvalue) context. Once we
// reach a point where we have concrete consumer for the subtree of
// (var/address/indrect/field/indexing) ops, we can then use this
// information to decide whether to materialize an address or perform
// a load.
//

class VarContext {
 public:
  VarContext(bool lvalue, unsigned addrLevel)
      : addrLevel_(addrLevel), lvalue_(lvalue) { }
  VarContext(const VarContext &src)
      : addrLevel_(src.addrLevel_), lvalue_(src.lvalue_)
  { }

  unsigned addrLevel() const { return addrLevel_; }
  unsigned lvalue() const { return lvalue_; }
  void incrementAddrLevel() { addrLevel_ += 1; }

 private:
  unsigned addrLevel_;
  bool lvalue_;
};

// Mix-in helper class for composite init expresions. Consider the following
// Go code:
//
//    func foo(qq int64) int64 {
//      var ad [4]int64 = [4]int64{ 0, 1, qq, 3 }
//
// Frontend will invoke the Backend::array_constructor_expression() method
// for the initializer ("{ 0, 1, qq, 3 }"). Because this initializer is not
// a pure constant (it contains a reference to the variable "qq"), the
// LLVM instructions we generate for it will have to store values to a
// chunk of memory. At the point where array_constructor_expression()
// is called, we don't have a good way to create a temporary variable, so
// we don't have anything to store to. In addition, since this initializer
// feeds directly into a variable, would be more efficient to generate
// the init code storing into "bar" as opposed to storing into a temporary
// and then copying over from the temp into "bar".
//
// To address these issues, this help class can be used to capture the
// sequence of instructions corresponding to the values for each element
// so as to delay creation of the initialization code.
//

class CompositeInitContext {
 public:
  CompositeInitContext(const std::vector<Bexpression *> &vals)
      : vals_(vals) { }
  std::vector<Bexpression *> &elementExpressions() { return vals_; }

 private:
   std::vector<Bexpression *> vals_;
};

// Here Bexpression is largely a wrapper around llvm::Value, however
// in addition to the consumable value it produces, a given
// Bexpression may also encapsulate a collection of other instructions
// that need to be executed prior to to that value. Semantics are
// roughly equivalent to the C++ comma operator, e.g. evaluate some
// set of expressions E1, E2, .. EK, then produce result EK.  See also
// the discussion in class Bstatement relating to the need to create
// expressions that wrap statements (hence the statement data member
// below).

class Bexpression : public Binstructions {
public:
  Bexpression(llvm::Value *value, Btype *btype, const std::string &tag = "");
  ~Bexpression();

  llvm::Value *value() const { return value_; }
  Btype *btype() { return btype_; }
  const std::string &tag() const { return tag_; }
  void setTag(const std::string &tag) { tag_ = tag; }

  bool varExprPending() const;
  VarContext &varContext() const;
  void setVarExprPending(bool lvalue, unsigned addrLevel);
  void setVarExprPending(const VarContext &vc);
  void resetVarExprContext();

  bool compositeInitPending() const;
  CompositeInitContext &compositeInitContext() const;
  void setCompositeInit(const std::vector<Bexpression *> &vals);
  void finishCompositeInit(llvm::Value *finalizedValue);

  Bstatement *stmt() { return stmt_; }
  void setStmt(Bstatement *st) { assert(st); stmt_ = st; }
  void incorporateStmt(Bstatement *src);

  // Delete some or all or this Bexpression. Deallocates just the
  // Bexpression, its contained instructions, or both (depending
  // on setting of 'which')
  static void destroy(Bexpression *expr, WhichDel which = DelWrappers);

  // debugging
  void dump();

  // dump to raw_ostream
  void osdump(llvm::raw_ostream &os, unsigned ilevel, bool terse = false);

private:
  Bexpression() : value_(NULL) {}
  llvm::Value *value_;
  Btype *btype_;
  Bstatement *stmt_;
  std::string tag_;
  std::unique_ptr<VarContext> varContext_;
  std::unique_ptr<CompositeInitContext> compositeInitContext_;
};

#endif // LLVMGOFRONTEND_GO_LLVM_BEXPRESSION_H
