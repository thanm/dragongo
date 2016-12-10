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
class BasicBlock;
class DataLayout;
class Function;
class Instruction;
class LLVMContext;
class Module;
class TargetLibraryInfo;
class Type;
class Value;
class raw_ostream;
}

#include "llvm/IR/GlobalValue.h"

// Btype wraps llvm::Type

class Btype {
public:
  explicit Btype(llvm::Type *type) : type_(type), isUnsigned_(false) {}

  llvm::Type *type() const { return type_; }

  // For integer types.
  bool isUnsigned() { return isUnsigned_; }
  void setUnsigned() { isUnsigned_ = true; }

  // debugging
  void dump();

  // dump to raw ostream buffer
  void osdump(llvm::raw_ostream &os, unsigned ilevel);

 private:
  Btype() : type_(NULL) {}
  llvm::Type *type_;
  bool isUnsigned_;
  friend class Llvm_backend;
};

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
    for (auto i : ilist)
      instructions_.push_back(i);
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
// This eager strategy works well in general but has to be relaxed a
// bit in some instances, notably variable references and composite
// initializers.  For example, consider the following Go code:
//
//        func foo(q int64, ip *int64) int64 {
//           y := q
//           x := **&ip
//           ip = &q
//
// The right hand side expression trees for these statements would look like:
//
//        stmt 1:   varexpr("q")
//        stmt 2:   deref(deref(address(varexpr("ip")))).
//        stmt 3:   address(varexpr("q"))
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
// "address" operators that have been applied. Once we reach a point
// where we have concrete consumer for the subtree of
// (var/address/indrect) ops, we can then use this information to
// decide whether to materialize an address or perform a load.
//

class VarContext {
 public:
  VarContext() : addrLevel_(0), pending_(false) { }
  VarContext(unsigned addrLevel, bool pending)
      : addrLevel_(addrLevel), pending_(pending) { }

  bool varPending() const { return pending_; }
  bool addrLevel() const { return addrLevel_; }

  void setVarExprPending(unsigned addrLevel) {
    addrLevel_ = addrLevel;
    pending_ = true;
  }
  void resetVarExprPending() {
    pending_ = false;
  }

 private:
  unsigned addrLevel_;
  bool pending_;
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
  CompositeInitContext() : pending_(false) { }

  bool compositeInitPending() const { return pending_; }
  void setCompositeInit(const std::vector<Bexpression *> &vals,
                        Btype *compositeType) {
    vals_.reset(new std::vector<Bexpression *>(vals));
    pending_ = true;
  }
  std::vector<Bexpression *> &elementExpressions() {
    assert(pending_);
    return *vals_.get();
  }
  void resetCompositeInit() {
    vals_.reset(nullptr);
    pending_ = false;
  }

 private:
  std::unique_ptr<std::vector<Bexpression *> > vals_;
  bool pending_;
};

// Here Bexpression is largely a wrapper around llvm::Value, however
// in addition to the consumable value it produces, a given
// Bexpression may also encapsulate a collection of other instructions
// that need to be executed prior to to that value. Semantics are
// roughly equivalent to the C++ comma operator, e.g. evaluate some
// set of expressions E1, E2, .. EK, then produce result EK.

class Bexpression : public Binstructions, public VarContext, public CompositeInitContext {
public:
  Bexpression(llvm::Value *value, Btype *btype, const std::string &tag = "");
  ~Bexpression();

  llvm::Value *value() const { return value_; }
  Btype *btype() { return btype_; }
  const std::string &tag() const { return tag_; }
  void setTag(const std::string &tag) { tag_ = tag; }

  // Delete some or all or this Bexpression. Deallocates just the
  // Bexpression, its contained instructions, or both (depending
  // on setting of 'which')
  static void destroy(Bexpression *expr, WhichDel which = DelWrappers);

  // debugging
  void dump();

  // dump to raw_ostream
  void osdump(llvm::raw_ostream &os, unsigned ilevel);

  void finishCompositeInit(llvm::Value *finalizedValue) {
    assert(value_ == nullptr);
    assert(finalizedValue);
    value_ = finalizedValue;
    resetCompositeInit();
  }

private:
  Bexpression() : value_(NULL) {}
  llvm::Value *value_;
  Btype *btype_;
  std::string tag_;
};

// In the current gofrontend implementation, there is an extremely
// fluzzy/fluid line between Bexpression's, Bblocks, and Bstatements.
// For example, you can take a Bblock and turn it into a statement via
// Backend::block_statement. You can also combine an existing
// Bstatement with a Bexpression to form a second Bexpression via
// Backend::compound_expression. This is most likely due to the fact
// that the first backend target (gcc) for gofrontend uses a tree
// intermediate representation, where blocks, expressions, and
// statements are all simply tree fragments. With LLVM on the other
// hand, it is not so straightforward to have chunks of IR morphing
// back end forth between statements, expressions, and blocks.
//
// What this means from a practical point of view is that we delay
// creation of control flow (creating llvm::BasicBlock objects and
// assigning instructions to blocks) until the front end is
// essentially done with creating all instructions and
// statements. Prior to this point when the front end creates a
// statement that creates control flow (for example an "if"
// statement), we create placeholders to record what has to be done,
// then come along later and stitch things together at the end.

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

  Bstatement(StFlavor flavor, Location location)
      : flavor_(flavor), location_(location) {}
  virtual ~Bstatement() {}
  StFlavor flavor() const { return flavor_; }
  Location location() const { return location_; }

  inline CompoundStatement *castToCompoundStatement();
  inline ExprListStatement *castToExprListStatement();
  inline IfPHStatement *castToIfPHStatement();
  inline SwitchPHStatement *castToSwitchPHStatement();
  inline GotoStatement *castToGotoStatement();
  inline LabelStatement *castToLabelStatement();

  // Create new Bstatement from an expression.
  static ExprListStatement *stmtFromExpr(Bexpression *expr);

  // Perform deletions on the tree of Bstatements rooted at stmt.
  // Delete Bstatements/Bexpressions, instructions, or both (depending
  // on setting of 'which')
  static void destroy(Bstatement *subtree, WhichDel which = DelWrappers);

  // debugging
  void dump();

  // dump to raw_ostream
  void osdump(llvm::raw_ostream &os, unsigned ilevel);

 private:
  StFlavor flavor_;
  Location location_;
};

// Compound statement is simply a list of other statements.

class CompoundStatement : public Bstatement {
public:
  CompoundStatement() : Bstatement(ST_Compound, Location()) {}
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
  ExprListStatement() : Bstatement(ST_ExprList, Location()) {}
  ExprListStatement(Bexpression *e) : Bstatement(ST_ExprList, Location()) {
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
  IfPHStatement(Bexpression *cond, Bstatement *ifTrue, Bstatement *ifFalse,
                Location location)
      : Bstatement(ST_IfPlaceholder, location), cond_(cond), iftrue_(ifTrue),
        iffalse_(ifFalse) {}
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
  SwitchPHStatement(Bexpression *value,
                    const std::vector<std::vector<Bexpression *>> &cases,
                    const std::vector<Bstatement *> &statements,
                    Location location)
      : Bstatement(ST_SwitchPlaceholder, location), value_(value),
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
  GotoStatement(LabelId label, Location location)
      : Bstatement(ST_Goto, location), label_(label) {}
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
  LabelStatement(LabelId label)
      : Bstatement(ST_Label, Location()), label_(label) {}
  LabelId definedLabel() const { return label_; }

private:
  LabelId label_;
};

inline LabelStatement *Bstatement::castToLabelStatement() {
  return (flavor_ == ST_Label ? static_cast<LabelStatement *>(this) : nullptr);
}

// A Bblock , which wraps statement list. Ssee the comment
// above on why we need to make it easy to convert between
// blocks and statements.

class Bblock : public CompoundStatement {};

// Class Bfunction wraps llvm::Function

class Bfunction {
public:
  Bfunction(llvm::Function *f);
  ~Bfunction();

  llvm::Function *function() const { return function_; }

  enum SplitStackDisposition { YesSplit, NoSplit };
  void setSplitStack(SplitStackDisposition disp) { splitstack_ = disp; }
  SplitStackDisposition splitStack() const { return splitstack_; }

  // Record an alloca() instruction, to be added to entry block
  void addAlloca(llvm::Instruction *inst) { allocas_.push_back(inst); }

  // Record a new Bblock for this function (do we need this?)
  void addBlock(Bblock *block) { blocks_.push_back(block); }

  // Collect Return nth argument
  llvm::Argument *getNthArg(unsigned argIdx);

  // Record a new function argument (for which we'll also add an alloca)
  llvm::Instruction *argValue(llvm::Argument *);

  // Number of parameter vars registered so far
  unsigned paramsCreated() { return argtoval_.size(); }

  // Create a new label
  Blabel *newLabel();

  // Create a new label definition statement
  Bstatement *newLabelDefStatement(Blabel *label);

  // Create a new goto statement
  Bstatement *newGotoStatement(Blabel *label, Location location);

  // Create code to spill function arguments to entry block, insert
  // allocas for local variables.
  void genProlog(llvm::BasicBlock *entry);

private:
  std::vector<llvm::Instruction *> allocas_;
  std::vector<llvm::Argument *> arguments_;
  std::vector<Bblock *> blocks_;
  std::unordered_map<llvm::Argument *, llvm::Instruction *> argtoval_;
  std::vector<Bstatement *> labelmap_;
  std::vector<Blabel *> labels_;
  llvm::Function *function_;
  unsigned labelcount_;
  SplitStackDisposition splitstack_;
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

  Bexpression *conditional_expression(Btype *, Bexpression *, Bexpression *,
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

  Bstatement *init_statement(Bvariable *var, Bexpression *init);

  Bstatement *assignment_statement(Bexpression *lhs, Bexpression *rhs,
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

  // Exposed for unit testing

  // Helpers to check tree integrity. Checks to make sure that
  // we don't have instructions that are parented by more than
  // one Bexpression or stmt. Returns <TRUE,""> if tree is ok,
  // otherwise returns <FALSE,descriptive_message>.
  std::pair<bool, std::string>
  check_tree_integrity(Bexpression *e, bool includePointers = true);
  std::pair<bool, std::string>
  check_tree_integrity(Bstatement *s,  bool includePointers = true);

  // Similar to the above, but prints message to std::cerr and aborts if fail
  void enforce_tree_integrity(Bexpression *e);
  void enforce_tree_integrity(Bstatement *s);

  // Needed for unit testing integrity checks-- cleans duplicates
  // from the internal expressions list.  Not for general-purpose use.
  void detachBexpression(Bexpression *victm);

  // Return true if this is a module-scope value such as a constant
  bool moduleScopeValue(llvm::Value *val, Btype *btype) const;

  // For debugging
  void setTraceLevel(unsigned level) { traceLevel_ = level; }
  unsigned traceLevel() const { return traceLevel_; }

  // For creating useful inst and block names
  std::string namegen(const std::string &tag, unsigned expl = 0xffffffff);

private:
  // Make an anonymous Btype from an llvm::Type
  Btype *makeAnonType(llvm::Type *lt);

  // Create a Btype from an llvm::Type, recording the fact that this
  // is a placeholder type.
  Btype *makePlaceholderType(llvm::Type *placeholder);

  // Replace the underlying llvm::Type for a given placeholder type once
  // we've determined what the final type will be.
  void updatePlaceholderUnderlyingType(Btype *plt, llvm::Type *newtyp);

  // Create an opaque type for use as part of a placeholder type.
  llvm::Type *makeOpaqueLlvmType();

  // Returns field type from struct type and index
  Btype *fieldTypeByIndex(Btype *type, unsigned field_index);

  // Returns array element type from array type
  Btype *elementType(Btype *artype);

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

  // Make a module-scope variable (static, global, or external).
  Bvariable *makeModuleVar(Btype *btype,
                           const std::string &name,
                           const std::string &asm_name,
                           Location location,
                           bool isConstant,
                           bool inUniqueSection,
                           bool inComdat,
                           bool isHiddenVisibility,
                           llvm::GlobalValue::LinkageTypes linkage,
                           unsigned alignmentInBytes = 0);

  // Create a Bexpression to hold a value being computed by the
  // instruction "inst".
  Bexpression *makeInstExpression(llvm::Instruction *inst, Btype *btype);

  // Combing the contents of a list of src expressions to produce
  // a new expression.
  Bexpression *makeExpression(llvm::Instruction *value,
                              Btype *btype,
                              Bexpression *src, ...);

  // Constant array creation helper
  Bexpression *makeConstArrayExpr(Btype *array_btype,
                                  llvm::ArrayType *llat,
                                  const std::vector<unsigned long> &indexes,
                                  const std::vector<Bexpression *> &vals,
                                  Location location);

  // Non-constant array value creation helper
  Bexpression *makeDelayedArrayExpr(Btype *array_btype, llvm::ArrayType *llat,
                                    const std::vector<unsigned long> &indexes,
                                    const std::vector<Bexpression *> &vals,
                                    Location location);

  // Field GEP helper
  llvm::Instruction *makeFieldGEP(llvm::StructType *llst,
                                  unsigned fieldIndex,
                                  llvm::Value *sptr);

  // Array indexing GEP helper
  llvm::Instruction *makeArrayIndexGEP(llvm::ArrayType *art,
                                       unsigned elemIndex,
                                       llvm::Value *sptr);

  // Assignment helper
  Bstatement *makeAssignment(llvm::Value *lvalue, Bexpression *lhs,
                             Bexpression *rhs, Location);

  // Helper to set up entry block for function
  llvm::BasicBlock *genEntryBlock(Bfunction *bfunction);

  // Var expr management
  Bexpression *resolveVarContext(Bexpression *expr);
  Bexpression *loadFromExpr(Bexpression *expr, Btype *loadResultType);

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

  // Data structures to record types that are being manfactured.

  // Anonymous typed are hashed and commoned via this map, except for
  // integer types (stored in a separate map below).
  std::unordered_map<llvm::Type *, Btype *> anonTypemap_;

  // Within the LLVM world there is no notion of an unsigned (vs
  // signed) type, there are only signed/unsigned operations on
  // vanilla integer types.  This table provides for commoning/caching
  // of integer types declared as signed/unsigned by the front end.
  integer_type_maptyp integerTypemap_;

  // Named types are commoned/cached using this table (since LLVM types
  // hemselves have no names).
  named_type_maptyp namedTypemap_;

  // Maps <structType,index> => <fieldType>
  fieldmaptype fieldmap_;

  // Maps array type to element type
  std::unordered_map<Btype *, Btype *> arrayElemTypeMap_;

  // Placeholder types created by the front end.
  std::unordered_set<Btype *> placeholders_;
  std::unordered_set<Btype *> updatedPlaceholders_;

  // Various predefined or pre-computed types that we cache away
  Btype *complexFloatType_;
  Btype *complexDoubleType_;
  Btype *errorType_;
  llvm::Type *llvmVoidType_;
  llvm::Type *llvmPtrType_;
  llvm::Type *llvmSizeType_;
  llvm::Type *llvmIntegerType_;
  llvm::Type *llvmInt8Type_;
  llvm::Type *llvmInt32Type_;
  llvm::Type *llvmInt64Type_;
  llvm::Type *llvmFloatType_;
  llvm::Type *llvmDoubleType_;
  llvm::Type *llvmLongDoubleType_;

  // Lifetime start intrinsic function (created lazily)
  // llvm::Function *llvmLifetimeStart_;

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
  std::unordered_map<llvm::Value *, Bvariable *> valueVarmap_;

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
