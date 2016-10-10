//===-- llvm-goparse.cpp - Debug test driver for go parser for llvm  ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Invokes the gofrontend parser on specified input files.
//
//===----------------------------------------------------------------------===//


#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Triple.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <system_error>

#include "go-c.h"
#include "go-llvm-linemap.h"
#include "go-llvm-backend.h"

using namespace llvm;

static cl::opt<std::string>
TargetTriple("mtriple", cl::desc("Override target triple for module"));

static cl::list<std::string>
InputFilenames(cl::Positional,
               cl::desc("<input go source files>"),
               cl::OneOrMore);

static cl::opt<bool>
CheckDivideZero("fgo-check-divide-zero",
                cl::desc("Add explicit checks for divide-by-zero."),
                cl::init(true));

static cl::opt<bool>
CheckDivideOverflow("fgo-check-divide-overflow",
                    cl::desc("Add explicit checks for division overflow in INT_MIN / -1."),
                    cl::init(true));

static cl::opt<bool>
DumpAst("fgo-dump-ast",
        cl::desc("Dump Go frontend internal AST structure."),
        cl::init(false));

static cl::opt<bool>
OptimizeAllocs("fgo-optimize-allocs",
               cl::desc("Enable escape analysis in the go frontend."),
               cl::init(false));

static cl::opt<std::string>
PackagePath("fgo-pkgpath",
            cl::desc("Set Go package path."),
            cl::init(""));

static cl::opt<std::string>
PackagePrefix("fgo-prefix",
              cl::desc("Set package-specific prefix for exported Go names."),
              cl::init(""));

static cl::opt<std::string>
RelativeImportPath("fgo-relative-import-path",
                   cl::desc("Treat a relative import as relative to path."),
                   cl::init(""));

static cl::opt<int>
EscapeDebugLevel("fgo-debug-escape",
                 cl::desc("Emit debugging information related to the "
                          "escape analysis pass when run with "
                          "-fgo-optimize-allocs."),
                 cl::init(0));

static void init_gogo(TargetMachine *Target, llvm::LLVMContext &Context)
{
  // does the comment below still apply?
#if 0
  /* We must create the gogo IR after calling build_common_tree_nodes
     (because Gogo::define_builtin_function_trees refers indirectly
     to, e.g., unsigned_char_type_node) but before calling
     build_common_builtin_nodes (because it calls, indirectly,
     go_type_for_size).  */
#endif

  struct go_create_gogo_args args;
  args.int_type_size = 4; // FIXME: get from target
  args.pointer_size = Target->getPointerSize();
  args.pkgpath = PackagePath.empty() ? NULL : PackagePath.c_str();
  args.prefix = PackagePrefix.empty() ? NULL : PackagePrefix.c_str();
  args.relative_import_path = RelativeImportPath.empty() ? NULL : RelativeImportPath.c_str();
  args.c_header = NULL; // FIXME: not yet supported
  args.check_divide_by_zero = CheckDivideZero;
  args.check_divide_overflow = CheckDivideOverflow;
  args.compiling_runtime = false; // FIXME: not yet supported
  args.debug_escape_level = EscapeDebugLevel;
  args.linemap = go_get_linemap();
  args.backend = go_get_backend(Context);
  go_create_gogo (&args);

#if 0
    /* The default precision for floating point numbers.  This is used
     for floating point constants with abstract type.  This may
     eventually be controllable by a command line option.  */
  mpfr_set_default_prec (256);
#endif
}

int main(int argc, char **argv)
{
  Triple TheTriple;

  cl::ParseCommandLineOptions(argc, argv, "llvm go parser driver\n");

  TheTriple = Triple(Triple::normalize(TargetTriple));
  if (TheTriple.getTriple().empty())
    TheTriple.setTriple(sys::getDefaultTargetTriple());

  // Get the target specific parser.
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(MArch, TheTriple,
                                                         Error);
  if (!TheTarget) {
    errs() << argv[0] << ": " << Error;
    return 1;
  }

  // FIXME: cpu, features not yet supported
  std::string CPUStr = getCPUStr(), FeaturesStr = getFeaturesStr();

  TargetOptions Options = InitTargetOptionsFromCodeGenFlags();
  CodeGenOpt::Level OLvl = CodeGenOpt::Default;
  std::unique_ptr<TargetMachine> Target(
      TheTarget->createTargetMachine(TheTriple.getTriple(), CPUStr, FeaturesStr,
                                     Options, getRelocModel(), CMModel, OLvl));
  assert(Target && "Could not allocate target machine!");

  // Print a stack trace if we signal out.
  llvm::LLVMContext Context;
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.

  init_gogo(Target.get(), Context);

#if 0
extern void go_parse_input_files (const char**, unsigned int,
                                  bool only_check_syntax,
                                  bool require_return_statement);

  go_parse_input_files (in_fnames, num_in_fnames, flag_syntax_only,
                        go_require_return_statement);
#endif
  return 0;
}
