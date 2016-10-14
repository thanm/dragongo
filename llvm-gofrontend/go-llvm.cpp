//===-- go-llvm.cpp - LLVM implementation of 'Backend'  -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for class Llvm_backend, a subclass of the gofrontend class
// Backend, with LLVM-specific implementation methods.
//
//===----------------------------------------------------------------------===//

#include "go-system.h"
#include "go-c.h"
#include "gogo.h"
#include "backend.h"
#include "go-llvm.h"

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

// Return whether the character c is OK to use in the assembler.

#if 0

// TODO: move to common location
static bool char_needs_encoding(char c) {
  switch (c) {
  case 'A':
  case 'B':
  case 'C':
  case 'D':
  case 'E':
  case 'F':
  case 'G':
  case 'H':
  case 'I':
  case 'J':
  case 'K':
  case 'L':
  case 'M':
  case 'N':
  case 'O':
  case 'P':
  case 'Q':
  case 'R':
  case 'S':
  case 'T':
  case 'U':
  case 'V':
  case 'W':
  case 'X':
  case 'Y':
  case 'Z':
  case 'a':
  case 'b':
  case 'c':
  case 'd':
  case 'e':
  case 'f':
  case 'g':
  case 'h':
  case 'i':
  case 'j':
  case 'k':
  case 'l':
  case 'm':
  case 'n':
  case 'o':
  case 'p':
  case 'q':
  case 'r':
  case 's':
  case 't':
  case 'u':
  case 'v':
  case 'w':
  case 'x':
  case 'y':
  case 'z':
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
  case '_':
  case '.':
  case '$':
  case '/':
    return false;
  default:
    return true;
  }
}

// Return whether the identifier needs to be translated because it
// contains non-ASCII characters.

// TODO: move to common location
static bool needs_encoding(const std::string &str) {
  for (std::string::const_iterator p = str.begin(); p != str.end(); ++p)
    if (char_needs_encoding(*p))
      return true;
  return false;
}

// Pull the next UTF-8 character out of P and store it in *PC.  Return
// the number of bytes read.

// TODO: move to common location
static size_t fetch_utf8_char(const char *p, unsigned int *pc) {
  unsigned char c = *p;
  if ((c & 0x80) == 0) {
    *pc = c;
    return 1;
  }
  size_t len = 0;
  while ((c & 0x80) != 0) {
    ++len;
    c <<= 1;
  }
  unsigned int rc = *p & ((1 << (7 - len)) - 1);
  for (size_t i = 1; i < len; i++) {
    unsigned int u = p[i];
    rc <<= 6;
    rc |= u & 0x3f;
  }
  *pc = rc;
  return len;
}

// Encode an identifier using ASCII characters.

// TODO: move to common location
static std::string encode_id(const std::string id) {
  std::string ret;
  const char *p = id.c_str();
  const char *pend = p + id.length();
  while (p < pend) {
    unsigned int c;
    size_t len = fetch_utf8_char(p, &c);
    if (len == 1 && !char_needs_encoding(c))
      ret += c;
    else {
      ret += "$U";
      char buf[30];
      snprintf(buf, sizeof buf, "%x", c);
      ret += buf;
      ret += "$";
    }
    p += len;
  }
  return ret;
}

#endif

// Define the built-in functions that are exposed to GCCGo.

Llvm_backend::Llvm_backend(llvm::LLVMContext &context)
    : context_(context), module_(new llvm::Module("gomodule", context)),
      datalayout_(module_->getDataLayout()), complex_float_type_(nullptr),
      complex_double_type_(nullptr), error_type_(nullptr), llvm_ptr_type_(NULL),
      address_space_(0) {
  // LLVM doesn't have anything that corresponds directly to the
  // gofrontend notion of an error type. For now we create a so-called
  // 'identified' anonymous struct type and have that act as a
  // stand-in. See http://llvm.org/docs/LangRef.html#structure-type
  error_type_ = make_anon_type(llvm::StructType::create(context_));

  // For use handling circular types
  llvm_ptr_type_ =
      llvm::PointerType::get(llvm::StructType::create(context), address_space_);

#if 0
  /* We need to define the fetch_and_add functions, since we use them
     for ++ and --.  */
  tree t = this->integer_type(true, BITS_PER_UNIT)->get_tree();
  tree p = build_pointer_type(build_qualified_type(t, TYPE_QUAL_VOLATILE));
  this->define_builtin(BUILT_IN_SYNC_ADD_AND_FETCH_1, "__sync_fetch_and_add_1",
                       NULL, build_function_type_list(t, p, t, NULL_TREE),
                       false, false);

  t = this->integer_type(true, BITS_PER_UNIT * 2)->get_tree();
  p = build_pointer_type(build_qualified_type(t, TYPE_QUAL_VOLATILE));
  this->define_builtin(BUILT_IN_SYNC_ADD_AND_FETCH_2, "__sync_fetch_and_add_2",
                       NULL, build_function_type_list(t, p, t, NULL_TREE),
                       false, false);

  t = this->integer_type(true, BITS_PER_UNIT * 4)->get_tree();
  p = build_pointer_type(build_qualified_type(t, TYPE_QUAL_VOLATILE));
  this->define_builtin(BUILT_IN_SYNC_ADD_AND_FETCH_4, "__sync_fetch_and_add_4",
                       NULL, build_function_type_list(t, p, t, NULL_TREE),
                       false, false);

  t = this->integer_type(true, BITS_PER_UNIT * 8)->get_tree();
  p = build_pointer_type(build_qualified_type(t, TYPE_QUAL_VOLATILE));
  this->define_builtin(BUILT_IN_SYNC_ADD_AND_FETCH_8, "__sync_fetch_and_add_8",
                       NULL, build_function_type_list(t, p, t, NULL_TREE),
                       false, false);

  // We use __builtin_expect for magic import functions.
  this->define_builtin(BUILT_IN_EXPECT, "__builtin_expect", NULL,
                       build_function_type_list(long_integer_type_node,
                                                long_integer_type_node,
                                                long_integer_type_node,
                                                NULL_TREE),
                       true, false);

  // We use __builtin_memcmp for struct comparisons.
  this->define_builtin(BUILT_IN_MEMCMP, "__builtin_memcmp", "memcmp",
                       build_function_type_list(integer_type_node,
                                                const_ptr_type_node,
                                                const_ptr_type_node,
                                                size_type_node,
                                                NULL_TREE),
                       false, false);

  // Used by runtime/internal/sys.
  this->define_builtin(BUILT_IN_CTZ, "__builtin_ctz", "ctz",
                       build_function_type_list(integer_type_node,
                                                unsigned_type_node,
                                                NULL_TREE),
                       true, false);
  this->define_builtin(BUILT_IN_CTZLL, "__builtin_ctzll", "ctzll",
                       build_function_type_list(integer_type_node,
                                                long_long_unsigned_type_node,
                                                NULL_TREE),
                       true, false);
  this->define_builtin(BUILT_IN_BSWAP32, "__builtin_bswap32", "bswap32",
                       build_function_type_list(uint32_type_node,
                                                uint32_type_node,
                                                NULL_TREE),
                       true, false);
  this->define_builtin(BUILT_IN_BSWAP64, "__builtin_bswap64", "bswap64",
                       build_function_type_list(uint64_type_node,
                                                uint64_type_node,
                                                NULL_TREE),
                       true, false);

  // We provide some functions for the math library.
  tree math_function_type = build_function_type_list(double_type_node,
                                                     double_type_node,
                                                     NULL_TREE);
  tree math_function_type_long =
    build_function_type_list(long_double_type_node, long_double_type_node,
                             long_double_type_node, NULL_TREE);
  tree math_function_type_two = build_function_type_list(double_type_node,
                                                         double_type_node,
                                                         double_type_node,
                                                         NULL_TREE);
  tree math_function_type_long_two =
    build_function_type_list(long_double_type_node, long_double_type_node,
                             long_double_type_node, NULL_TREE);
  this->define_builtin(BUILT_IN_ACOS, "__builtin_acos", "acos",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_ACOSL, "__builtin_acosl", "acosl",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_ASIN, "__builtin_asin", "asin",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_ASINL, "__builtin_asinl", "asinl",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_ATAN, "__builtin_atan", "atan",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_ATANL, "__builtin_atanl", "atanl",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_ATAN2, "__builtin_atan2", "atan2",
                       math_function_type_two, true, false);
  this->define_builtin(BUILT_IN_ATAN2L, "__builtin_atan2l", "atan2l",
                       math_function_type_long_two, true, false);
  this->define_builtin(BUILT_IN_CEIL, "__builtin_ceil", "ceil",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_CEILL, "__builtin_ceill", "ceill",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_COS, "__builtin_cos", "cos",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_COSL, "__builtin_cosl", "cosl",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_EXP, "__builtin_exp", "exp",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_EXPL, "__builtin_expl", "expl",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_EXPM1, "__builtin_expm1", "expm1",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_EXPM1L, "__builtin_expm1l", "expm1l",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_FABS, "__builtin_fabs", "fabs",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_FABSL, "__builtin_fabsl", "fabsl",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_FLOOR, "__builtin_floor", "floor",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_FLOORL, "__builtin_floorl", "floorl",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_FMOD, "__builtin_fmod", "fmod",
                       math_function_type_two, true, false);
  this->define_builtin(BUILT_IN_FMODL, "__builtin_fmodl", "fmodl",
                       math_function_type_long_two, true, false);
  this->define_builtin(BUILT_IN_LDEXP, "__builtin_ldexp", "ldexp",
                       build_function_type_list(double_type_node,
                                                double_type_node,
                                                integer_type_node,
                                                NULL_TREE),
                       true, false);
  this->define_builtin(BUILT_IN_LDEXPL, "__builtin_ldexpl", "ldexpl",
                       build_function_type_list(long_double_type_node,
                                                long_double_type_node,
                                                integer_type_node,
                                                NULL_TREE),
                       true, false);
  this->define_builtin(BUILT_IN_LOG, "__builtin_log", "log",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_LOGL, "__builtin_logl", "logl",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_LOG1P, "__builtin_log1p", "log1p",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_LOG1PL, "__builtin_log1pl", "log1pl",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_LOG10, "__builtin_log10", "log10",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_LOG10L, "__builtin_log10l", "log10l",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_LOG2, "__builtin_log2", "log2",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_LOG2L, "__builtin_log2l", "log2l",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_SIN, "__builtin_sin", "sin",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_SINL, "__builtin_sinl", "sinl",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_SQRT, "__builtin_sqrt", "sqrt",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_SQRTL, "__builtin_sqrtl", "sqrtl",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_TAN, "__builtin_tan", "tan",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_TANL, "__builtin_tanl", "tanl",
                       math_function_type_long, true, false);
  this->define_builtin(BUILT_IN_TRUNC, "__builtin_trunc", "trunc",
                       math_function_type, true, false);
  this->define_builtin(BUILT_IN_TRUNCL, "__builtin_truncl", "truncl",
                       math_function_type_long, true, false);

  // We use __builtin_return_address in the thunk we build for
  // functions which call recover, and for runtime.getcallerpc.
  t = build_function_type_list(ptr_type_node, unsigned_type_node, NULL_TREE);
  this->define_builtin(BUILT_IN_RETURN_ADDRESS, "__builtin_return_address",
                       NULL, t, false, false);

  // The runtime calls __builtin_frame_address for runtime.getcallersp.
  this->define_builtin(BUILT_IN_FRAME_ADDRESS, "__builtin_frame_address",
                       NULL, t, false, false);

  // The compiler uses __builtin_trap for some exception handling
  // cases.
  this->define_builtin(BUILT_IN_TRAP, "__builtin_trap", NULL,
                       build_function_type(void_type_node, void_list_node),
                       false, true);
#endif
}

Llvm_backend::~Llvm_backend()
{
  for (auto pht : placeholders_)
    delete pht;
  for (auto pht : updated_placeholders_)
    delete pht;
  for (auto &kv : anon_typemap_)
    delete kv.second;
  for (auto &kv : named_typemap_)
    delete kv.second;
  for (auto &kv : builtin_functions_)
    delete kv.second;
}

Btype *Llvm_backend::make_anon_type(llvm::Type *lt) {
  assert(lt);

  // unsure whether caching is a net win, but for now cache all
  // previously created types and return the cached result
  // if we ask for the same type twice.
  auto it = anon_typemap_.find(lt);
  if (it != anon_typemap_.end())
    return it->second;
  Btype *rval = new Btype(lt);
  anon_typemap_[lt] = rval;
  return rval;
}

Btype *Llvm_backend::make_placeholder_type(llvm::Type *pht) {
  Btype *bplace = new Btype(pht);
  assert(placeholders_.find(bplace) == placeholders_.end());
  placeholders_.insert(bplace);
  return bplace;
}

void Llvm_backend::update_placeholder_underlying_type(Btype *pht,
                                                      llvm::Type *newtyp) {
  assert(placeholders_.find(pht) != placeholders_.end());
  placeholders_.erase(pht);
  updated_placeholders_.insert(pht);
  pht->type_ = newtyp;
}

Btype *Llvm_backend::void_type()
{
  return make_anon_type(llvm::Type::getVoidTy(context_));
}

Btype *Llvm_backend::bool_type()
{
  // LLVM has no predefined boolean type. Use int8 for now
  return make_anon_type(llvm::Type::getInt1Ty(context_));
}

// Get an unnamed integer type. Note that in the LLVM world, we don't
// have signed/unsigned on types, we only have signed/unsigned on operations
// over types (e.g. signed addition of two integers).

Btype *Llvm_backend::integer_type(bool /*is_unsigned*/, int bits) {
  return make_anon_type(llvm::IntegerType::get(context_, bits));
}

// Get an unnamed float type.

Btype *Llvm_backend::float_type(int bits) {
  if (bits == 32)
    return make_anon_type(llvm::Type::getFloatTy(context_));
  else if (bits == 64)
    return make_anon_type(llvm::Type::getDoubleTy(context_));
  else if (bits == 128)
    return make_anon_type(llvm::Type::getFP128Ty(context_));
  assert(false && "unsupported float width");
  return NULL;
}

// Make a struct type.

// FIXME: llvm::StructType::get has no means of specifying field
// names, meaning that for debug info generation we'll need to
// capture fields here in some way, either by eagerly creating the DI
// (preferrable) or recording the field names for later use (less so)

Btype *Llvm_backend::struct_type(const std::vector<Btyped_identifier> &fields) {
  llvm::SmallVector<llvm::Type *, 64> elems(fields.size());
  for (unsigned i = 0; i < fields.size(); ++i) {
    if (fields[i].btype == error_type())
      return error_type();
    elems[i] = fields[i].btype->type();
  }
  return make_anon_type(llvm::StructType::get(context_, elems));
}

// LLVM has no such thing as a complex type -- it expects the front
// end to lower all complex operations from the get-go, meaning
// that the back end only sees two-element structs.

Btype *Llvm_backend::complex_type(int bits) {
  if (bits == 64 && complex_float_type_)
    return complex_float_type_;
  if (bits == 128 && complex_double_type_)
    return complex_double_type_;
  assert(bits == 64 || bits == 128);
  llvm::Type *elemTy = (bits == 64 ? llvm::Type::getFloatTy(context_)
                                   : llvm::Type::getDoubleTy(context_));
  llvm::SmallVector<llvm::Type *, 2> elems(2);
  elems[0] = elemTy;
  elems[1] = elemTy;
  Btype *rval = make_anon_type(llvm::StructType::get(context_, elems));
  if (bits == 64)
    complex_float_type_ = rval;
  else
    complex_double_type_ = rval;
  return rval;
}

// Get a pointer type.

Btype *Llvm_backend::pointer_type(Btype *to_type) {
  if (to_type == error_type_)
    return error_type_;
  return make_anon_type(
      llvm::PointerType::get(to_type->type(), address_space_));
}

// Make a function type.

Btype *
Llvm_backend::function_type(const Btyped_identifier &receiver,
                            const std::vector<Btyped_identifier> &parameters,
                            const std::vector<Btyped_identifier> &results,
                            Btype *result_struct, Location) {
  llvm::SmallVector<llvm::Type *, 4> elems(0);

  // Receiver type if applicable
  if (receiver.btype != NULL) {
    if (receiver.btype == error_type())
      return error_type();
    elems.push_back(receiver.btype->type());
  }

  // Argument types
  for (std::vector<Btyped_identifier>::const_iterator p = parameters.begin();
       p != parameters.end(); ++p) {
    if (p->btype == error_type())
      return error_type();
    elems.push_back(p->btype->type());
  }

  // Result types
  llvm::Type *rtyp = NULL;
  if (results.empty())
    rtyp = llvm::Type::getVoidTy(context_);
  else if (results.size() == 1) {
    if (results.front().btype == error_type())
      return error_type();
    rtyp = results.front().btype->type();
  } else {
    assert(result_struct != NULL);
    rtyp = result_struct->type();
  }
  assert(rtyp != NULL);

  // https://gcc.gnu.org/PR72814 handling. From the go-gcc.cc
  // equivalent, here is an explanatory comment:
  //
  // The libffi library can not represent a zero-sized object.  To
  // avoid causing confusion on 32-bit SPARC, we treat a function that
  // returns a zero-sized value as returning void.  That should do no
  // harm since there is no actual value to be returned.
  //
  if (rtyp->isSized() && datalayout_.getTypeSizeInBits(rtyp) == 0)
    rtyp = llvm::Type::getVoidTy(context_);

  // from LLVM's perspective, no functions have varargs (all that
  // is dealt with by the front end).
  const bool isVarargs = false;
  return make_anon_type(llvm::FunctionType::get(rtyp, elems, isVarargs));
}

Btype *Llvm_backend::array_type(Btype *element_btype, Bexpression *length) {
  assert(false && "LLvm_backend::array_type not yet implemented");
  return NULL;
}

// LLVM doesn't directly support placeholder types other than opaque
// structs, so the general strategy for placeholders is to create an
// opaque struct (corresponding to the thing being pointed to) and then
// make a pointer to it. Since LLVM allows only a single opaque struct
// type with a given name within a given context, we generally throw
// out the name/location information passed into the placeholder type
// creation routines.

llvm::Type *Llvm_backend::make_opaque_llvm_type() {
  return llvm::StructType::create(context_);
}

// Create a placeholder for a pointer type.

Btype *Llvm_backend::placeholder_pointer_type(const std::string &name,
                                              Location location, bool) {
  llvm::Type *opaque = make_opaque_llvm_type();
  llvm::Type *ph_ptr_typ = llvm::PointerType::get(opaque, address_space_);
  return make_placeholder_type(ph_ptr_typ);
}

// Set the real target type for a placeholder pointer type.

bool Llvm_backend::set_placeholder_pointer_type(Btype *placeholder,
                                                Btype *to_type) {
  assert(placeholder);
  assert(to_type);
  if (placeholder == error_type_ || to_type == error_type_)
    return false;
  assert(to_type->type()->isPointerTy());
  update_placeholder_underlying_type(placeholder, to_type->type());
  return true;
}

// Set the real values for a placeholder function type.

bool Llvm_backend::set_placeholder_function_type(Btype *placeholder,
                                                 Btype *ft) {
  return set_placeholder_pointer_type(placeholder, ft);
}

// Create a placeholder for a struct type.

Btype *Llvm_backend::placeholder_struct_type(const std::string &name,
                                             Location location) {
  return make_placeholder_type(make_opaque_llvm_type());
}

// Fill in the fields of a placeholder struct type.

bool Llvm_backend::set_placeholder_struct_type(
    Btype *placeholder, const std::vector<Btyped_identifier> &fields) {
  if (placeholder == error_type_)
    return false;
  Btype *stype = struct_type(fields);
  update_placeholder_underlying_type(placeholder, stype->type());
  return true;
}

// Create a placeholder for an array type.

Btype *Llvm_backend::placeholder_array_type(const std::string &name,
                                            Location location) {
  assert(false && "LLvm_backend::placeholder_array_type not yet implemented");
  return NULL;
}

// Fill in the components of a placeholder array type.

bool Llvm_backend::set_placeholder_array_type(Btype *placeholder,
                                              Btype *element_btype,
                                              Bexpression *length) {
  if (placeholder == error_type_)
    return false;
  Btype *atype = array_type(element_btype, length);
  update_placeholder_underlying_type(placeholder, atype->type());
  return true;
}

// Return a named version of a type.

Btype *Llvm_backend::named_type(const std::string &name,
                                Btype *btype,
                                Location location)
{
  // TODO: add support for debug metadata

  // In the LLVM type world, all types are nameless except for so-called
  // identified struct types. For this reason, names are stored in a side
  // data structure.

  named_llvm_type cand(name, btype->type());
  auto it = named_typemap_.find(cand);
  if (it != named_typemap_.end())
    return it->second;
  Btype *rval = new Btype(btype->type());
  named_typemap_[cand] = rval;
  return rval;
}

// Return a pointer type used as a marker for a circular type.

Btype *Llvm_backend::circular_pointer_type(Btype *, bool) {
  return make_anon_type(llvm_ptr_type_);
}

// Return whether we might be looking at a circular type.

bool Llvm_backend::is_circular_pointer_type(Btype *btype) {
  return btype->type() == llvm_ptr_type_;
}

// Return the size of a type.

int64_t Llvm_backend::type_size(Btype *btype) {
  if (btype == error_type_)
    return 1;
  uint64_t uval = datalayout_.getTypeSizeInBits(btype->type());
  return static_cast<int64_t>(uval);
}

// Return the alignment of a type.

int64_t Llvm_backend::type_alignment(Btype *btype) {
  if (btype == error_type_)
    return 1;
  unsigned uval = datalayout_.getPrefTypeAlignment(btype->type());
  return static_cast<int64_t>(uval);
}

// Return the alignment of a struct field of type BTYPE.
//
// One case where type_field_align(X) != type_align(X) is
// for type 'double' on x86 32-bit, where for compatibility
// a double field is 4-byte aligned but will be 8-byte aligned
// otherwise.

int64_t Llvm_backend::type_field_alignment(Btype *btype)
{
  // Corner cases.
  if (! btype->type()->isSized() || btype == error_type_)
    return -1;

  // Create a new anonymous struct with two fields: first field is a
  // single byte, second field is of type btype. Then use
  // getElementOffset to find out where the second one has been
  // placed. Finally, return min of alignof(btype) and that value.

  llvm::SmallVector<llvm::Type *, 2> elems(2);
  elems[0] = llvm::Type::getInt1Ty(context_);
  elems[1] = btype->type();
  llvm::StructType *dummyst = llvm::StructType::get(context_, elems);
  const llvm::StructLayout *sl = datalayout_.getStructLayout(dummyst);
  uint64_t uoff = sl->getElementOffset(1);
  unsigned talign = datalayout_.getPrefTypeAlignment(btype->type());
  int64_t rval = (uoff < talign ? uoff : talign);
  return rval;
}

// Return the offset of a field in a struct.

int64_t Llvm_backend::type_field_offset(Btype *btype, size_t index) {
  if (btype == error_type_)
    return 0;
  assert(btype->type()->isStructTy());
  llvm::StructType *llvm_st = llvm::cast<llvm::StructType>(btype->type());
  const llvm::StructLayout *sl = datalayout_.getStructLayout(llvm_st);
  uint64_t uoff = sl->getElementOffset(index);
  return static_cast<int64_t>(uoff);
}

// Return the zero value for a type.

Bexpression *Llvm_backend::zero_expression(Btype *btype) {
  assert(false && "LLvm_backend::zero_expression not yet implemented");
  return NULL;
}

Bexpression *Llvm_backend::error_expression() {
  assert(false && "LLvm_backend::error_expression not yet implemented");
  return NULL;
}

Bexpression *Llvm_backend::nil_pointer_expression() {
  assert(false && "LLvm_backend::nil_pointer_expression not yet implemented");
  return NULL;
}

// An expression that references a variable.

Bexpression *Llvm_backend::var_expression(Bvariable *var, Location location) {
  assert(false && "LLvm_backend::var_expression not yet implemented");
  return NULL;
}

// An expression that indirectly references an expression.

Bexpression *Llvm_backend::indirect_expression(Btype *btype, Bexpression *expr,
                                               bool known_valid,
                                               Location location) {
  assert(false && "LLvm_backend::indirect_expression not yet implemented");
  return NULL;
}

// Return an expression that declares a constant named NAME with the
// constant value VAL in BTYPE.

Bexpression *Llvm_backend::named_constant_expression(Btype *btype,
                                                     const std::string &name,
                                                     Bexpression *val,
                                                     Location location) {
  assert(false && "LLvm_backend::named_constant_expression not yet impl");
  return NULL;
}

// Return a typed value as a constant integer.

Bexpression *Llvm_backend::integer_constant_expression(Btype *btype,
                                                       mpz_t val) {
  assert(false && "LLvm_backend::integer_constant_expression not yet impl");
  return NULL;
}

// Return a typed value as a constant floating-point number.

Bexpression *Llvm_backend::float_constant_expression(Btype *btype, mpfr_t val) {
  assert(false && "Llvm_backend::float_constant_expression not yet impl");
  return NULL;
}

// Return a typed real and imaginary value as a constant complex number.

Bexpression *Llvm_backend::complex_constant_expression(Btype *btype,
                                                       mpc_t val) {
  assert(false && "Llvm_backend::complex_constant_expression not yet impl");
  return NULL;
}

// Make a constant string expression.

Bexpression *Llvm_backend::string_constant_expression(const std::string &val) {
  assert(false && "Llvm_backend::string_constant_expression not yet impl");
  return NULL;
}

// Make a constant boolean expression.

Bexpression *Llvm_backend::boolean_constant_expression(bool val) {
  assert(false && "Llvm_backend::boolean_constant_expression not yet impl");
  return NULL;
}

// Return the real part of a complex expression.

Bexpression *Llvm_backend::real_part_expression(Bexpression *bcomplex,
                                                Location location) {
  assert(false && "Llvm_backend::real_part_expression not yet impl");
  return NULL;
}

// Return the imaginary part of a complex expression.

Bexpression *Llvm_backend::imag_part_expression(Bexpression *bcomplex,
                                                Location location) {
  assert(false && "Llvm_backend::imag_part_expression not yet impl");
  return NULL;
}

// Make a complex expression given its real and imaginary parts.

Bexpression *Llvm_backend::complex_expression(Bexpression *breal,
                                              Bexpression *bimag,
                                              Location location) {
  assert(false && "Llvm_backend::complex_expression not yet impl");
  return NULL;
}

// An expression that converts an expression to a different type.

Bexpression *Llvm_backend::convert_expression(Btype *type, Bexpression *expr,
                                              Location location) {
  assert(false && "Llvm_backend::convert_expression not yet impl");
  return NULL;
}

// Get the address of a function.

Bexpression *Llvm_backend::function_code_expression(Bfunction *bfunc,
                                                    Location location) {
  assert(false && "Llvm_backend::function_code_expression not yet impl");
  return NULL;
}

// Get the address of an expression.

Bexpression *Llvm_backend::address_expression(Bexpression *bexpr,
                                              Location location) {
  assert(false && "Llvm_backend::address_code_expression not yet impl");
  return NULL;
}

// Return an expression for the field at INDEX in BSTRUCT.

Bexpression *Llvm_backend::struct_field_expression(Bexpression *bstruct,
                                                   size_t index,
                                                   Location location) {
  assert(false && "Llvm_backend::struct_field_expression not yet impl");
  return NULL;
}

// Return an expression that executes BSTAT before BEXPR.

Bexpression *Llvm_backend::compound_expression(Bstatement *bstat,
                                               Bexpression *bexpr,
                                               Location location) {
  assert(false && "Llvm_backend::compound_expression not yet impl");
  return NULL;
}

// Return an expression that executes THEN_EXPR if CONDITION is true, or
// ELSE_EXPR otherwise.

Bexpression *Llvm_backend::conditional_expression(Btype *btype,
                                                  Bexpression *condition,
                                                  Bexpression *then_expr,
                                                  Bexpression *else_expr,
                                                  Location location) {
  assert(false && "Llvm_backend::conditional_expression not yet impl");
  return NULL;
}

// Return an expression for the unary operation OP EXPR.

Bexpression *Llvm_backend::unary_expression(Operator op, Bexpression *expr,
                                            Location location) {
  assert(false && "Llvm_backend::unary_expression not yet impl");
  return NULL;
}

#if 0

// Convert a gofrontend operator to an equivalent tree_code.

static enum tree_code
operator_to_tree_code(Operator op, tree type)
{
  enum tree_code code;
  switch (op)
    {
    case OPERATOR_EQEQ:
      code = EQ_EXPR;
      break;
    case OPERATOR_NOTEQ:
      code = NE_EXPR;
      break;
    case OPERATOR_LT:
      code = LT_EXPR;
      break;
    case OPERATOR_LE:
      code = LE_EXPR;
      break;
    case OPERATOR_GT:
      code = GT_EXPR;
      break;
    case OPERATOR_GE:
      code = GE_EXPR;
      break;
    case OPERATOR_OROR:
      code = TRUTH_ORIF_EXPR;
      break;
    case OPERATOR_ANDAND:
      code = TRUTH_ANDIF_EXPR;
      break;
    case OPERATOR_PLUS:
      code = PLUS_EXPR;
      break;
    case OPERATOR_MINUS:
      code = MINUS_EXPR;
      break;
    case OPERATOR_OR:
      code = BIT_IOR_EXPR;
      break;
    case OPERATOR_XOR:
      code = BIT_XOR_EXPR;
      break;
    case OPERATOR_MULT:
      code = MULT_EXPR;
      break;
    case OPERATOR_DIV:
      if (TREE_CODE(type) == REAL_TYPE || TREE_CODE(type) == COMPLEX_TYPE)
        code = RDIV_EXPR;
      else
        code = TRUNC_DIV_EXPR;
      break;
    case OPERATOR_MOD:
      code = TRUNC_MOD_EXPR;
      break;
    case OPERATOR_LSHIFT:
      code = LSHIFT_EXPR;
      break;
    case OPERATOR_RSHIFT:
      code = RSHIFT_EXPR;
      break;
    case OPERATOR_AND:
      code = BIT_AND_EXPR;
      break;
    case OPERATOR_BITCLEAR:
      code = BIT_AND_EXPR;
      break;
    default:
      gcc_unreachable();
    }

  return code;
}

#endif

// Return an expression for the binary operation LEFT OP RIGHT.

Bexpression *Llvm_backend::binary_expression(Operator op, Bexpression *left,
                                             Bexpression *right,
                                             Location location) {
  assert(false && "Llvm_backend::binary_expression not yet impl");
  return NULL;
}

// Return an expression that constructs BTYPE with VALS.

Bexpression *Llvm_backend::constructor_expression(
    Btype *btype, const std::vector<Bexpression *> &vals, Location location) {
  assert(false && "Llvm_backend::constructor_expression not yet impl");
  return NULL;
}

Bexpression *Llvm_backend::array_constructor_expression(
    Btype *array_btype, const std::vector<unsigned long> &indexes,
    const std::vector<Bexpression *> &vals, Location location) {
  assert(false && "Llvm_backend::array_constructor_expression not yet impl");
  return NULL;
}

// Return an expression for the address of BASE[INDEX].

Bexpression *Llvm_backend::pointer_offset_expression(Bexpression *base,
                                                     Bexpression *index,
                                                     Location location) {
  assert(false && "Llvm_backend::pointer_offset_expression not yet impl");
  return NULL;
}

// Return an expression representing ARRAY[INDEX]

Bexpression *Llvm_backend::array_index_expression(Bexpression *array,
                                                  Bexpression *index,
                                                  Location location) {
  assert(false && "Llvm_backend::array_index_expression not yet impl");
  return NULL;
}

// Create an expression for a call to FN_EXPR with FN_ARGS.
Bexpression *
Llvm_backend::call_expression(Bexpression *fn_expr,
                              const std::vector<Bexpression *> &fn_args,
                              Bexpression *chain_expr, Location location) {
  assert(false && "Llvm_backend::call_expression not yet impl");
  return NULL;
}

// Return an expression that allocates SIZE bytes on the stack.

Bexpression *Llvm_backend::stack_allocation_expression(int64_t size,
                                                       Location location) {
  assert(false && "Llvm_backend::stack_allocation_expression not yet impl");
  return NULL;
}

Bstatement *Llvm_backend::error_statement() {
  assert(false && "Llvm_backend::error_statement not yet impl");
  return NULL;
}

// An expression as a statement.

Bstatement *Llvm_backend::expression_statement(Bexpression *expr) {
  assert(false && "Llvm_backend::expression_statement not yet impl");
  return NULL;
}

// Variable initialization.

Bstatement *Llvm_backend::init_statement(Bvariable *var, Bexpression *init) {
  assert(false && "Llvm_backend::init_statement not yet impl");
  return NULL;
}

// Assignment.

Bstatement *Llvm_backend::assignment_statement(Bexpression *lhs,
                                               Bexpression *rhs,
                                               Location location) {
  assert(false && "Llvm_backend::assignment_statement not yet impl");
  return NULL;
}

// Return.

Bstatement *
Llvm_backend::return_statement(Bfunction *bfunction,
                               const std::vector<Bexpression *> &vals,
                               Location location) {
  assert(false && "Llvm_backend::return_statement not yet impl");
  return NULL;
}

// Create a statement that attempts to execute BSTAT and calls EXCEPT_STMT if an
// error occurs.  EXCEPT_STMT may be NULL.  FINALLY_STMT may be NULL and if not
// NULL, it will always be executed.  This is used for handling defers in Go
// functions.  In C++, the resulting code is of this form:
//   try { BSTAT; } catch { EXCEPT_STMT; } finally { FINALLY_STMT; }

Bstatement *Llvm_backend::exception_handler_statement(Bstatement *bstat,
                                                      Bstatement *except_stmt,
                                                      Bstatement *finally_stmt,
                                                      Location location) {
  assert(false && "Llvm_backend::exception_handler_statement not yet impl");
  return NULL;
}

// If.

Bstatement *Llvm_backend::if_statement(Bexpression *condition,
                                       Bblock *then_block, Bblock *else_block,
                                       Location location) {
  assert(false && "Llvm_backend::if_statement not yet impl");
  return NULL;
}

// Switch.

Bstatement *Llvm_backend::switch_statement(
    Bfunction *function, Bexpression *value,
    const std::vector<std::vector<Bexpression *>> &cases,
    const std::vector<Bstatement *> &statements, Location switch_location) {
  assert(false && "Llvm_backend::switch_statement not yet impl");
  return NULL;
}

// Pair of statements.

Bstatement *Llvm_backend::compound_statement(Bstatement *s1, Bstatement *s2) {
  assert(false && "Llvm_backend::compound_statement not yet impl");
  return NULL;
}

// List of statements.

Bstatement *
Llvm_backend::statement_list(const std::vector<Bstatement *> &statements) {
  assert(false && "Llvm_backend::statement_list not yet impl");
  return NULL;
}

// Make a block.  For some reason gcc uses a dual structure for
// blocks: BLOCK tree nodes and BIND_EXPR tree nodes.  Since the
// BIND_EXPR node points to the BLOCK node, we store the BIND_EXPR in
// the Bblock.

Bblock *Llvm_backend::block(Bfunction *function, Bblock *enclosing,
                            const std::vector<Bvariable *> &vars,
                            Location start_location, Location) {
  assert(false && "Llvm_backend::block not yet impl");
  return NULL;
}

// Add statements to a block.

void Llvm_backend::block_add_statements(
    Bblock *bblock, const std::vector<Bstatement *> &statements) {
  assert(false && "Llvm_backend::block_add_statements not yet impl");
}

// Return a block as a statement.

Bstatement *Llvm_backend::block_statement(Bblock *bblock) {
  assert(false && "Llvm_backend::block_statement not yet impl");
  return NULL;
}

// Make a global variable.

Bvariable *Llvm_backend::global_variable(const std::string &package_name,
                                         const std::string &pkgpath,
                                         const std::string &name, Btype *btype,
                                         bool is_external, bool is_hidden,
                                         bool in_unique_section,
                                         Location location) {
  // NB: add code to insure non-zero size
  assert(false && "Llvm_backend::global_variable not yet impl");
  return NULL;
}

// Set the initial value of a global variable.

void Llvm_backend::global_variable_set_init(Bvariable *var, Bexpression *expr) {
  assert(false && "Llvm_backend::global_variable_set_init not yet impl");
}

Bvariable *Llvm_backend::error_variable() {
  assert(false && "Llvm_backend::error_variable not yet impl");
  return NULL;
}
// Make a local variable.

Bvariable *Llvm_backend::local_variable(Bfunction *function,
                                        const std::string &name, Btype *btype,
                                        bool is_address_taken,
                                        Location location) {
  assert(false && "Llvm_backend::local_variable not yet impl");
  return NULL;
}

// Make a function parameter variable.

Bvariable *Llvm_backend::parameter_variable(Bfunction *function,
                                            const std::string &name,
                                            Btype *btype, bool is_address_taken,
                                            Location location) {
  assert(false && "Llvm_backend::parameter_variable not yet impl");
  return NULL;
}

// Make a static chain variable.

Bvariable *Llvm_backend::static_chain_variable(Bfunction *function,
                                               const std::string &name,
                                               Btype *btype,
                                               Location location) {
  assert(false && "Llvm_backend::static_chain_variable not yet impl");
  return NULL;
}

// Make a temporary variable.

Bvariable *Llvm_backend::temporary_variable(Bfunction *function, Bblock *bblock,
                                            Btype *btype, Bexpression *binit,
                                            bool is_address_taken,
                                            Location location,
                                            Bstatement **pstatement) {
  assert(false && "Llvm_backend::temporary_variable not yet impl");
  return NULL;
}

// Create an implicit variable that is compiler-defined.  This is used when
// generating GC root variables and storing the values of a slice initializer.

Bvariable *Llvm_backend::implicit_variable(const std::string &name, Btype *type,
                                           bool is_hidden, bool is_constant,
                                           bool is_common, int64_t alignment) {
  assert(false && "Llvm_backend::implicit_variable not yet impl");
  return NULL;
}

// Set the initalizer for a variable created by implicit_variable.
// This is where we finish compiling the variable.

void Llvm_backend::implicit_variable_set_init(Bvariable *var,
                                              const std::string &, Btype *,
                                              bool, bool, bool is_common,
                                              Bexpression *init) {
  assert(false && "Llvm_backend::implicit_variable_set_init not yet impl");
}

// Return a reference to an implicit variable defined in another package.

Bvariable *Llvm_backend::implicit_variable_reference(const std::string &name,
                                                     Btype *btype) {
  assert(false && "Llvm_backend::implicit_variable_reference not yet impl");
  return NULL;
}

// Create a named immutable initialized data structure.

Bvariable *Llvm_backend::immutable_struct(const std::string &name,
                                          bool is_hidden, bool is_common,
                                          Btype *btype, Location location) {
  assert(false && "Llvm_backend::immutable_struct not yet impl");
  return NULL;
}

// Set the initializer for a variable created by immutable_struct.
// This is where we finish compiling the variable.

void Llvm_backend::immutable_struct_set_init(Bvariable *var,
                                             const std::string &, bool,
                                             bool is_common, Btype *, Location,
                                             Bexpression *initializer) {
  assert(false && "Llvm_backend::immutable_struct_set_init not yet impl");
}

// Return a reference to an immutable initialized data structure
// defined in another package.

Bvariable *Llvm_backend::immutable_struct_reference(const std::string &name,
                                                    Btype *btype,
                                                    Location location) {
  assert(false && "Llvm_backend::immutable_struct_reference not yet impl");
  return NULL;
}

// Make a label.

Blabel *Llvm_backend::label(Bfunction *function, const std::string &name,
                            Location location) {
  assert(false && "Llvm_backend::label not yet impl");
  return NULL;
}

// Make a statement which defines a label.

Bstatement *Llvm_backend::label_definition_statement(Blabel *label) {
  assert(false && "Llvm_backend::label_definition_statement not yet impl");
  return NULL;
}

// Make a goto statement.

Bstatement *Llvm_backend::goto_statement(Blabel *label, Location location) {
  assert(false && "Llvm_backend::goto_statement not yet impl");
  return NULL;
}

// Get the address of a label.

Bexpression *Llvm_backend::label_address(Blabel *label, Location location) {
  assert(false && "Llvm_backend::label_address not yet impl");
  return NULL;
}

Bfunction*
Llvm_backend::error_function()
{
  assert(false && "Llvm_backend::error_function not yet impl");
  return NULL;
}

// Declare or define a new function.

Bfunction *Llvm_backend::function(Btype *fntype, const std::string &name,
                                  const std::string &asm_name, bool is_visible,
                                  bool is_declaration, bool is_inlinable,
                                  bool disable_split_stack,
                                  bool in_unique_section, Location location)
{
  llvm::Twine fn(name);
  llvm::FunctionType *fty = llvm::cast<llvm::FunctionType>(fntype->type());
  llvm::GlobalValue::LinkageTypes linkage = llvm::GlobalValue::ExternalLinkage;
  if (!is_visible)
    linkage = llvm::GlobalValue::InternalLinkage;

  llvm::Function *fcn = llvm::Function::Create(fty, linkage, fn, module_.get());

  // visibility
  fcn->setVisibility(llvm::GlobalValue::HiddenVisibility);

  // inline/noinline
  if (!is_inlinable)
    fcn->addFnAttr(llvm::Attribute::NoInline);

  Bfunction *bfunc = new Bfunction(fcn);

  // TODO: unique section support. llvm::GlobalObject has support for
  // setting COMDAT groups and section names, but nothing to manage how
  // section names are created or doled out as far as I can tell
  assert(!in_unique_section);

  if (disable_split_stack)
    bfunc->setSplitStack(Bfunction::NoSplit);

  return bfunc;
}

// Create a statement that runs all deferred calls for FUNCTION.  This should
// be a statement that looks like this in C++:
//   finish:
//     try { UNDEFER; } catch { CHECK_DEFER; goto finish; }

Bstatement *Llvm_backend::function_defer_statement(Bfunction *function,
                                                   Bexpression *undefer,
                                                   Bexpression *defer,
                                                   Location location) {
  assert(false && "Llvm_backend::function_defer_statement not yet impl");
  return NULL;
}

// Record PARAM_VARS as the variables to use for the parameters of FUNCTION.
// This will only be called for a function definition.

bool Llvm_backend::function_set_parameters(
    Bfunction *function, const std::vector<Bvariable *> &param_vars) {
  assert(false && "Llvm_backend::function_set_parameters not yet impl");
  return false;
}

// Set the function body for FUNCTION using the code in CODE_BLOCK.

bool Llvm_backend::function_set_body(Bfunction *function,
                                     Bstatement *code_stmt) {
  assert(false && "Llvm_backend::function_set_body not yet impl");
  return false;
}

// Look up a named built-in function in the current backend implementation.
// Returns NULL if no built-in function by that name exists.

Bfunction *Llvm_backend::lookup_builtin(const std::string &name) {
  assert(false && "Llvm_backend::lookup_builtin not yet impl");
  return NULL;
}

// Write the definitions for all TYPE_DECLS, CONSTANT_DECLS,
// FUNCTION_DECLS, and VARIABLE_DECLS declared globally, as well as
// emit early debugging information.

void Llvm_backend::write_global_definitions(
    const std::vector<Btype *> &type_decls,
    const std::vector<Bexpression *> &constant_decls,
    const std::vector<Bfunction *> &function_decls,
    const std::vector<Bvariable *> &variable_decls) {
  assert(false && "Llvm_backend::write_global_definitions not yet impl");
}

#if 0
// Define a builtin function.  BCODE is the builtin function code
// defined by builtins.def.  NAME is the name of the builtin function.
// LIBNAME is the name of the corresponding library function, and is
// NULL if there isn't one.  FNTYPE is the type of the function.
// CONST_P is true if the function has the const attribute.
// NORETURN_P is true if the function has the noreturn attribute.

void
Llvm_backend::define_builtin(built_in_function bcode, const char* name,
                            const char* libname, tree fntype, bool const_p,
                            bool noreturn_p)
{
  assert(false && "Llvm_backend::define_builtin not yet impl");
}
#endif

// Convert an identifier for use in an error message.
// TODO(tham): scan the identifier to determine if contains
// only ASCII or printable UTF-8, then perform character set
// conversions if needed.

const char *go_localize_identifier(const char *ident) { return ident; }

// Return the backend generator.

Backend *go_get_backend(llvm::LLVMContext &context) {
  return new Llvm_backend(context);
}
