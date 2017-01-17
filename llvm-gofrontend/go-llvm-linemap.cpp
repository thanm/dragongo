// go-linemap.cc -- LLVM implementation of Linemap.

// Copyright 2016 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "go-location.h"
#include "go-llvm-linemap.h"

#include "llvm/Support/LEB128.h"

#include <sstream>

static constexpr unsigned NoHandle = 0xffffffff;

Linemap* Linemap::instance_ = NULL;

Llvm_linemap::Llvm_linemap()
    : Linemap()
    , unknown_fidx_(0)
    , current_fidx_(0)
    , current_line_(0xffffffff)
    , predeclared_handle_(1)
    , unknown_handle_(0)
    , firsthandle_(NoHandle)
    , lasthandle_(NoHandle)
    , lookups_(0)
    , in_file_(false)
{
  files_.push_back("");
  unknown_handle_ = add_encoded_location(FLC(unknown_fidx_, 0, 0));
  predeclared_handle_ = add_encoded_location(FLC(unknown_fidx_, 1, 1));
  firsthandle_ = unknown_handle_;
  lasthandle_ = predeclared_handle_;
}

Llvm_linemap::~Llvm_linemap()
{
}

unsigned Llvm_linemap::add_encoded_location(const FLC &flc)
{
  unsigned char buf[64];
  unsigned l1 = llvm::encodeULEB128(flc.line, buf);
  unsigned l2 = llvm::encodeULEB128(flc.column, &buf[l1]);
  unsigned tot = l1 + l2;
  unsigned handle = encoded_locations_.size();
  for (unsigned i = 0; i < tot; ++i)
    encoded_locations_.push_back(buf[i]);
  return handle;
}

Llvm_linemap::FLC Llvm_linemap::decode_location(unsigned handle)
{
  assert(handle < encoded_locations_.size());

  // Read line/col from encoded array
  FLC rval(0, 0, 0);
  unsigned char *lptr = &encoded_locations_[handle];
  unsigned c1;
  rval.line = llvm::decodeULEB128(lptr, &c1);
  unsigned c2;
  rval.column = llvm::decodeULEB128(&lptr[c1], &c2);
  assert(handle + c1 + c2 <= encoded_locations_.size());

  // Determine file ID by looking up handle in segment table
  Segment s(handle, handle, 0);
  auto it = std::lower_bound(segments_.begin(), segments_.end(), s,
                             Segment::cmp);
  rval.fidx = (it == segments_.end() ? current_fidx_ : it->fidx);
  return rval;
}

// Start getting locations from a new file.

void
Llvm_linemap::start_file(const char *file_name, unsigned line_begin)
{
  if (firsthandle_ != NoHandle) {
    // Close out the current segment
    segments_.push_back(Segment(firsthandle_, lasthandle_, current_fidx_));
  }
  firsthandle_ = NoHandle;
  lasthandle_ = NoHandle;

  // Locate the file in the file table, adding new entry if needed
  auto it = fmap_.find(file_name);
  unsigned fidx = files_.size();
  if (it != fmap_.end())
    fidx = it->second;
  else {
    files_.push_back(file_name);
    fmap_[file_name] = fidx;
  }
  current_fidx_ = fidx;
  current_line_ = line_begin;
  in_file_ = true;
}

// Stringify a location

std::string
Llvm_linemap::to_string(Location location)
{
  if (location.handle() == predeclared_handle_ ||
      location.handle() == unknown_handle_)
    return "";

  FLC flc = decode_location(location.handle());
  const char *path = files_[flc.fidx];
  std::stringstream ss;
  ss << lbasename(path) << ":" << flc.line;
  return ss.str();
}

// Return the line number for a given location (for debugging dumps)

int
Llvm_linemap::location_line(Location loc)
{
  FLC flc = decode_location(loc.handle());
  return flc.line;
}

// Stop getting locations.

void
Llvm_linemap::stop()
{
  in_file_ = false;
}

// Start a new line.

void
Llvm_linemap::start_line(unsigned lineno, unsigned linesize)
{
  current_line_ = lineno;
}

// Get a location.

Location
Llvm_linemap::get_location(unsigned column)
{
  assert(in_file_);

  lookups_++;
  FLC flc(current_fidx_, current_line_, column);
  unsigned handle = add_encoded_location(flc);
  if (firsthandle_ == NoHandle)
    firsthandle_ = handle;
  lasthandle_ = handle;
  return Location(handle);
}

// Get the unknown location.

Location
Llvm_linemap::get_unknown_location()
{
  return Location(unknown_handle_);
}

// Get the predeclared location.

Location
Llvm_linemap::get_predeclared_location()
{
  return Location(predeclared_handle_);
}

// Return whether a location is the predeclared location.

bool
Llvm_linemap::is_predeclared(Location loc)
{
  return loc.handle() == predeclared_handle_;
}

// Return whether a location is the unknown location.

bool
Llvm_linemap::is_unknown(Location loc)
{
  return loc.handle() == unknown_handle_;
}

std::string Llvm_linemap::statistics()
{
  std::stringstream ss;
  ss << "accesses: " << lookups_
     << " files: " << files_.size()
     << " segments: " << segments_.size()
     << " locmem: " << encoded_locations_.size();
  return ss.str();
}

void Llvm_linemap::dump()
{
  std::cerr << "Files:\n";
  for (unsigned ii = 0; ii < files_.size(); ++ii)
    std::cerr << ii << ": " << files_[ii] << "\n";
  std::cerr << "Segments:\n";
  for (unsigned ii = 0; ii < segments_.size(); ++ii) {
    unsigned lo = segments_[ii].lo;
    unsigned hi = segments_[ii].hi;
    std::cerr << ii << ": [" << lo << "," << hi << "] lo='"
              << to_string(Location(lo)) << "' hi='"
              << to_string(Location(hi)) << "'\n";
  }
  std::cerr << "firsthandle: " << firsthandle_ << "\n";
  std::cerr << "lasthandle: " << lasthandle_ << "\n";
}

// Return the Linemap to use for the backend.

Linemap*
go_get_linemap()
{
  return new Llvm_linemap;
}
