//===-- go-llvm-dibuildhelper.cpp - implementation of DIBuildHelper -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for DIBuildHelper class.
//
//===----------------------------------------------------------------------===//

#include "go-llvm-dibuildhelper.h"
#include "go-llvm-typemanager.h"
#include "go-llvm-bexpression.h"
#include "go-llvm-bstatement.h"
#include "go-llvm-bfunction.h"
#include "go-llvm-bvariable.h"

#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"

DIBuildHelper::DIBuildHelper(TypeManager *typemanager,
                             Llvm_linemap *linemap,
                             llvm::DIBuilder &builder,
                             llvm::DIScope *moduleScope)
    : typemanager_(typemanager), linemap_(linemap),
      dibuilder_(builder), moduleScope_(moduleScope),
      known_locations_(0)
{
  pushDIScope(moduleScope);
}

void DIBuildHelper::beginFunction(llvm::DIScope *scope, Bfunction *function)
{
  known_locations_ = 0;

  // Create proper DIType for function
  llvm::DIType *dit =
      typemanager()->buildDIType(function->fcnType(), *this);
  llvm::DISubroutineType *dst =
      llvm::cast<llvm::DISubroutineType>(dit);

  // Now the function entry itself
  unsigned fcnLine = linemap()->location_line(function->location());
  bool isLocalToUnit = false; // FIXME -- look at exported/non-exported
  bool isDefinition = true;
  unsigned scopeLine = fcnLine; // FIXME -- determine correct value here
  llvm::DIFile *difile = diFileFromLocation(function->location());
  auto difunc =
      dibuilder().createFunction(scope, function->name(), function->asmName(),
                                 difile, fcnLine, dst, isLocalToUnit,
                                 isDefinition, scopeLine);
  pushDIScope(difunc);

  // Then the local variables. Note that block-scoped vars will be
  // registered later on when we visit their containing block.
  for (auto &v : function->getFunctionLocalVars()) {
    llvm::DIFile *vfile = diFileFromLocation(v->location());
    llvm::DIType *vdit =
        typemanager()->buildDIType(v->btype(), *this);
    unsigned vline = linemap()->location_line(v->location());
    auto *av = dibuilder().createAutoVariable(difunc, v->name(), vfile,
                                              vline, vdit);
  }
}

void DIBuildHelper::endFunction(Bfunction *function)
{
  llvm::DISubprogram *fscope =
      llvm::cast<llvm::DISubprogram>(popDIScope());

  // This is hacky but needed for the time being.
  // TODO: figure out a better strategy (more FDO-friendly)
  // for compiler-generated functions.
  if (known_locations_)
    function->function()->setSubprogram(fscope);
}

void DIBuildHelper::beginLexicalBlock(Bblock *block)
{
  // Register block with DIBuilder
  Location bloc = block->location();
  llvm::DIFile *file = diFileFromLocation(block->location());
  llvm::DILexicalBlock *dilb =
      dibuilder().createLexicalBlock(currentDIScope(), file,
                                     linemap()->location_line(bloc),
                                     linemap()->location_column(bloc));
  pushDIScope(dilb);

  // Declare local variables
  for (auto &v : block->vars()) {
    llvm::DIFile *vfile = diFileFromLocation(v->location());
    llvm::DIType *vdit =
        typemanager()->buildDIType(v->btype(), *this);
    unsigned vline = linemap()->location_line(v->location());
    dibuilder().createAutoVariable(dilb, v->name(), vfile, vline, vdit);
  }
}

void DIBuildHelper::endLexicalBlock(Bblock *block)
{
  popDIScope();
}

void DIBuildHelper::processExprInst(Bexpression *expr, llvm::Instruction *inst)
{
  Location eloc = expr->location();
  // && !linemap()->is_predeclared(eloc)) {
  if (! linemap()->is_unknown(eloc)) {
    known_locations_ += 1;
    inst->setDebugLoc(debugLocFromLocation(eloc));
  }
}

llvm::DIFile *DIBuildHelper::diFileFromLocation(Location location)
{
  std::string locfile = linemap()->location_file(location);
  llvm::StringRef locdir = llvm::sys::path::parent_path(locfile);
  llvm::StringRef locfilename = llvm::sys::path::filename(locfile);
  if (linemap()->is_predeclared(location))
    locdir = "";
#if 0
  llvm::SmallString<256> currentDir;
  llvm::sys::fs::current_path(currentDir);
  if (locdir == "" || locdir == ".")
    locdir = currentDir;
#endif
  return dibuilder().createFile(locfilename, locdir);
}

llvm::DebugLoc DIBuildHelper::debugLocFromLocation(Location loc)
{
  llvm::LLVMContext &context = typemanager()->context();
  return llvm::DILocation::get(context, linemap()->location_line(loc),
                               linemap()->location_column(loc),
                               currentDIScope());
}

llvm::DIScope *DIBuildHelper::currentDIScope()
{
  assert(diScopeStack_.size());
  return diScopeStack_.back();
}

llvm::DIScope *DIBuildHelper::popDIScope()
{
  assert(diScopeStack_.size());
  llvm::DIScope *popped = diScopeStack_.back();
  diScopeStack_.pop_back();
  return popped;
}

void DIBuildHelper::pushDIScope(llvm::DIScope *scope)
{
  assert(scope);
  diScopeStack_.push_back(scope);
}
