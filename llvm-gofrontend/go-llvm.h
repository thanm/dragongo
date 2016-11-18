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
#include "go-location.h"
#include "go-linemap.h"

#include "backend.h"

#include <unordered_map>
#include <unordered_set>

namespace llvm {
class Type;
class TargetLibraryInfo;
}

// Btype wraps llvm::Type

class Btype {
public:
  explicit Btype(llvm::Type *type) : type_(type) {}

  llvm::Type *type() const { return type_; }

private:
  Btype() : type_(NULL) {}
  llvm::Type *type_;
  friend class Llvm_backend;
};

// Bexpression wraps llvm::Value

class Bexpression {
public:
  explicit Bexpression(llvm::Value *value) : value_(value) {}

  llvm::Value *value() const { return value_; }

private:
  Bexpression() : value_(NULL) {}
  llvm::Value *value_;
  friend class Llvm_backend;
};

// Class Bfunction wraps llvm::Function

class Bfunction
{
 public:
  Bfunction(llvm::Function *f);
  ~Bfunction();

  llvm::Function *function() const { return function_; }

  enum SplitStackDisposition { YesSplit, NoSplit };
  void setSplitStack(SplitStackDisposition disp) { splitstack_ = disp; }
  SplitStackDisposition splitStack() const { return splitstack_; }

  // Record an alloca() instruction, to be added to entry block
  void addAlloca(llvm::Instruction *inst) {
    allocas_.push_back(inst);
  }

  // Collect Return nth argument
  llvm::Argument *getNthArg(unsigned argIdx);

  // Record a new function argument (for which we'll also add an alloca)
  llvm::Instruction *argValue(llvm::Argument *);

  // Number of parameter vars registered so far
  unsigned paramsCreated() { return argtoval_.size(); }

 private:
  std::vector<llvm::Instruction*> allocas_;
  std::vector<llvm::Argument *> arguments_;
  std::unordered_map<llvm::Argument*, llvm::Instruction*> argtoval_;
  llvm::Function *function_;
  SplitStackDisposition splitstack_;
};

// Back end variable class

enum WhichVar { ParamVar, GlobalVar, LocalVar, ErrorVar };

class Bvariable {
 public:
  explicit Bvariable(Btype *type, Location location, const std::string &name,
                     WhichVar which, bool address_taken, llvm::Value *value)
      : name_(name)
      , location_(location)
      , value_(value)
      , type_(type)
      , which_(which)
      , addrtaken_(address_taken)
  {
  }

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

  Btype *error_type() { return error_type_; }

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

  Bexpression *var_expression(Bvariable *var, Location);

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

  Bstatement *expression_statement(Bexpression *);

  Bstatement *init_statement(Bvariable *var, Bexpression *init);

  Bstatement *assignment_statement(Bexpression *lhs, Bexpression *rhs,
                                   Location);

  Bstatement *return_statement(Bfunction *, const std::vector<Bexpression *> &,
                               Location);

  Bstatement *if_statement(Bexpression *condition, Bblock *then_block,
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
                             const std::string &asm_name,
                             Btype *btype, bool is_external, bool is_hidden,
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

  Bvariable *immutable_struct(const std::string &, const std::string &,
                              bool, bool, Btype *, Location);

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

private:
  // Make an anonymous Btype from an llvm::Type
  Btype *make_anon_type(llvm::Type *lt);

  // Create a Btype from an llvm::Type, recording the fact that this
  // is a placeholder type.
  Btype *make_placeholder_type(llvm::Type *placeholder);

  // Replace the underlying llvm::Type for a given placeholder type once
  // we've determined what the final type will be.
  void update_placeholder_underlying_type(Btype *plt, llvm::Type *newtyp);

  // Create an opaque type for use as part of a placeholder type.
  llvm::Type *make_opaque_llvm_type();

  // Did gofrontend declare this as an unsigned integer type?
  bool is_unsigned_integer_type(Btype *t) const {
    return unsigned_integer_types_.find(t) != unsigned_integer_types_.end();
  }

  // add a builtin function definition
  void define_builtin_fcn(const char* name, const char* libname,
                          llvm::Function *fcn);

  // varargs convenience wrapper for define_builtin_fcn.
  // creates a libcall builtin. If the builtin being created is defined
  // in llvm/Analysis/TargetLibraryInfo.def, then the proper enumeration
  // should be passed in "libfuncID" (otherwise pass NumLibFuncs).
  // varargs: first arg after libfuncID is return type, following
  // arguments are parameter types, followed by NULL type indicating
  // end of params.
  void define_libcall_builtin(const char* name, const char* libname,
                              unsigned libfuncID, ...);

  // similar to the routine above, but takes a vector of
  // types as opposed to an argument list.
  void define_libcall_builtin(const char* name, const char* libname,
                              const std::vector<llvm::Type *> &types,
                              unsigned libfuncID);

  // varargs convenience wrapper for define_builtin_fcn;
  // creates in intrinsic builtin by looking up intrinsic
  // 'intrinsicID'; variable arguments the overloaded types required
  // by llvm::Intrinsic::getDeclaration (see the comments on that
  // function for more info) terminated by NULL.
  void define_intrinsic_builtin(const char* name, const char* libname,
                                unsigned intrinsicID, ...);

  // more helpers for builtin creation
  void define_all_builtins();
  void define_sync_fetch_and_add_builtins();
  void define_intrinsic_builtins();
  void define_trig_builtins();

  // Create a Bexpression to hold an llvm::Value for a constant or instruction
  Bexpression *make_value_expression(llvm::Value *val);

private:
  typedef std::pair<const std::string, llvm::Type *> named_llvm_type;

  class named_llvm_type_hash {
   public:
    unsigned int
    operator()(const named_llvm_type& nt) const
    {
      std::size_t h1 = std::hash<std::string>{}(nt.first);
      // just hash pointer value
      const void *vptr = static_cast<void*>(nt.second);
      std::size_t h2 = std::hash<const void*>{}(vptr);
      return h1 + h2;
    }
  };

  class named_llvm_type_equal {
   public:
    bool
    operator()(const named_llvm_type& nt1, const named_llvm_type& nt2) const
    {
      return (nt1.first == nt2.first &&
              nt1.second == nt2.second);
    }
  };
  typedef std::unordered_map<named_llvm_type, Btype *,
                             named_llvm_type_hash,
                             named_llvm_type_equal > named_type_maptyp;

  // Context information needed for the LLVM backend.
  llvm::LLVMContext &context_;
  std::unique_ptr<llvm::Module> module_;
  const llvm::DataLayout &datalayout_;
  unsigned address_space_;

  // Data structures to record types that are being manfactured.

  // Anonymous typed are hashed and commoned via this map.
  std::unordered_map<llvm::Type *, Btype *> anon_typemap_;

  // LLVM types have no notion of names, hence this map is used
  // to keep track of named types (for symbol and debug info emit).
  named_type_maptyp named_typemap_;

  // Within the LLVM world there is no notion of an unsigned (vs signed)
  // type, there are only signed/unsigned operations on vanilla integer
  // types. This set keeps track of types that the frontend has told
  // us are unsigned; see Llvm_backend::integer_type for more.
  std::unordered_set<Btype *> unsigned_integer_types_;

  // Placeholder types
  std::unordered_set<Btype *> placeholders_;
  std::unordered_set<Btype *> updated_placeholders_;

  // Various predefined or pre-computed types that we cache away
  Btype *complex_float_type_;
  Btype *complex_double_type_;
  Btype *error_type_;
  llvm::Type *llvm_void_type_;
  llvm::Type *llvm_ptr_type_;
  llvm::Type *llvm_size_type_;
  llvm::Type *llvm_integer_type_;
  llvm::Type *llvm_int32_type_;
  llvm::Type *llvm_int64_type_;
  llvm::Type *llvm_float_type_;
  llvm::Type *llvm_double_type_;
  llvm::Type *llvm_long_double_type_;

  // Target library info oracle
  llvm::TargetLibraryInfo *TLI_;

  // maps name to builtin function
  std::unordered_map<std::string, Bfunction *> builtin_map_;

  // Error function
  std::unique_ptr<Bfunction> error_function_;

  // Error expression
  std::unique_ptr<Bexpression> error_expression_;

  // Map from LLVM values to Bexpression.
  std::unordered_map<llvm::Value *, Bexpression *> value_exprmap_;

  // Map from LLVM values to Bvariable.
  std::unordered_map<llvm::Value *, Bvariable *> value_varmap_;

  // Error variable
  std::unique_ptr<Bvariable> error_variable_;

  // Currently we don't do any commoning of Bfunction objects created
  // by the frontend, so here we keep track of all returned Bfunctions
  // so that we can free them on exit.
  std::vector<Bfunction *> functions_;
};

#endif
