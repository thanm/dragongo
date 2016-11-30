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

// Implementation notes:
//
// File/line/column triples are ULEB128-encoded and stored in a large
// array; we then hand out the offset of the encoded triple as a
// handle for use in the Location class.
//
// In this implementation, incoming FLC triples are hashed, and when a
// new FLC triple is added to the linemap we record an entry to a map
// recording the FLC for that hash. When looking up a new location, we
// consult the hash table and return the existing handle if its
// encoded FLC match the incoming FLC.  There can be hash collision,
// however, meaning that we may return two different handles for
// the same FLC triple requested at different times in the compilation.
//
// It may be more efficient in the long run to do away with the hashing
// and just return a new handle on each lookup, since hit rates for the
// cache seem to be pretty low as far as I can tell.
//

#ifndef GO_LLVM_LINEMAP_H
#define GO_LLVM_LINEMAP_H

#include "go-linemap.h"

class Llvm_linemap : public Linemap
{
 public:
  Llvm_linemap();
  ~Llvm_linemap();

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
    unsigned hash() { return (line << 15) | (column << 6) | fidx; }
    bool equal(const FLC other) {
      return (other.line == line &&
              other.column == column &&
              other.fidx == fidx);
    }
  };

  unsigned add_encoded_location(const FLC &flc);
  FLC read_encoded_location(unsigned handle);

 private:
  std::vector<const char *> files_;
  std::vector<unsigned char> encoded_locations_;
  std::map<const char *, unsigned> fmap_;
  std::map<unsigned, unsigned> hmap_;
  unsigned unknown_fidx_;
  unsigned current_fidx_;
  unsigned current_line_;
  unsigned predeclared_handle_;
  unsigned unknown_handle_;
  unsigned lookups_;
  unsigned hits_;
  unsigned collisions_;

  // Whether we are currently reading a file.
  bool in_file_;
};

// Main hook for linemap creation
extern Linemap *go_get_linemap();

#endif // !defined(GO_LLVM_LINEMAP_H)
