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
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <system_error>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "go-c.h"
#include "go-llvm-linemap.h"
#include "go-llvm-diagnostics.h"
#include "go-llvm.h"

#include "mpfr.h"

using namespace llvm;

static cl::opt<std::string>
TargetTriple("mtriple", cl::desc("Override target triple for module"));

static cl::list<std::string>
InputFilenames(cl::Positional,
               cl::desc("<input go source files>"),
               cl::OneOrMore);

static cl::opt<std::string>
IncludeDirs("I", cl::desc("<include dirs>"));

static cl::opt<std::string>
LibDirs("L", cl::desc("<system library dirs>"));

// Determine optimization level.
static cl::opt<char>
OptLevel("O",
         cl::desc("Optimization level. [-O0, -O1, -O2, or -O3] "
                  "(default = '-O2')"),
         cl::Prefix,
         cl::ZeroOrMore,
         cl::init(' '));

static cl::opt<std::string>
OutputFileName("o",
               cl::desc("Set name of output file."),
               cl::init(""));

// These are provided for compatibility purposes -- they are currently ignored.
static cl::opt<bool>
M64Option("m64",  cl::desc("Dummy -m64 arg."), cl::init(false));
static cl::opt<bool>
MinusGOption("g",  cl::desc("Dummy -g arg."), cl::init(false));
static cl::opt<bool>
MinusCOption("c",  cl::desc("Dummy -c arg."), cl::init(false));
static cl::opt<bool>
MinusVOption("v",  cl::desc("Dummy -v arg."), cl::init(false));

static cl::opt<bool>
NoBackend("nobackend",
          cl::desc("Stub out back end invocation."),
          cl::init(false));

static cl::opt<bool>
NoVerify("noverify",
          cl::desc("Stub out module verifier invocation."),
          cl::init(false));

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
DumpIR("dump-ir",
        cl::desc("Dump LLVM IR for module at end of run."),
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

static cl::opt<unsigned>
TraceLevel("tracelevel",
           cl::desc("Set debug trace level (def: 0, no trace output)."),
           cl::init(0));

static std::unique_ptr<tool_output_file>
GetOutputStream() {
  // Decide if we need "binary" output.
  bool Binary = false;
  switch (FileType) {
  case TargetMachine::CGFT_AssemblyFile:
    break;
  case TargetMachine::CGFT_ObjectFile:
  case TargetMachine::CGFT_Null:
    Binary = true;
    break;
  }

  // Open the file.
  std::error_code EC;
  sys::fs::OpenFlags OpenFlags = sys::fs::F_None;
  if (!Binary)
    OpenFlags |= sys::fs::F_Text;
  auto FDOut = llvm::make_unique<tool_output_file>(OutputFileName, EC,
                                                   OpenFlags);
  if (EC) {
    errs() << EC.message() << '\n';
    return nullptr;
  }

  return FDOut;
}

static void DiagnosticHandler(const DiagnosticInfo &DI, void *Context) {
  bool *HasError = static_cast<bool *>(Context);
  if (DI.getSeverity() == DS_Error)
    *HasError = true;

  if (auto *Remark = dyn_cast<DiagnosticInfoOptimizationBase>(&DI))
    if (!Remark->isEnabled())
      return;

  DiagnosticPrinterRawOStream DP(errs());
  errs() << LLVMContext::getDiagnosticMessagePrefix(DI.getSeverity()) << ": ";
  DI.print(DP);
  errs() << "\n";
}

static Llvm_backend *init_gogo(TargetMachine *Target,
                               llvm::LLVMContext &Context,
                               llvm::Module *module,
                               Llvm_linemap *linemap)
{
  struct go_create_gogo_args args;
  unsigned bpi = Target->getPointerSize() * 8;
  args.int_type_size = bpi;
  args.pointer_size = bpi;
  args.pkgpath = PackagePath.empty() ? NULL : PackagePath.c_str();
  args.prefix = PackagePrefix.empty() ? NULL : PackagePrefix.c_str();
  args.relative_import_path = RelativeImportPath.empty() ? NULL : RelativeImportPath.c_str();
  args.c_header = NULL; // FIXME: not yet supported
  args.check_divide_by_zero = CheckDivideZero;
  args.check_divide_overflow = CheckDivideOverflow;
  args.compiling_runtime = false; // FIXME: not yet supported
  args.debug_escape_level = EscapeDebugLevel;
  args.linemap = linemap;
  Llvm_backend *backend = new Llvm_backend(Context, module, linemap);
  args.backend = backend;
  go_create_gogo (&args);

  /* The default precision for floating point numbers.  This is used
     for floating point constants with abstract type.  This may
     eventually be controllable by a command line option.  */
  mpfr_set_default_prec (256);

  return backend;
}

int main(int argc, char **argv)
{
  Triple TheTriple;

  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();

  cl::ParseCommandLineOptions(argc, argv, "llvm go parser driver\n");

  // Initialize codegen and IR passes
  PassRegistry *Registry = PassRegistry::getPassRegistry();
  initializeCore(*Registry);
  initializeCodeGen(*Registry);
  initializeLoopStrengthReducePass(*Registry);
  initializeLowerIntrinsicsPass(*Registry);
  initializeCountingFunctionInserterPass(*Registry);
  initializeUnreachableBlockElimLegacyPassPass(*Registry);
  initializeConstantHoistingLegacyPassPass(*Registry);
  initializeScalarOpts(*Registry);
  initializeVectorization(*Registry);

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

  // optimization level
  CodeGenOpt::Level OLvl = CodeGenOpt::Default;
  switch (OptLevel) {
  default:
    errs() << argv[0] << ": invalid optimization level.\n";
    return 1;
  case ' ': break;
  case '0': OLvl = CodeGenOpt::None; break;
  case '1': OLvl = CodeGenOpt::Less; break;
  case '2': OLvl = CodeGenOpt::Default; break;
  case '3': OLvl = CodeGenOpt::Aggressive; break;
  }

  TargetOptions Options = InitTargetOptionsFromCodeGenFlags();
  Options.DisableIntegratedAS = true;
  std::unique_ptr<TargetMachine> Target(
      TheTarget->createTargetMachine(TheTriple.getTriple(), CPUStr, FeaturesStr,
                                     Options, getRelocModel(), CMModel, OLvl));
  assert(Target && "Could not allocate target machine!");

  // Print a stack trace if we signal out.
  llvm::LLVMContext Context;
  bool hasError = false;
  Context.setDiagnosticHandler(DiagnosticHandler, &hasError);
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.

  // Construct linemap and module
  std::unique_ptr<Llvm_linemap> linemap(new Llvm_linemap());
  std::unique_ptr<llvm::Module> module(new llvm::Module("gomodule", Context));

  // Add the target data from the target machine, if it exists
  module->setTargetTriple(TheTriple.getTriple());
  module->setDataLayout(Target->createDataLayout());

  // Now construct Llvm_backend helper.
  std::unique_ptr<Llvm_backend> backend(init_gogo(Target.get(), Context,
                                                  module.get(), linemap.get()));
  backend->setTraceLevel(TraceLevel);

  // Support -fgo-dump-ast
  if (DumpAst)
    go_enable_dump("ast");

  // Include dirs
  if (! IncludeDirs.empty()) {
    std::stringstream ss(IncludeDirs);
    std::string dir;
    while(std::getline(ss, dir, ':')) {
      struct stat st;
      if (stat (dir.c_str(), &st) == 0 && S_ISDIR (st.st_mode))
        go_add_search_path(dir.c_str());
    }
  }

  // Library dirs
  // TODO: add version, architecture dirs
  if (! LibDirs.empty()) {
    std::stringstream ss(LibDirs);
    std::string dir;
    while(std::getline(ss, dir, ':')) {
      struct stat st;
      if (stat (dir.c_str(), &st) == 0 && S_ISDIR (st.st_mode))
        go_add_search_path(dir.c_str());
    }
  }


  // Figure out where we are going to send the output.
  if (OutputFileName.empty()) {
    errs() << "no output file specified (please use -o option)\n";
    return 1;
  }
  std::unique_ptr<tool_output_file> Out = GetOutputStream();
  if (!Out) return 1;

  // Kick off the front end
  unsigned nfiles = InputFilenames.size();
  std::unique_ptr<const char *> filenames(new const char *[nfiles]);
  const char **fns = filenames.get();
  unsigned idx = 0;
  for (auto &fn : InputFilenames)
    fns[idx++] = fn.c_str();
  go_parse_input_files(fns, nfiles, false, true);
  if (! NoBackend)
    go_write_globals();
  if (! NoVerify && !go_be_saw_errors())
    backend->verifyModule();
  if (DumpIR)
    backend->dumpModule();
  if (TraceLevel)
    std::cerr << "linemap stats:" << linemap->statistics() << "\n";

  // Early exit at this point if we've seen errors
  if (go_be_saw_errors())
    return 1;


  // On to the back end for this module...
  llvm::Module *M = module.get();

  // Pass manager
  legacy::PassManager PM;

  // Add an appropriate TargetLibraryInfo pass for the module triple.
  TargetLibraryInfoImpl TLII(TheTriple);
  PM.add(new TargetLibraryInfoWrapperPass(TLII));

  // Override function attributes based on CPUStr, FeaturesStr, and command line
  // flags.
  setFunctionAttributes(CPUStr, FeaturesStr, *M);

  raw_pwrite_stream *OS = &Out->os();

  // Ask the target to add backend passes as necessary.
  if (Target->addPassesToEmitFile(PM, *OS, FileType)) {
    errs() << argv[0] << ": target does not support generation of this"
           << " file type!\n";
    return 1;
  }

  // Before executing passes, print the final values of the LLVM options.
  cl::PrintOptionValues();

  // Run pass manager
  PM.run(*M);

  // Check for errors
  auto HasError = *static_cast<bool *>(Context.getDiagnosticContext());
  if (HasError)
    return 1;

  // Declare success.
  Out->keep();

  return 0;
}
