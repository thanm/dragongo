//===-- go-llvm-linemap.h - Linemap class public interfaces  --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// LLVM/go implementation of Linemap class. This class is exposed
// primarily for unit testing.
//
//===----------------------------------------------------------------------===//

#ifndef GO_LLVM_LINEMAP_H
#define GO_LLVM_LINEMAP_H

#include "go-linemap.h"

class Llvm_linemap : public Linemap
{
 public:
  Llvm_linemap();

  void
  start_file(const char* file_name, unsigned int line_begin);

  void
  start_line(unsigned int line_number, unsigned int line_size);

  Location
  get_location(unsigned int column);

  void
  stop();

  std::string
  to_string(Location);

  int
  location_line(Location);

  std::string statistics();

 protected:
  Location
  get_predeclared_location();

  Location
  get_unknown_location();

  bool
  is_predeclared(Location);

  bool
  is_unknown(Location);

 private:
  struct FLC {
    unsigned fidx;
    unsigned line;
    unsigned column;
    FLC(unsigned f, unsigned l, unsigned c)
        : fidx(f), line(l), column(c)
    { }
    unsigned hash() { return (fidx << 10) + (line << 5) + column; }
    bool equal(const FLC other) {
      return (other.line == line &&
              other.column == column &&
              other.fidx == fidx);
    }
  };

 private:
  std::vector<const char *> files_;
  std::vector<FLC> locations_;
  std::map<const char *, unsigned> fmap_;
  std::map<unsigned, unsigned> hmap_;
  unsigned unknown_fidx_;
  unsigned current_fidx_;
  unsigned current_line_;
  unsigned predeclared_handle_;
  unsigned unknown_handle_;
  unsigned lookups_;
  unsigned hits_;

  // Whether we are currently reading a file.
  bool in_file_;
};

// Main hook for linemap creation
extern Linemap *go_get_linemap();

#endif // !defined(GO_LLVM_LINEMAP_H)
