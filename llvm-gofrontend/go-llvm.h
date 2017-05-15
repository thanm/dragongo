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
#include "go-llvm-linemap.h"
#include "go-location.h"

// Definitions of Btype, Bexpression, and related B* classes.
#include "go-llvm-btype.h"
#include "go-llvm-bexpression.h"
#include "go-llvm-bstatement.h"
#include "go-llvm-bfunction.h"
#include "go-llvm-bvariable.h"
#include "go-llvm-tree-integrity.h"
#include "go-llvm-typemanager.h"

#include "namegen.h"

#include "backend.h"

#include <unordered_map>
#include <unordered_set>

namespace llvm {
class Argument;
class ArrayType;
class BasicBlock;
class CallInst;
class Constant;
class ConstantFolder;
class DataLayout;
class DICompileUnit;
class DIFile;
class DIScope;
class DIBuilder;
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

class BuiltinTable;
class BlockLIRBuilder;
class BinstructionsLIRBuilder;
struct GenCallState;

#include "llvm/IR/GlobalValue.h"

//
// LLVM-specific implementation of the Backend class; the code in
// gofrontend instantiates an object of this class and then invokes
// the various methods to convert its IR into LLVM IR. Nearly all of
// the interesting methods below are virtual.
//

class Llvm_backend : public Backend, public TypeManager, public NameGen {
public:
  Llvm_backend(llvm::LLVMContext &context,
               llvm::Module *module,
               Llvm_linemap *linemap);
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

  Bexpression *call_expression(Bfunction *caller,
                               Bexpression *fn,
                               const std::vector<Bexpression *> &args,
                               Bexpression *static_chain,
                               Location);

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

  void write_export_data(const char *bytes, unsigned int size);

  Llvm_linemap *linemap() const { return linemap_; }

  // Module and datalayout
  llvm::Module &module() { return *module_; }
  const llvm::DataLayout &datalayout() { return *datalayout_; }

  // Type manager functionality
  TypeManager *typeManager() const;

  // DI builder
  llvm::DIBuilder &dibuilder() { return *dibuilder_.get(); }

  // Bnode builder
  BnodeBuilder &nodeBuilder() { return nbuilder_; }

  // Return top-level debug meta data object for module
  llvm::DICompileUnit *getDICompUnit();

  // Finalize export data for the module. Exposed for unit testing.
  void finalizeExportData();

  // Run the module verifier.
  void verifyModule();

  // Dump LLVM IR for module
  void dumpModule();

  // Dump expression or stmt with line information. For debugging purposes.
  void dumpExpr(Bexpression *);
  void dumpStmt(Bstatement *);
  void dumpVar(Bvariable *);

  // Exposed for unit testing

  // Helpers to check tree integrity. Checks to make sure that we
  // don't have instructions that are parented by more than one
  // Bexpression or Bstatement, or Bexpressions parented by more than
  // one expr/stmt. Returns <TRUE,""> if tree is ok, otherwise returns
  // <FALSE,descriptive_message>.
  std::pair<bool, std::string>
  checkTreeIntegrity(Bnode *n, TreeIntegCtl control);

  // Similar to the above, but prints message to std::cerr and aborts if fail
  void enforceTreeIntegrity(Bnode *n);

  // Disable tree integrity checking. This is mainly
  // so that we can unit test the integrity checker.
  void disableIntegrityChecks() { checkIntegrity_ = false; }

  // Disable debug meta-data generation. Should be used only during
  // unit testing, where we're manufacturing IR that might not verify
  // if meta-data is created.
  void disableDebugMetaDataGeneration() { createDebugMetaData_ = false; }

  // Return true if this is a module-scope value such as a constant
  bool moduleScopeValue(llvm::Value *val, Btype *btype) const;

  // For debugging
  void setTraceLevel(unsigned level);
  unsigned traceLevel() const { return traceLevel_; }

  // Personality function
  llvm::Function *personalityFunction();

 private:
  Bexpression *errorExpression() const { return errorExpression_; }
  Bstatement *errorStatement() const { return errorStatement_; }

  // create a Bfunction for a predefined builtin function with specified name
  Bfunction *defineBuiltinFcn(const std::string &name,
                              llvm::Function *fcn);

  // Certain Bexpressions we want to cache (constants for example,
  // or lvalue references to global variables). This helper looks up
  // the specified expr in a table keyed by <llvm::Value,Btype>. If
  // the lookup succeeds, the cached value is returned, otherwise the
  // specified Bexpression is installed in the table and returned.
  Bexpression *makeGlobalExpression(Bexpression *expr,
                                   llvm::Value *val,
                                   Btype *btype,
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
  llvm::Value *makeArrayIndexGEP(llvm::ArrayType *at,
                                 llvm::Value *idx,
                                 llvm::Value *sptr);

  // Pointer indexing GEP helper
  llvm::Value *makePointerOffsetGEP(llvm::PointerType *pt,
                                    llvm::Value *idx,
                                    llvm::Value *sptr);

  // Assignment helper
  Bstatement *makeAssignment(Bfunction *function, llvm::Value *lvalue,
                             Bexpression *lhs, Bexpression *rhs, Location);

  // Helper to set up entry block for function
  llvm::BasicBlock *genEntryBlock(Bfunction *bfunction);

  // Helper to fix up epilog block for function (add return if needed)
  void fixupEpilogBlock(Bfunction *bfunction, llvm::BasicBlock *epilog);

  // Load-generation helper
  Bexpression *loadFromExpr(Bexpression *space,
                            Btype *resultTyp,
                            Location loc,
                            const std::string &tag);

  // Store generation helper. Creates store or memcpy call.
  Bexpression *genStore(Bfunction *func,
                        Bexpression *srcExpr,
                        Bexpression *dstExpr,
                        Location location);


  // Lower-level version of the above
  llvm::Value *genStore(BlockLIRBuilder *builder,
                        Btype *srcType,
                        llvm::Type *dstType,
                        llvm::Value *srcValue,
                        llvm::Value *dstLoc);

  // Materialize a composite constant into a variable
  Bvariable *genVarForConstant(llvm::Constant *conval, Btype *type);

  // Examine vector of values to test whether they are constants.
  // Checks for and handles pending composite inits.
  static bool
  valuesAreConstant(const std::vector<Bexpression *> &vals);

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

  // Composite init management
  Bexpression *resolveCompositeInit(Bexpression *expr,
                                    Bfunction *func,
                                    llvm::Value *storage);
  // Var expr management
  Bexpression *resolveVarContext(Bexpression *expr,
                                 Varexpr_context ctx=VE_rvalue);

  // General-purpose resolver, handles var expr context and
  // composite init context.
  Bexpression *resolve(Bexpression *expr, Bfunction *func,
                       Varexpr_context ctx=VE_rvalue);

  // Check a vector of Bexpression's to see if any are the
  // error expression, returning TRUE if so.
  bool exprVectorHasError(const std::vector<Bexpression *> &vals);

  // Check a vector of Bstatement's to see if any are the
  // error statement, returning TRUE if so.
  bool stmtVectorHasError(const std::vector<Bstatement *> &stmts);

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
  llvm::Value *convertForAssignment(Bexpression *src,
                                    llvm::Type *dstToType,
                                    Bfunction *bfunc);
  // lower-level version of the routine above
  llvm::Value *convertForAssignment(Btype *srcType,
                                    llvm::Value *srcVal,
                                    llvm::Type *dstToType,
                                    BlockLIRBuilder *builder);


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

  // Helpers for call sequence generation.
  void genCallProlog(GenCallState &state);
  void genCallAttributes(GenCallState &state, llvm::CallInst *call);
  void genCallMarshallArgs(const std::vector<Bexpression *> &fn_args,
                           GenCallState &state);
  void genCallEpilog(GenCallState &state, llvm::Instruction *callInst,
                     Bexpression *callExpr);

  // Store the value in question to a temporary, returning the alloca
  // for the temp.
  llvm::Instruction *storeToTemporary(Bfunction *func, llvm::Value *val);

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

  typedef std::pair<llvm::Type *, bool> type_plus_unsigned;
  typedef pairvalmap<llvm::Type *, bool, Btype *> integer_type_maptyp;

  typedef std::pair<llvm::Value *, Btype *> valbtype;
  typedef pairvalmap<llvm::Value *, Btype *, Bexpression *>
  btyped_value_expr_maptyp;

  // Context information needed for the LLVM backend.
  llvm::LLVMContext &context_;

  // The LLVM module we're emitting IR into. If client did not supply
  // a module during construction, then ownModule_ is filled in.
  llvm::Module *module_;
  std::unique_ptr<llvm::Module> ownModule_;

  // Data layout info from the module.
  const llvm::DataLayout *datalayout_;

  // Builder for constructing Bexpressions and Bstatements.
  BnodeBuilder nbuilder_;

  // Debug info builder
  std::unique_ptr<llvm::DIBuilder> dibuilder_;

  // Root debug meta-data scope for compilation unit
  llvm::DICompileUnit *diCompileUnit_;

  // Linemap to use. If client did not supply a linemap during
  // construction, then ownLinemap_ is filled in.
  Llvm_linemap *linemap_;
  std::unique_ptr<Llvm_linemap> ownLinemap_;

  // Address space designator for pointer types.
  unsigned addressSpace_;

  // Debug trace level
  unsigned traceLevel_;

  // Whether to check for unexpected node sharing (e.g. same Bexpression
  // or statement pointed to by multiple parents).
  bool checkIntegrity_;

  // Whether to create debug meta data. On by default, can be
  // disabled for unit testing.
  bool createDebugMetaData_;

  // Whether we've started / finalized export data for the module.
  bool exportDataStarted_;
  bool exportDataFinalized_;

  // This counter gets incremented when the FE requests an error
  // object (error variable, error type, etc). We check to see whether
  // it is non-zero before walking function bodies to emit debug
  // meta-data (the idea being that there is no point going through
  // that process if there were errors).
  unsigned errorCount_;

  // Target library info oracle
  llvm::TargetLibraryInfo *TLI_;

  // maps name to entry storing info on builtin function
  std::unique_ptr<BuiltinTable> builtinTable_;

  // Error function
  std::unique_ptr<Bfunction> errorFunction_;

  // Error expression
  Bexpression *errorExpression_;

  // Error statement
  Bstatement *errorStatement_;

  // Error variable
  std::unique_ptr<Bvariable> errorVariable_;

  // Map from LLVM-value/Btype pair to Bexpression. This is
  // used to cache + reuse things like global constants.
  btyped_value_expr_maptyp valueExprmap_;

  // Map from LLVM values to Bvariable. This is used for
  // module-scope variables, not vars local to a function.
  std::unordered_map<llvm::Value *, Bvariable *> valueVarMap_;

  // Table for commoning strings by value. String constants have
  // concrete types like "[5 x i8]", whereas we would like to return
  // things that have type "i8*". To manage this, we eagerly create
  // module-scope vars with strings, but this tends to defeat the
  // caching mechanisms, so here we have a map from constant string
  // value to Bexpression holding that string const.
  std::unordered_map<llvm::Value *, Bexpression*> stringConstantMap_;

  // For caching of immutable struct references. Similar situation here
  // as above, in that we can't look for such things in valueVarMap_
  // without creating what it is we're looking for.
  std::unordered_map<std::string, Bvariable *> immutableStructRefs_;

  // A map from function asm name to Bfunction, used to cache declarations
  // of external functions (for example, well-known functions in the
  // runtime). Only declarations will be placed in this map-- if a function
  // is being defined, it will only be added to the functions_ list below.
  std::unordered_map<std::string, Bfunction*> fcnNameMap_;

  // Currently we don't do any commoning of Bfunction objects created
  // by the frontend, so here we keep track of all returned Bfunctions
  // so that we can free them on exit.
  std::vector<Bfunction *> functions_;

  // Personality function
  llvm::Function *personalityFunction_;

  // Pointer to current function being generated. Used for sanity checking,
  // to catch cases where the front end switches between functions in
  // an expected way.
  Bfunction *curFcn_;
};

#endif
