//===-- go-location.cpp ---------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Location helper functions.
//

#include "go-location.h"

#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DebugInfoMetadata.h"

static bool operator==(const llvm::DIFile &a, const llvm::DIFile &b)
{
  llvm::StringRef fa = a.getFilename();
  llvm::StringRef fb = b.getFilename();
  if (!fa.equals(fb))
    return false;
  llvm::StringRef da = a.getDirectory();
  llvm::StringRef db = b.getDirectory();
  if (!da.equals(db))
    return false;
  return true;
}

static bool operator<(const llvm::DIFile &a, const llvm::DIFile &b)
{
  llvm::StringRef fa = a.getFilename();
  llvm::StringRef fb = b.getFilename();
  int cmpf = fa.compare(fb);
  if (cmpf != 0)
    return cmpf < 0;
  llvm::StringRef da = a.getDirectory();
  llvm::StringRef db = b.getDirectory();
  return da.compare(db) < 0;
}

static bool operator<(const llvm::DILocation &a, const llvm::DILocation &b)
{
  if (a.getLine() != b.getLine())
    return a.getLine() < b.getLine();
  if (a.getColumn() != b.getColumn())
    return a.getColumn() < b.getColumn();
  llvm::DIFile &fa = *a.getFile();
  llvm::DIFile &fb = *b.getFile();
  return fa < fb;
}

static bool operator==(const llvm::DILocation &a, const llvm::DILocation &b)
{
  return (a.getLine() == b.getLine() &&
          a.getColumn() == b.getColumn() &&
          (*a.getFile()) == (*b.getFile()));
}

bool operator<(Location loca, Location locb)
{
  llvm::DebugLoc dla = loca.debug_location();
  llvm::DebugLoc dlb = locb.debug_location();
  llvm::DILocation &dia = *dla.get();
  llvm::DILocation &dib = *dlb.get();
  return dia < dib;
}

bool operator==(Location loca, Location locb)
{
  llvm::DebugLoc dla = loca.debug_location();
  llvm::DebugLoc dlb = locb.debug_location();
  llvm::DILocation &dia = *dla.get();
  llvm::DILocation &dib = *dlb.get();
  return dia == dib;
}
