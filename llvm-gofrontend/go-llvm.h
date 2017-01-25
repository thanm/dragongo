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
#include "go-llvm-btype.h"
#include "go-llvm-bexpression.h"
#include "go-llvm-bstatement.h"
#include "go-llvm-bfunction.h"
#include "go-llvm-bvariable.h"
#include "go-llvm-tree-integrity.h"

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


//
// LLVM-specific implementation of the Backend class; the code in
// gofrontend instantiates an object of this class and then invokes
// the various methods to convert its IR into LLVM IR. Nearly all of
// the interesting methods below are virtual.
//

class Llvm_backend : public Backend {
public:
  Llvm_backend(llvm::LLVMContext &context, Linemap *linemap);
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

  Linemap *linemap() const { return linemap_; }

  // Exposed for unit testing
  llvm::Module &module() { return *module_.get(); }

  // Run the module verifier.
  void verifyModule();

  // Dump LLVM IR for module
  void dumpModule();

  // Dump expression or stmt with line information. For debugging purposes.
  void dumpExpr(Bexpression *);
  void dumpStmt(Bstatement *);
  void dumpVar(Bvariable *);

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

  // Tells namegen to choose its own version number for the created name
  static constexpr unsigned ChooseVer = 0xffffffff;

  // For creating useful inst and block names.
  std::string namegen(const std::string &tag, unsigned expl = ChooseVer);

 private:

  enum PTDisp { Concrete, Placeholder };

  // Create a new anonymous Btype based on LLVM type 'lt'. This is used
  // for types where there is a direct corresponding between the LLVM type
  // and the frontend type (ex: float32), and where we don't need to
  // do any later post-processing or checking.
  Btype *makeAuxType(llvm::Type *lt);

  // Create a new anonymous BFunctionType based on a corresponding
  // LLVM function type. This is slightly different from the routine
  // above in that it replicates information about parameter types and
  // results and creates a BFunctionType based on them (whereas
  // everything created by makeAuxTpe is an AuxT container). Not for
  // general use (should be used only for things like builtins), since
  // there is no way to express things like signed/unsigned param types.
  BFunctionType *makeAuxFcnType(llvm::FunctionType *eft);

  // Is this a placeholder type?
  bool isPlaceholderType(Btype *t);

  // Is this a Go boolean type
  bool isBooleanType(Btype *);

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
                                   ValExprScope scope,
                                   Location location);

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

  // Helper to fold contents of one instruction into another.
  static void
  incorporateExpression(Bexpression *dst,
                        Bexpression *src,
                        std::set<llvm::Instruction *> *visited);

  enum MkExprAction { AppendInst, DontAppend };

  // Create a new Bexpression with the specified LLMV value and Btype,
  // based on the contents of a list of source expressions (list terminated
  // with a null pointer). If 'action' is set to AppendInst, then the
  // specified value is an instruction that should be appended to the
  // result Bexpression; if 'action' is DontAppend then the value
  // is not an inst that should be appended.
  Bexpression *makeExpression(llvm::Value *value,
                              MkExprAction action,
                              Btype *btype,
                              Location location,
                              Bexpression *src, ...);

  // Similar to the method above, but array-based and not varargs.
  Bexpression *makeExpression(llvm::Value *value,
                              MkExprAction action,
                              Btype *btype,
                              Location location,
                              const std::vector<Bexpression *> &srcs);

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
                            llvm::Value *storage,
                            Bfunction *bfunc);

  // Struct init helper
  Bexpression *genStructInit(llvm::StructType *llst,
                             Bexpression *expr,
                             llvm::Value *storage,
                             Bfunction *bfunc);

  // General-purpose resolver, handles var expr context and
  // composite init context.
  Bexpression *resolve(Bexpression *expr, Bfunction *func);

  // Check a vector of Bexpression's to see if any are the
  // error expression, returning TRUE if so.
  bool exprVectorHasError(const std::vector<Bexpression *> &vals);

  // Check a vector of Bstatement's to see if any are the
  // error statement, returning TRUE if so.
  bool stmtVectorHasError(const std::vector<Bstatement *> &stmts);

  // Type helpers
  bool isFuncDescriptorType(llvm::Type *typ);
  bool isPtrToFuncDescriptorType(llvm::Type *typ);
  bool isPtrToFuncType(llvm::Type *typ);

  // Converts value "src" for assignment to container of type
  // "dstType" in assignment-like contexts. This helper exists to help
  // with cases where the frontend is creating an assignment of form
  // "X = Y" where X and Y's types are considered matching by the
  // front end, but are non-matching in an LLVM context. For example,
  //
  //   type Ifi func(int) int
  //   ...
  //   var fp Ifi = myfunctionfoobar
  //
  // Here the right hand side will come out as pointer-to-descriptor,
  // whereas variable "fp" will have type "pointer to functon", which are
  // not the same. Another example is assignments involving nil, e.g.
  //
  //   var ip *float32
  //   ...
  //   ip = nil
  //
  // The type of the right hand side of the assignment will be a
  // generic "*i64" as opposed to "*float32", since the backend
  // "nil_pointer_expression" does not allow for creation of nil
  // pointers of specific types.
  //
  // Return value will be a new convert Bexpression if a convert is
  // needed, NULL otherwise.
  llvm::Value *convertForAssignment(Bexpression *src, llvm::Type *dstToType);

  // Apply type conversion for a binary operation. This helper exists
  // to resolve situations where expressions are created by the front
  // end have incomplete or "polymorphic" type. A good example is pointer
  // comparison with nil, e.g.
  //
  //    var ip *A = foobar()
  //    if ip == nil { ...
  //
  // The type of the right hand side of the '==' above will be a generic
  // "*i64" as opposed to "*A", since the backend "nil_pointer_expression"
  // does not allow for creation of nil pointers of specific types.
  //
  // Input expressions 'left' and 'right' correspond to the original
  // uncoerced expressions; if conversions are needed, any additional
  // instructions will be added to the args and the resulting LLVM
  // values will be returned.
  std::pair<llvm::Value *, llvm::Value *>
  convertForBinary(Bexpression *left, Bexpression *right);

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
  Linemap *linemap_;
  std::unique_ptr<Linemap> ownLinemap_;
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

  // Map from LLVM values to Bvariable. This is used for
  // module-scope variables, not vars local to a function.
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
