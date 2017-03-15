// go-llvm-diagnostics.cc -- LLVM implementation of go diagnostics interface.

// Copyright 2016 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "go-system.h"
#include "go-diagnostics.h"
#include "go-llvm-linemap.h"

#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/raw_ostream.h"

// Notes to self:
// - low-level diagnostics that crop up during LLVM back end
//   processing (for example, incorrect use of inline assembly)
//   are handled via LLVMContext::diagnose(const DiagnosticInfo &DI).
// - this seems inappropriate for pure front-end type errors, however,
//   since diagnostics are designed to be filtered or suppressed in
//   many cases.

static unsigned error_count = 0;

bool go_be_saw_errors()
{
  return error_count > 0;
}

void
go_be_error(const std::string& errmsg)
{
  error_count += 1;
  llvm::errs() << errmsg << '\n';
}

void
go_be_sorry(const std::string& errmsg)
{
  error_count += 1;
  llvm::errs() << errmsg << '\n';
}

static void emitLoc(const Location loc)
{
  Llvm_linemap *lm = go_get_llvm_linemap();
  if (loc == lm->get_predeclared_location())
    llvm::errs() << "<predeclared>";
  else if (loc == lm->get_unknown_location())
    llvm::errs() << "<unknown>";
  else {
    llvm::errs() << lm->location_file(loc) << ":"
                 << lm->location_line(loc) << ":"
                 << lm->location_column(loc);
  }
}

void
go_be_error_at(const Location location, const std::string& errmsg)
{
  error_count += 1;
  emitLoc(location);
  llvm::errs() << ": " << errmsg << '\n';
}


void
go_be_warning_at(const Location location,
                 int opt, const std::string& warningmsg)
{
  emitLoc(location);
  llvm::errs() << ": " << warningmsg << '\n';
}

void
go_be_fatal_error(const Location location,
                  const std::string& fatalmsg)
{
  emitLoc(location);
  llvm::errs() << ": " << fatalmsg << '\n';
  abort();
}

void
go_be_inform(const Location location,
             const std::string& infomsg)
{
  emitLoc(location);
  llvm::errs() << ": " << infomsg << '\n';
}

void
go_be_get_quotechars(const char** open_qu, const char** close_qu)
{
  // FIXME: base this on locale
  *open_qu = "'";
  *close_qu = "'";
}
