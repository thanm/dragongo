// go-llvm-diagnostics.cc -- LLVM implementation of go diagnostics interface.

// Copyright 2016 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "go-system.h"
#include "go-diagnostics.h"

#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/raw_ostream.h"


//
// Notes to self:
// - low-level diagnostics that crop up during LLVM back end
//   processing (for example, incorrect use of inline assembly)
//   are handled via LLVMContext::diagnose(const DiagnosticInfo &DI).
// - this seems inappropriate for pure front-end type errors, however,
//   since diagnostics are designed to be filtered or suppressed in
//   many cases.
//

void
go_be_error(const std::string& errmsg)
{
  // record or report error in some way?
  llvm::errs() << errmsg << '\n';
}

void
go_be_sorry(const std::string& errmsg)
{
  // record or report error in some way?
  llvm::errs() << errmsg << '\n';
}

void
go_be_error_at(const Location location, const std::string& errmsg)
{
  // FIXME: unpack DebugLoc from location and report
  llvm::errs() << errmsg << '\n';
}


void
go_be_warning_at(const Location location,
                 int opt, const std::string& warningmsg)
{
  // FIXME: unpack DebugLoc from location and report
  llvm::errs() << warningmsg << '\n';
}

void
go_be_fatal_error(const Location location,
                  const std::string& fatalmsg)
{
  // FIXME: unpack DebugLoc from location and report
  llvm::errs() << fatalmsg << '\n';
  abort();
}

void
go_be_inform(const Location location,
             const std::string& infomsg)
{
  // FIXME: unpack DebugLoc from location and report
  llvm::errs() << infomsg << '\n';
}

void
go_be_get_quotechars(const char** open_qu, const char** close_qu)
{
  // FIXME: base this on locale
  *open_qu = "'";
  *close_qu = "'";
}
