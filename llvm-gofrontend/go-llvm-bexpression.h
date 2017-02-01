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
#include "go-llvm-bnode.h"

#include "backend.h"

namespace llvm {
class Instruction;
class Value;
class raw_ostream;
}

class Bstatement;
class BnodeBuilder;

// Mixin class for a list of instructions

class Binstructions {
public:
  Binstructions() {}
  explicit Binstructions(const std::vector<llvm::Instruction *> instructions)
      : instructions_(instructions) {}

  const std::vector<llvm::Instruction *> &instructions() const {
    return instructions_;
  }
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
// composite initializers. Consider the following Go code:
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
// To address this, non-constant composite expressions are created
// initially without an LLVM value, then once we reach the point where
// we know what the storage has to be, someone makes a call to
// BnodeBuilder::finishComposite to generate the necessary stor
// instructions and finalize the LLVM value.
//
// Second area where we want to delay things is in handling of
// variable expressions. For example, consider the following Go code:
//
//        struct X { a, b int64 }
//        func foo(q, r int64, ip *int64, px *X) int64 {
//           r = q
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
// To deal with this, we use this helper class below to record whether
// the subtree in question contains a root variable expression, and if
// so, the number of "address" operators that have been applied, and
// whether the original varexpr appears in an assignment (lvalue)
// context. Once we reach a point where we have concrete consumer for
// the subtree of (var/address/indrect/field/indexing) ops, we can
// then use this information to decide whether to materialize an
// address or perform a load.
//

class VarContext {
 public:
  VarContext() : addrLevel_(0), lvalue_(false), pending_(false) { }
  VarContext(bool lvalue, unsigned addrLevel)
      : addrLevel_(addrLevel), lvalue_(lvalue), pending_(true) { }
  explicit VarContext(const VarContext &src)
      : addrLevel_(src.addrLevel_), lvalue_(src.lvalue_)
  { }

  bool pending() const { return pending_; }
  unsigned addrLevel() const { return addrLevel_; }
  unsigned lvalue() const { return lvalue_; }
  void incrementAddrLevel() { addrLevel_ += 1; }
  void setPending(bool lvalue, unsigned addrLevel) {
    assert(!pending_);
    pending_ = true;
    lvalue_ = lvalue;
    addrLevel_ = addrLevel;
  }
  void reset() { assert(pending_); pending_ = false; }

 private:
  unsigned addrLevel_;
  bool lvalue_;
  bool pending_;
};

class Bexpression : public Bnode, public Binstructions {
 public:
  // no public constructor, use BnodeBuilder instead
  virtual ~Bexpression();

  llvm::Value *value() const { return value_; }
  Btype *btype() const { return btype_; }
  const std::string &tag() const { return tag_; }
  void setTag(const std::string &tag) { tag_ = tag; }

  bool varExprPending() const;
  const VarContext &varContext() const;
  void setVarExprPending(bool lvalue, unsigned addrLevel);
  void setVarExprPending(const VarContext &vc);
  void resetVarExprContext();
  bool compositeInitPending() const;
  const std::vector<Bexpression *> getChildExprs() const;

  // debugging
  void dumpInstructions(llvm::raw_ostream &os, unsigned ilevel,
                        Linemap *linemap, bool terse) const;

  // dump with source line info
  void srcDump(Linemap *);

  friend class BnodeBuilder;

 private:
  Bexpression(NodeFlavor fl, const std::vector<Bnode *> &kids,
              llvm::Value *val, Btype *typ, Location loc);

  llvm::Value *value_;
  Btype *btype_;
  std::string tag_;
  VarContext varContext_;
};

#endif // LLVMGOFRONTEND_GO_LLVM_BEXPRESSION_H
