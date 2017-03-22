//===-- go-llvm-linemap.h - Linemap class public interfaces  --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// LLVM/go implementation of Linemap class.
//
//===----------------------------------------------------------------------===//

// Implementation notes:
//
// Line/column pairs are ULEB128-encoded and stored in a large array;
// we then hand out the offset of the encoded pair as a handle for use
// in the Location class. The source file for a range of pairs is
// stored in a separate table.  This implementation doesn't hash or
// common line/column pairs, meaning that there can be some redundancy
// in the encodings.
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

  std::string statistics();

  int
  location_line(Location);

  std::string
  location_file(Location);

  unsigned
  location_column(Location);

  // Return first file mentioned in a returned location. This will
  // be the empty string if no files have been started.
  std::string
  get_initial_file();

  Location
  get_predeclared_location();

  Location
  get_unknown_location();

  bool
  is_predeclared(Location);

  bool
  is_unknown(Location);

  static Llvm_linemap*
  instance();

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
    // lo/hi handles in this range
    unsigned lo, hi;
    // file id for this collection of handles
    unsigned fidx;

    Segment(unsigned low, unsigned high, unsigned fileid)
        : lo(low), hi(high), fidx(fileid) { }

    static bool cmp(const struct Segment &a1, const struct Segment &a2) {
      return a1.hi < a2.hi;
    }
  };

  // Given a file/line/column triple, add an entry to the linemap for
  // the triple and return a handle for it.
  unsigned add_encoded_location(const FLC &flc);

  // Given a handle, return the associated file/line/column triple.
  FLC decode_location(unsigned handle);

  // Debugging
  void dump();
  void dumpLocation(Location loc);
  void dumpHandle(unsigned handle);

 private:
  // Source files we've seen so far.
  std::vector<std::string> files_;
  // Maps source file to index in the files_ array.
  std::map<std::string, unsigned> fmap_;
  // Sorted table of segments, used to record file id for ranges of handles.
  std::vector<Segment> segments_;
  // Array of ULEB-encoded line/col pairs.
  std::vector<unsigned char> encoded_locations_;
  // Predefined "unknown file" file ID.
  unsigned unknown_fidx_;
  // Predefined file ID for predeclared or builtin locations.
  unsigned builtin_fidx_;
  // Current file ID (most recent file passed to start_file)
  unsigned current_fidx_;
  // Current line.
  unsigned current_line_;
  // Special handle for predeclared location.
  unsigned builtin_handle_;
  // Special handle for unknown location.
  unsigned unknown_handle_;
  // First handle in the segment we are building (not yet in segment table).
  // May be set to NoHandle if there are no locations in the segment yet.
  unsigned firsthandle_;
  // Last handle in the segment we are building (not yet in segment table).
  // May be set to NoHandle if there are no locations in the segment yet.
  unsigned lasthandle_;
  // Number of lookups made into the linemap.
  unsigned lookups_;
  // Whether we are currently reading a file.
  bool in_file_;
};

// Main hook for linemap creation
extern Linemap *go_get_linemap();

// For unit testing and error reporting
extern Llvm_linemap *go_get_llvm_linemap();

#endif // !defined(GO_LLVM_LINEMAP_H)
