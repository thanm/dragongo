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
// Line/column pairs are ULEB128-encoded and stored in a large
// array; we then hand out the offset of the encoded pair as a
// handle for use in the Location class. The source file for a range
// of pairs is stored in a separate table.
//
// This implementation doesn't hash or common line/column pairs, meaning
// that there can be some redundancy in the encodings.
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

  // File/line/column container
  struct FLC {
    unsigned fidx;
    unsigned line;
    unsigned column;
    FLC(unsigned f, unsigned l, unsigned c)
        : fidx(f), line(l), column(c)
    { }
  };

  // Stores the file ID associated with a range of handle
  struct Segment {
    // Lo/hi handles in this range
    unsigned lo, hi;
    // File id for this collection of handles
    unsigned fidx;

    Segment(unsigned low, unsigned high, unsigned fileid)
        : lo(low), hi(high), fidx(fileid) { }

    static bool cmp(const struct Segment &a1, const struct Segment &a2) {
      return a1.hi < a2.hi;
    }
  };

  unsigned add_encoded_location(const FLC &flc);
  FLC decode_location(unsigned handle);
  void dump();

 private:
  std::vector<const char *> files_;
  std::map<const char *, unsigned> fmap_;
  std::vector<Segment> segments_;
  std::vector<unsigned char> encoded_locations_;
  unsigned unknown_fidx_;
  unsigned current_fidx_;
  unsigned current_line_;
  unsigned predeclared_handle_;
  unsigned unknown_handle_;
  unsigned firsthandle_;
  unsigned lasthandle_;
  unsigned lookups_;

  // Whether we are currently reading a file.
  bool in_file_;
};

// Main hook for linemap creation
extern Linemap *go_get_linemap();

#endif // !defined(GO_LLVM_LINEMAP_H)
