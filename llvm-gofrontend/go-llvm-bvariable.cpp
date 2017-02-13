//===-- go-llvm-bvariable.cpp - implementation of 'Bvariable' class ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for class Bvariable.
//
//===----------------------------------------------------------------------===//

#include "go-llvm-bvariable.h"

#include "llvm/Support/raw_ostream.h"

static void indent(llvm::raw_ostream &os, unsigned ilevel) {
  for (unsigned i = 0; i < ilevel; ++i)
    os << " ";
}

void Bvariable::dump()
{
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os, 0, nullptr, false);
  std::cerr << os.str();
}

void Bvariable::srcDump(Linemap *linemap)
{
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os, 0, linemap, false);
  std::cerr << os.str();
}

void Bvariable::osdump(llvm::raw_ostream &os, unsigned ilevel,
                       Linemap *linemap, bool terse) {
  if (! terse) {
    if (linemap) {
      indent(os, ilevel);
      os << linemap->to_string(location_) << "\n";
    }
  }
  indent(os, ilevel);
  const char *whtag = nullptr;
  switch(which_) {
    case ParamVar: whtag = "param"; break;
    case GlobalVar: whtag = "global"; break;
    case LocalVar: whtag = "local"; break;
    case BlockVar: whtag = "block"; break;
    case ErrorVar: whtag = "error"; break;
    default: whtag = "<unknownwhichval>"; break;
  }
  os << whtag << " var '" << name_ << "' type: ";
  type_->osdump(os, 0);
  os << "\n";
}
