//===-- go-llvm.h - LLVM implementation of gofrontend 'Backend' class -----===//
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

#ifndef LLVMGOFRONTEND_GO_LLVM_H
#define LLVMGOFRONTEND_GO_LLVM_H

// Currently these need to be included before backend.h
#include "go-linemap.h"
#include "go-location.h"

#include "backend.h"

#include <unordered_map>
#include <unordered_set>

namespace llvm {
class Argument;
class ArrayType;
class BasicBlock;
class Constant;
class ConstantFolder;
class DataLayout;
class Function;
class Instruction;
class LLVMContext;
class Module;
class StructType;
class TargetLibraryInfo;
class Type;
class Value;
class raw_ostream;
}

#include "llvm/IR/GlobalValue.h"

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
  explicit Btype(TyFlavor flavor, llvm::Type *type) :
      type_(type), flavor_(flavor), isPlaceholder_(false) { }
  virtual ~Btype() { }

  TyFlavor flavor() const { return flavor_; }

  llvm::Type *type() const { return type_; }
  void setType(llvm::Type *t) { assert(t); type_ = t; }
  const std::string &name() const { return name_; }
  void setName(const std::string &name) { name_ = name; }

  bool isPlaceholder() const { return isPlaceholder_; }
  void setPlaceholder(bool v) { isPlaceholder_ = v; }

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
  llvm::Type *type_;
  TyFlavor flavor_;
  std::string name_;
  bool isPlaceholder_;
};

class BIntegerType : public Btype {
 public:
  BIntegerType(bool isUnsigned, unsigned bits, llvm::Type *type)
      : Btype(IntegerT, type), bits_(bits), isUnsigned_(isUnsigned) { }

  bool isUnsigned() const { return isUnsigned_; }
  unsigned bits() const { return bits_; }

  // Create a shallow copy of this type
  Btype *clone() const {
    return new BIntegerType(isUnsigned_, bits_, type());
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
  BFloatType(unsigned bits, llvm::Type *type)
      : Btype(FloatT, type), bits_(bits) { }

  unsigned bits() const { return bits_; }

  // Create a shallow copy of this type
  Btype *clone() const {
    return new BFloatType(bits_, type());
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
  explicit BStructType(const std::vector<Backend::Btyped_identifier> &fields,
                       llvm::Type *type)
      : Btype(StructT, type), fields_(fields) { }

  // For placeholder struct types
  explicit BStructType(const std::string &name)
      : Btype(StructT, nullptr)
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
    return new BStructType(fields_, type());
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
  explicit BArrayType(Btype *elemType, Bexpression *nelements, llvm::Type *type)
      : Btype(ArrayT, type), elemType_(elemType), nelements_(nelements)
  {
    if (elemType_->isPlaceholder())
      setPlaceholder(true);
  }

  // For placeholder array types
  explicit BArrayType(const std::string &name)
      : Btype(ArrayT, nullptr), elemType_(nullptr), nelements_(nullptr)
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
    return new BArrayType(elemType_, nelements_, type());
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
  explicit BPointerType(Btype *toType, llvm::Type *type)
      : Btype(PointerT, type), toType_(toType) {
    if (toType_->isPlaceholder())
      setPlaceholder(true);
  }

  // For placeholder pointers
  explicit BPointerType(const std::string &name)
      : Btype(PointerT, nullptr), toType_(nullptr) {
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
    return new BPointerType(toType_, type());
  }

 private:
  Btype *toType_;
};

inline BPointerType *Btype::castToBPointerType() {
  return (flavor_ == PointerT ? static_cast<BPointerType *>(this)
          : nullptr);
};

inline const BPointerType *Btype::castToBPointerType() const {
  return (flavor_ == PointerT ? static_cast<const BPointerType *>(this)
          : nullptr);
};

class BFunctionType : public Btype {
 public:
  BFunctionType(Btype *receiverType,
                const std::vector<Btype *> &paramTypes,
                const std::vector<Btype *> &resultTypes,
                Btype *rtype,
                llvm::Type *type)
      : Btype(FunctionT, type), receiverType_(receiverType),
        paramTypes_(paramTypes), resultTypes_(resultTypes),
        rtype_(rtype) { }

  Btype *resultType() const { return rtype_; }
  Btype *receiverType() const { return receiverType_; }
  const std::vector<Btype *> &paramTypes() const { return paramTypes_; }

  // Create a shallow copy of this type
  Btype *clone() const {
    return new BFunctionType(receiverType_, paramTypes_, resultTypes_, rtype_,
                             type());
  }

 private:
  Btype *receiverType_;
  std::vector<Btype *> paramTypes_;
  std::vector<Btype *> resultTypes_;
  Btype *rtype_;
};

inline BFunctionType *Btype::castToBFunctionType() {
  return (flavor_ == FunctionT ? static_cast<BFunctionType *>(this)
          : nullptr);
}
inline const BFunctionType *Btype::castToBFunctionType() const {
  return (flavor_ == FunctionT ? static_cast<const BFunctionType *>(this)
          : nullptr);
}

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
// set of expressions E1, E2, .. EK, then produce result EK.
// See also the discussion in class Bstatement relating to
// the need to create expressions that wrap statements (hence the
// statement data member below).

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

// In the current gofrontend implementation, there is an extremely
// fluzzy/fluid line between Bexpression's, Bblocks, and Bstatements.
// For example, you can take a Bblock and turn it into a statement via
// Backend::block_statement. You can also combine an existing
// Bstatement with a Bexpression to form a second Bexpression via
// Backend::compound_expression. This is most likely due to the fact
// that the first backend (gcc) selected as a target for gofrontend
// uses a tree intermediate representation, where blocks, expressions,
// and statements are all simply tree fragments. With LLVM on the
// other hand, it is not so straightforward to have chunks of IR
// morphing back end forth between statements, expressions, and
// blocks.
//
// What this means from a practical point of view is that we delay
// creation of control flow (creating llvm::BasicBlock objects and
// assigning instructions to blocks) until the front end is
// essentially done with creating all statements and expressions.
// Prior to this point when the front end creates a statement that
// creates control flow (for example an "if" statement), we create
// placeholders to record what has to be done, then come along later
// and stitch things together at the end.

class CompoundStatement;
class ExprListStatement;
class IfPHStatement;
class SwitchPHStatement;
class GotoStatement;
class LabelStatement;

// Abstract base statement class, just used to distinguish between
// the various derived statement types.

class Bstatement {
public:
  enum StFlavor {
    ST_Compound,
    ST_ExprList,
    ST_IfPlaceholder,
    ST_SwitchPlaceholder,
    ST_Goto,
    ST_Label
  };

  Bstatement(Bfunction *function, StFlavor flavor, Location location)
      : function_(function), flavor_(flavor), location_(location) {}
  virtual ~Bstatement() {}
  StFlavor flavor() const { return flavor_; }
  Location location() const { return location_; }
  Bfunction *function() const { return function_; }

  inline CompoundStatement *castToCompoundStatement();
  inline ExprListStatement *castToExprListStatement();
  inline IfPHStatement *castToIfPHStatement();
  inline SwitchPHStatement *castToSwitchPHStatement();
  inline GotoStatement *castToGotoStatement();
  inline LabelStatement *castToLabelStatement();

  // Perform deletions on the tree of Bstatements rooted at stmt.
  // Delete Bstatements/Bexpressions, instructions, or both (depending
  // on setting of 'which')
  static void destroy(Bstatement *subtree, WhichDel which = DelWrappers);

  // debugging
  void dump();

  // dump to raw_ostream
  void osdump(llvm::raw_ostream &os, unsigned ilevel, bool terse = false);

 private:
  Bfunction *function_;
  StFlavor flavor_;
  Location location_;
};

// Compound statement is simply a list of other statements.

class CompoundStatement : public Bstatement {
public:
  explicit CompoundStatement(Bfunction *function)
      : Bstatement(function, ST_Compound, Location()) {}
  std::vector<Bstatement *> &stlist() { return stlist_; }

private:
  std::vector<Bstatement *> stlist_;
};

inline CompoundStatement *Bstatement::castToCompoundStatement() {
  return (flavor_ == ST_Compound ? static_cast<CompoundStatement *>(this)
                                 : nullptr);
}

// ExprList statement contains a list of LLVM instructions.

class ExprListStatement : public Bstatement {
public:
  explicit ExprListStatement(Bfunction *function)
      : Bstatement(function, ST_ExprList, Location()) {}
  ExprListStatement(Bfunction *function, Bexpression *e)
      : Bstatement(function, ST_ExprList, Location()) {
    expressions_.push_back(e);
  }
  void appendExpression(Bexpression *e) { expressions_.push_back(e); }
  std::vector<Bexpression *> expressions() { return expressions_; }

private:
  std::vector<Bexpression *> expressions_;
};

inline ExprListStatement *Bstatement::castToExprListStatement() {
  return (flavor_ == ST_ExprList ? static_cast<ExprListStatement *>(this)
                                 : nullptr);
}

// "If" placeholder statement.

class IfPHStatement : public Bstatement {
public:
  IfPHStatement(Bfunction *function, Bexpression *cond,
                Bstatement *ifTrue, Bstatement *ifFalse,
                Location location)
      : Bstatement(function, ST_IfPlaceholder, location)
      , cond_(cond)
      , iftrue_(ifTrue)
      , iffalse_(ifFalse) {}
  Bexpression *cond() { return cond_; }
  Bstatement *trueStmt() { return iftrue_; }
  Bstatement *falseStmt() { return iffalse_; }

private:
  Bexpression *cond_;
  Bstatement *iftrue_;
  Bstatement *iffalse_;
};

inline IfPHStatement *Bstatement::castToIfPHStatement() {
  return (flavor_ == ST_IfPlaceholder ? static_cast<IfPHStatement *>(this)
                                      : nullptr);
}

// "Switch" placeholder statement.

class SwitchPHStatement : public Bstatement {
public:
  SwitchPHStatement(Bfunction *function, Bexpression *value,
                    const std::vector<std::vector<Bexpression *>> &cases,
                    const std::vector<Bstatement *> &statements,
                    Location location)
      : Bstatement(function, ST_SwitchPlaceholder, location), value_(value),
        cases_(cases), statements_(statements) {}
  Bexpression *switchValue() { return value_; }
  std::vector<std::vector<Bexpression *>> &cases() { return cases_; }
  std::vector<Bstatement *> &statements() { return statements_; }

private:
  Bexpression *value_;
  std::vector<std::vector<Bexpression *>> cases_;
  std::vector<Bstatement *> statements_;
};

inline SwitchPHStatement *Bstatement::castToSwitchPHStatement() {
  return (flavor_ == ST_SwitchPlaceholder
              ? static_cast<SwitchPHStatement *>(this)
              : nullptr);
}

// Opaque labelID handle.
typedef unsigned LabelId;

class Blabel {
public:
  Blabel(const Bfunction *function, LabelId lab)
      : function_(const_cast<Bfunction *>(function)), lab_(lab) {}
  LabelId label() const { return lab_; }
  Bfunction *function() { return function_; }

private:
  Bfunction *function_;
  LabelId lab_;
};

// A goto statement, representing an unconditional jump to
// some other labeled statement.

class GotoStatement : public Bstatement {
public:
  GotoStatement(Bfunction *function, LabelId label, Location location)
      : Bstatement(function, ST_Goto, location), label_(label) {}
  LabelId targetLabel() const { return label_; }

private:
  LabelId label_;
};

inline GotoStatement *Bstatement::castToGotoStatement() {
  return (flavor_ == ST_Goto ? static_cast<GotoStatement *>(this) : nullptr);
}

// A label statement, representing the target of some jump (conditional
// or unconditional).

class LabelStatement : public Bstatement {
public:
  LabelStatement(Bfunction *function, LabelId label)
      : Bstatement(function, ST_Label, Location()), label_(label) {}
  LabelId definedLabel() const { return label_; }

private:
  LabelId label_;
};

inline LabelStatement *Bstatement::castToLabelStatement() {
  return (flavor_ == ST_Label ? static_cast<LabelStatement *>(this) : nullptr);
}

// A Bblock , which wraps statement list. See the comment
// above on why we need to make it easy to convert between
// blocks and statements.

class Bblock : public CompoundStatement {
 public:
  explicit Bblock(Bfunction *function)
      : CompoundStatement(function) { }
};

// Class Bfunction wraps llvm::Function

class Bfunction {
public:
  Bfunction(llvm::Function *f, Btype *fcnType, const std::string &asmName);
  ~Bfunction();

  llvm::Function *function() const { return function_; }
  Btype *fcnType() const { return fcnType_; }
  const std::string &asmName() const { return asmName_; }

  enum SplitStackDisposition { YesSplit, NoSplit };
  void setSplitStack(SplitStackDisposition disp) { splitStack_ = disp; }
  SplitStackDisposition splitStack() const { return splitStack_; }

  // Add a local variable
  Bvariable *local_variable(const std::string &name,
                            Btype *btype,
                            bool is_address_taken,
                            Location location);

  // Add a parameter variable
  Bvariable *parameter_variable(const std::string &name,
                                Btype *btype,
                                bool is_address_taken,
                                Location location);

  // Record a new Bblock for this function (do we need this?)
  void addBlock(Bblock *block) { blocks_.push_back(block); }

  // Create and return a new block
  Bblock *newBlock(Bfunction *function) {
    Bblock *block = new Bblock(function);
    blocks_.push_back(block);
    return block;
  }

  // Create a new label
  Blabel *newLabel();

  // Create a new label definition statement
  Bstatement *newLabelDefStatement(Blabel *label);

  // Create a new goto statement
  Bstatement *newGotoStatement(Blabel *label, Location location);

  // Create code to spill function arguments to entry block, insert
  // allocas for local variables.
  void genProlog(llvm::BasicBlock *entry);

  // Map back from an LLVM value (argument, alloca) to the Bvariable
  // we created to wrap it. Exposed for unit testing.
  Bvariable *getBvarForValue(llvm::Value *val);

  // Return Nth argument as llvm value. Exposed for unit testing.
  llvm::Value *getNthArgValue(unsigned argIdx);

private:

  // Record an alloca() instruction, to be added to entry block
  void addAlloca(llvm::Instruction *inst) { allocas_.push_back(inst); }

  // Return Nth argument
  llvm::Argument *getNthArg(unsigned argIdx);

  // Return alloca inst holding argument value (create if needed)
  llvm::Instruction *argValue(llvm::Argument *arg);

  // Number of parameter vars registered so far
  unsigned paramsCreated() { return argToVal_.size(); }

 private:
  std::vector<llvm::Instruction *> allocas_;
  std::vector<llvm::Argument *> arguments_;
  std::vector<Bblock *> blocks_;
  std::unordered_map<llvm::Value *, Bvariable *> valueVarMap_;
  std::unordered_map<llvm::Argument *, llvm::Instruction *> argToVal_;
  std::vector<Bstatement *> labelmap_;
  std::vector<Blabel *> labels_;
  llvm::Function *function_;
  Btype *fcnType_;
  std::string asmName_;
  unsigned labelCount_;
  SplitStackDisposition splitStack_;
};

// Back end variable class

enum WhichVar { ParamVar, GlobalVar, LocalVar, ErrorVar };

class Bvariable {
public:
  explicit Bvariable(Btype *type, Location location, const std::string &name,
                     WhichVar which, bool address_taken, llvm::Value *value)
      : name_(name), location_(location), value_(value), type_(type),
        which_(which), addrtaken_(address_taken) {}

  // Common to all varieties of variables
  Location getLocation() { return location_; }
  Btype *getType() { return type_; }
  const std::string &getName() { return name_; }
  llvm::Value *value() { return value_; }
  bool addrtaken() { return addrtaken_; }
  WhichVar flavor() const { return which_; }

private:
  Bvariable() = delete;
  const std::string name_;
  Location location_;
  llvm::Value *value_;
  Btype *type_;
  WhichVar which_;
  bool addrtaken_;

  friend class Llvm_backend;
};

//
// LLVM-specific implementation of the Backend class; the code in
// gofrontend instantiates an object of this class and then invokes
// the various methods to convert its IR into LLVM IR. Nearly all of
// the interesting methods below are virtual.
//

class Llvm_backend : public Backend {
public:
  Llvm_backend(llvm::LLVMContext &context);
  ~Llvm_backend();

  // Types.

  Btype *error_type();

  Btype *void_type();

  Btype *bool_type();

  Btype *integer_type(bool, int);

  Btype *float_type(int);

  Btype *complex_type(int);

  Btype *pointer_type(Btype *);

  Btype *function_type(const Btyped_identifier &,
                       const std::vector<Btyped_identifier> &,
                       const std::vector<Btyped_identifier> &, Btype *,
                       const Location);

  Btype *struct_type(const std::vector<Btyped_identifier> &);

  Btype *array_type(Btype *, Bexpression *);

  Btype *placeholder_pointer_type(const std::string &, Location, bool);

  bool set_placeholder_pointer_type(Btype *, Btype *);

  bool set_placeholder_function_type(Btype *, Btype *);

  Btype *placeholder_struct_type(const std::string &, Location);

  bool set_placeholder_struct_type(Btype *placeholder,
                                   const std::vector<Btyped_identifier> &);

  Btype *placeholder_array_type(const std::string &, Location);

  bool set_placeholder_array_type(Btype *, Btype *, Bexpression *);

  Btype *named_type(const std::string &, Btype *, Location);

  Btype *circular_pointer_type(Btype *, bool);

  bool is_circular_pointer_type(Btype *);

  int64_t type_size(Btype *);

  int64_t type_alignment(Btype *);

  int64_t type_field_alignment(Btype *);

  int64_t type_field_offset(Btype *, size_t index);

  // Expressions.

  Bexpression *zero_expression(Btype *);

  Bexpression *error_expression();

  Bexpression *nil_pointer_expression();

  Bexpression *var_expression(Bvariable *var, Varexpr_context in_lvalue_pos,
                              Location);

  Bexpression *indirect_expression(Btype *, Bexpression *expr, bool known_valid,
                                   Location);

  Bexpression *named_constant_expression(Btype *btype, const std::string &name,
                                         Bexpression *val, Location);

  Bexpression *integer_constant_expression(Btype *btype, mpz_t val);

  Bexpression *float_constant_expression(Btype *btype, mpfr_t val);

  Bexpression *complex_constant_expression(Btype *btype, mpc_t val);

  Bexpression *string_constant_expression(const std::string &val);

  Bexpression *boolean_constant_expression(bool val);

  Bexpression *real_part_expression(Bexpression *bcomplex, Location);

  Bexpression *imag_part_expression(Bexpression *bcomplex, Location);

  Bexpression *complex_expression(Bexpression *breal, Bexpression *bimag,
                                  Location);

  Bexpression *convert_expression(Btype *type, Bexpression *expr, Location);

  Bexpression *function_code_expression(Bfunction *, Location);

  Bexpression *address_expression(Bexpression *, Location);

  Bexpression *struct_field_expression(Bexpression *, size_t, Location);

  Bexpression *compound_expression(Bstatement *, Bexpression *, Location);

  Bexpression *conditional_expression(Bfunction *,
                                      Btype *, Bexpression *, Bexpression *,
                                      Bexpression *, Location);

  Bexpression *unary_expression(Operator, Bexpression *, Location);

  Bexpression *binary_expression(Operator, Bexpression *, Bexpression *,
                                 Location);

  Bexpression *
  constructor_expression(Btype *, const std::vector<Bexpression *> &, Location);

  Bexpression *array_constructor_expression(Btype *,
                                            const std::vector<unsigned long> &,
                                            const std::vector<Bexpression *> &,
                                            Location);

  Bexpression *pointer_offset_expression(Bexpression *base, Bexpression *offset,
                                         Location);

  Bexpression *array_index_expression(Bexpression *array, Bexpression *index,
                                      Location);

  Bexpression *call_expression(Bexpression *fn,
                               const std::vector<Bexpression *> &args,
                               Bexpression *static_chain, Location);

  Bexpression *stack_allocation_expression(int64_t size, Location);

  // Statements.

  Bstatement *error_statement();

  Bstatement *expression_statement(Bfunction *, Bexpression *);

  Bstatement *init_statement(Bfunction*, Bvariable *var, Bexpression *init);

  Bstatement *assignment_statement(Bfunction*,
                                   Bexpression *lhs, Bexpression *rhs,
                                   Location);

  Bstatement *return_statement(Bfunction *, const std::vector<Bexpression *> &,
                               Location);

  Bstatement *if_statement(Bfunction *func,
                           Bexpression *condition, Bblock *then_block,
                           Bblock *else_block, Location);

  Bstatement *
  switch_statement(Bfunction *function, Bexpression *value,
                   const std::vector<std::vector<Bexpression *>> &cases,
                   const std::vector<Bstatement *> &statements, Location);

  Bstatement *compound_statement(Bstatement *, Bstatement *);

  Bstatement *statement_list(const std::vector<Bstatement *> &);

  Bstatement *exception_handler_statement(Bstatement *bstat,
                                          Bstatement *except_stmt,
                                          Bstatement *finally_stmt, Location);

  // Blocks.

  Bblock *block(Bfunction *, Bblock *, const std::vector<Bvariable *> &,
                Location, Location);

  void block_add_statements(Bblock *, const std::vector<Bstatement *> &);

  Bstatement *block_statement(Bblock *);

  // Variables.

  Bvariable *error_variable();

  Bvariable *global_variable(const std::string &var_name,
                             const std::string &asm_name, Btype *btype,
                             bool is_external, bool is_hidden,
                             bool in_unique_section, Location location);

  void global_variable_set_init(Bvariable *, Bexpression *);

  Bvariable *local_variable(Bfunction *, const std::string &, Btype *, bool,
                            Location);

  Bvariable *parameter_variable(Bfunction *, const std::string &, Btype *, bool,
                                Location);

  Bvariable *static_chain_variable(Bfunction *, const std::string &, Btype *,
                                   Location);

  Bvariable *temporary_variable(Bfunction *, Bblock *, Btype *, Bexpression *,
                                bool, Location, Bstatement **);

  Bvariable *implicit_variable(const std::string &, const std::string &,
                               Btype *, bool, bool, bool, int64_t);

  void implicit_variable_set_init(Bvariable *, const std::string &, Btype *,
                                  bool, bool, bool, Bexpression *);

  Bvariable *implicit_variable_reference(const std::string &,
                                         const std::string &, Btype *);

  Bvariable *immutable_struct(const std::string &, const std::string &, bool,
                              bool, Btype *, Location);

  void immutable_struct_set_init(Bvariable *, const std::string &, bool, bool,
                                 Btype *, Location, Bexpression *);

  Bvariable *immutable_struct_reference(const std::string &,
                                        const std::string &, Btype *, Location);

  // Labels.

  Blabel *label(Bfunction *, const std::string &name, Location);

  Bstatement *label_definition_statement(Blabel *);

  Bstatement *goto_statement(Blabel *, Location);

  Bexpression *label_address(Blabel *, Location);

  // Functions.

  Bfunction *error_function();

  Bfunction *function(Btype *fntype, const std::string &name,
                      const std::string &asm_name, bool is_visible,
                      bool is_declaration, bool is_inlinable,
                      bool disable_split_stack, bool in_unique_section,
                      Location);

  Bstatement *function_defer_statement(Bfunction *function,
                                       Bexpression *undefer, Bexpression *defer,
                                       Location);

  bool function_set_parameters(Bfunction *function,
                               const std::vector<Bvariable *> &);

  bool function_set_body(Bfunction *function, Bstatement *code_stmt);

  Bfunction *lookup_builtin(const std::string &);

  void write_global_definitions(const std::vector<Btype *> &,
                                const std::vector<Bexpression *> &,
                                const std::vector<Bfunction *> &,
                                const std::vector<Bvariable *> &);

  // Exposed for unit testing
  llvm::Module &module() { return *module_.get(); }

  // Run the module verifier.
  void verifyModule();

  // Dump LLVM IR for module
  void dumpModule();

  // Exposed for unit testing

  // Helpers to check tree integrity. Checks to make sure that
  // we don't have instructions that are parented by more than
  // one Bexpression or stmt. Returns <TRUE,""> if tree is ok,
  // otherwise returns <FALSE,descriptive_message>.
  std::pair<bool, std::string>
  checkTreeIntegrity(Bexpression *e, bool includePointers = true);
  std::pair<bool, std::string>
  checkTreeIntegrity(Bstatement *s,  bool includePointers = true);

  // Similar to the above, but prints message to std::cerr and aborts if fail
  void enforceTreeIntegrity(Bexpression *e);
  void enforceTreeIntegrity(Bstatement *s);

  // Disable tree integrity checking. This is mainly
  // so that we can unit test the integrity checker.
  void disableIntegrityChecks() { checkIntegrity_ = false; }

  // Return true if this is a module-scope value such as a constant
  bool moduleScopeValue(llvm::Value *val, Btype *btype) const;

  // For debugging
  void setTraceLevel(unsigned level) { traceLevel_ = level; }
  unsigned traceLevel() const { return traceLevel_; }

  // For creating useful inst and block names. Exposed to help unit testing.
  std::string namegen(const std::string &tag, unsigned expl = 0xffffffff);

 private:

  enum PTDisp { Concrete, Placeholder };

  // Create a new anonymous Btype based on LLVM type 'lt'. This is used
  // for types where there is a direct corresponding between the LLVM type
  // and the frontend type (ex: float32), and where we don't need to
  // do any later post-processing or checking.
  Btype *makeAuxType(llvm::Type *lt);

  // Is this a placeholder type?
  bool isPlaceholderType(Btype *t);

  // Replace the underlying type for a given placeholder type once
  // we've determined what the final type will be.
  void updatePlaceholderUnderlyingType(Btype *plt, Btype *totype);

  // Create an opaque type for use as part of a placeholder type.
  // Type will be named according to the tag passed in (name is relevant
  // only for debugging).
  llvm::Type *makeOpaqueLlvmType(const char *tag);

  // LLVM type creation helpers
  llvm::Type *makeLLVMFloatType(int bits);
  llvm::Type *makeLLVMStructType(const std::vector<Btyped_identifier> &fields);
  llvm::Type *makeLLVMFunctionType(Btype *receiverType,
                                   const std::vector<Btype *> &paramTypes,
                                   const std::vector<Btype *> &resultTypes,
                                   Btype *rbtype);

  // Returns field type from composite (struct/array) type and index
  Btype *elementTypeByIndex(Btype *type, unsigned element_index);

  // Returns function result type from pointer-to-function type
  Btype *functionReturnType(Btype *functionType);

  // When making a change to a Btype (for example,modifying its underlying
  // type or setting/resetting its placeholder flag) we need to
  // remove it from anonTypes and then reinstall it after we're
  // done making changes. These routines help with that process.
  // 'removeAnonType' returns true if the type in question was in
  // the anonTypes set.
  bool removeAnonType(Btype *typ);
  void reinstallAnonType(Btype *typ);

  // The specified placeholder 'btype' has been resolved to a
  // concrete type -- visit all of the types that refer to it
  // and see if we can completely resolve them.
  void postProcessResolvedPlaceholder(Btype *btype);

  // Helpers for the routine above
  void postProcessResolvedPointerPlaceholder(BPointerType *bpt, Btype *btype);
  void postProcessResolvedStructPlaceholder(BStructType *bst, Btype *btype);
  void postProcessResolvedArrayPlaceholder(BArrayType *bat, Btype *btype);

  // For a newly create type, adds entries to the placeholderRefs
  // table for any contained types. Returns true if any placeholders
  // found.
  bool addPlaceholderRefs(Btype *type);

  // add a builtin function definition
  void defineBuiltinFcn(const char *name, const char *libname,
                        llvm::Function *fcn);

  // varargs convenience wrapper for define_builtin_fcn.
  // creates a libcall builtin. If the builtin being created is defined
  // in llvm/Analysis/TargetLibraryInfo.def, then the proper enumeration
  // should be passed in "libfuncID" (otherwise pass NumLibFuncs).
  // varargs: first arg after libfuncID is return type, following
  // arguments are parameter types, followed by NULL type indicating
  // end of params.
  void defineLibcallBuiltin(const char *name, const char *libname,
                            unsigned libfuncID, ...);

  // similar to the routine above, but takes a vector of
  // types as opposed to an argument list.
  void defineLibcallBuiltin(const char *name, const char *libname,
                            const std::vector<llvm::Type *> &types,
                            unsigned libfuncID);

  // varargs convenience wrapper for define_builtin_fcn;
  // creates in intrinsic builtin by looking up intrinsic
  // 'intrinsicID'; variable arguments the overloaded types required
  // by llvm::Intrinsic::getDeclaration (see the comments on that
  // function for more info) terminated by NULL.
  void defineIntrinsicBuiltin(const char *name, const char *libname,
                              unsigned intrinsicID, ...);

  // more helpers for builtin creation
  void defineAllBuiltins();
  void defineSyncFetchAndAddBuiltins();
  void defineIntrinsicBuiltins();
  void defineTrigBuiltins();

  // Create a Bexpression to hold an llvm::Value. Some Bexpressions
  // we want to cache (constants for example, or lvalue references to
  // global variables); for these cases scope should be set to "GlobalScope".
  // For non-cacheable values (for example, an lvalue reference to a local
  // var in a function), set scope to LocalScope (no caching in this case).
  enum ValExprScope { GlobalScope, LocalScope };
  Bexpression *makeValueExpression(llvm::Value *val,
                                   Btype *btype,
                                   ValExprScope scope);

  enum ModVarConstant { MV_Constant, MV_NonConstant };
  enum ModVarSec { MV_UniqueSection, MV_DefaultSection };
  enum ModVarComdat { MV_InComdat, MV_NotInComdat };
  enum ModVarVis { MV_HiddenVisibility, MV_DefaultVisibility };

  // Make a module-scope variable (static, global, or external).
  Bvariable *makeModuleVar(Btype *btype,
                           const std::string &name,
                           const std::string &asm_name,
                           Location location,
                           ModVarConstant constant,
                           ModVarSec inUniqueSection,
                           ModVarComdat inComdat,
                           ModVarVis isHiddenVisibility,
                           llvm::GlobalValue::LinkageTypes linkage,
                           llvm::Constant *initializer,
                           unsigned alignmentInBytes = 0);

  // Combing the contents of a list of src expressions to produce
  // a new expression.
  Bexpression *makeExpression(llvm::Value *value,
                              Btype *btype,
                              Bexpression *src, ...);

  // Helper for creating a constant-valued array/struct expression.
  Bexpression *makeConstCompositeExpr(Btype *btype,
                                      llvm::CompositeType *llct,
                                      unsigned numElements,
                                      const std::vector<unsigned long> &indexes,
                                      const std::vector<Bexpression *> &vals,
                                      Location location);

  // Helper for creating a non-constant-valued array or struct expression.
  Bexpression *makeDelayedCompositeExpr(Btype *btype,
                                        llvm::CompositeType *llct,
                                        unsigned numElements,
                                        const std::vector<unsigned long> &idxs,
                                        const std::vector<Bexpression *> &vals,
                                        Location location);

  // Field GEP helper
  llvm::Value *makeFieldGEP(llvm::StructType *llst,
                            unsigned fieldIndex,
                            llvm::Value *sptr);

  // Array indexing GEP helper
  llvm::Value *makeArrayIndexGEP(llvm::ArrayType *art,
                                 llvm::Value *idx,
                                 llvm::Value *sptr);

  // Create new Bstatement from an expression.
  ExprListStatement *stmtFromExpr(Bfunction *function, Bexpression *expr);

  // Assignment helper
  Bstatement *makeAssignment(Bfunction *function, llvm::Value *lvalue,
                             Bexpression *lhs, Bexpression *rhs, Location);

  // Helper to set up entry block for function
  llvm::BasicBlock *genEntryBlock(Bfunction *bfunction);

  // Helper to fix up epilog block for function (add return if needed)
  void fixupEpilogBlog(Bfunction *bfunction, llvm::BasicBlock *epilog);

  // Var expr management
  Bexpression *resolveVarContext(Bexpression *expr);
  Bexpression *loadFromExpr(Bexpression *expr, Btype* &loadResultType,
                            Location location);

  // Examine vector of values to test whether they are constants.
  // Checks for and handles pending composite inits.
  static bool
  valuesAreConstant(const std::vector<Bexpression *> &vals);

  // Composite init management
  Bexpression *resolveCompositeInit(Bexpression *expr,
                                    Bfunction *func,
                                    llvm::Value *storage = nullptr);

  // Array init helper
  Bexpression *genArrayInit(llvm::ArrayType *llat,
                            Bexpression *expr,
                            llvm::Value *storage);

  // Struct init helper
  Bexpression *genStructInit(llvm::StructType *llst,
                             Bexpression *expr,
                             llvm::Value *storage);

  // General-purpose resolver, handles var expr context and
  // composite init context.
  Bexpression *resolve(Bexpression *expr, Bfunction *func);

  // Check a vector of Bexpression's to see if any are the
  // error expression, returning TRUE if so.
  bool exprVectorHasError(const std::vector<Bexpression *> &vals) {
    for (auto v : vals)
      if (v == errorExpression_.get())
        return true;
    return false;
  }

  // Conversion helper.
  llvm::Type *isAcceptableBitcastConvert(Bexpression *expr,
                                         llvm::Type *fromType,
                                         llvm::Type *toType);

private:
  template <typename T1, typename T2> class pairvalmap_hash {
    typedef std::pair<T1, T2> pairtype;

  public:
    unsigned int operator()(const pairtype &p) const {
      std::size_t h1 = std::hash<T1>{}(p.first);
      std::size_t h2 = std::hash<T2>{}(p.second);
      return h1 + h2;
    }
  };

  template <typename T1, typename T2> class pairvalmap_equal {
    typedef std::pair<T1, T2> pairtype;

  public:
    bool operator()(const pairtype &p1, const pairtype &p2) const {
      return (p1.first == p2.first && p1.second == p2.second);
    }
  };

  template <typename T1, typename T2, typename V>
  using pairvalmap =
      std::unordered_map<std::pair<T1, T2>, V, pairvalmap_hash<T1, T2>,
                         pairvalmap_equal<T1, T2>>;

  typedef std::pair<const std::string, llvm::Type *> named_llvm_type;
  typedef pairvalmap<std::string, llvm::Type *, Btype *> named_type_maptyp;

  typedef std::pair<llvm::Type *, bool> type_plus_unsigned;
  typedef pairvalmap<llvm::Type *, bool, Btype *> integer_type_maptyp;

  typedef std::pair<llvm::Value *, Btype *> valbtype;
  typedef pairvalmap<llvm::Value *, Btype *, Bexpression *>
  btyped_value_expr_maptyp;

  typedef std::pair<Btype *, unsigned> structplusindextype;
  typedef pairvalmap<Btype *, unsigned, Btype *> fieldmaptype;

  // Context information needed for the LLVM backend.
  llvm::LLVMContext &context_;
  std::unique_ptr<llvm::Module> module_;
  const llvm::DataLayout &datalayout_;
  unsigned addressSpace_;
  unsigned traceLevel_;
  bool checkIntegrity_;

  class btype_hash {
  public:
    unsigned int operator()(const Btype *t) const {
      return t->hash();
    }
  };

  class btype_equal {
  public:
    bool operator()(const Btype *t1, const Btype *t2) const {
      return t1->equal(*t2);
    }
  };

  typedef std::unordered_set<Btype *, btype_hash, btype_equal> anonTypeSetType;

  // Anonymous typed are hashed/commoned via this set.
  anonTypeSetType anonTypes_;

  // This map stores oddball types that get created internally by
  // the back end (ex: void type, or predefined complex). Key is
  // LLVM type, value is Btype.
  std::unordered_map<llvm::Type *, Btype *> auxTypeMap_;

  // Repository for named types (those specifically created by the
  // ::named_type method).
  std::unordered_set<Btype *> namedTypes_;

  // Records all placeholder types explicitlt created viar
  // Backend::placeholder_<XYZ>_type() method calls.
  std::unordered_set<Btype *> placeholders_;

  // These types became redundant/duplicate after one or more
  // of their placeholder children were updated.
  std::unordered_set<Btype *> duplicates_;

  // For managing placeholder types. An entry [X, {A,B,C}] indicates
  // that placeholder type X is referred to by the other placeholder
  // types A, B, and C.
  std::unordered_map<Btype *, std::set<Btype *> > placeholderRefs_;

  // Set of circular types. These are pointers to opaque types that
  // are returned by the ::circular_pointer_type() method.
  std::unordered_set<llvm::Type *> circularPointerTypes_;

  // Map from placeholder type to circular pointer type. Key is placeholder
  // pointer type, value is circular pointer type marker.
  std::unordered_map<Btype *, Btype *> circularPointerTypeMap_;

  // Maps for inserting conversions involving circular pointers.
  std::unordered_map<Btype *, Btype *> circularConversionLoadMap_;
  std::unordered_map<Btype *, Btype *> circularConversionAddrMap_;

  // For storing the pointers involved in a circular pointer type loop.
  // Temporary; filled in only during processing of the loop.
  typedef std::pair<Btype *, Btype *> btpair;
  std::vector<btpair> circularPointerLoop_;

  // Various predefined or pre-computed types that we cache away
  Btype *complexFloatType_;
  Btype *complexDoubleType_;
  Btype *errorType_;
  Btype *stringType_;
  llvm::Type *llvmVoidType_;
  llvm::Type *llvmBoolType_;
  llvm::Type *llvmPtrType_;
  llvm::Type *llvmSizeType_;
  llvm::Type *llvmIntegerType_;
  llvm::Type *llvmInt8Type_;
  llvm::Type *llvmInt32Type_;
  llvm::Type *llvmInt64Type_;
  llvm::Type *llvmFloatType_;
  llvm::Type *llvmDoubleType_;
  llvm::Type *llvmLongDoubleType_;

  // Target library info oracle
  llvm::TargetLibraryInfo *TLI_;

  // maps name to builtin function
  std::unordered_map<std::string, Bfunction *> builtinMap_;

  // Error function
  std::unique_ptr<Bfunction> errorFunction_;

  // Error expression
  std::unique_ptr<Bexpression> errorExpression_;

  // Error statement
  std::unique_ptr<Bstatement> errorStatement_;

  // Error variable
  std::unique_ptr<Bvariable> errorVariable_;

  // Map from LLVM-value/Btype pair to Bexpression. This is
  // used to cache + reuse things like global constants.
  btyped_value_expr_maptyp valueExprmap_;

  // Map from LLVM values to Bvariable.
  std::unordered_map<llvm::Value *, Bvariable *> valueVarMap_;

  // For creation of useful block and inst names. Key is tag (ex: "add")
  // and val is counter to uniquify.
  std::unordered_map<std::string, unsigned> nametags_;

  // Currently we don't do any commoning of Bfunction objects created
  // by the frontend, so here we keep track of all returned Bfunctions
  // so that we can free them on exit.
  std::vector<Bfunction *> functions_;

  // Keep track of Bexpression's we've given out to the front end
  // (those not appearing in other maps) so that we can delete them
  // when we're done with them.
  std::vector<Bexpression *> expressions_;
};

#endif
