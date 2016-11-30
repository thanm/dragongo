// go-linemap.cc -- LLVM implementation of Linemap.

// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "go-location.h"
#include "go-llvm-linemap.h"

#include "llvm/Support/LEB128.h"

#include <sstream>

Linemap* Linemap::instance_ = NULL;

Llvm_linemap::Llvm_linemap()
    : Linemap()
    , unknown_fidx_(0)
    , current_fidx_(0xffffffff)
    , current_line_(0xffffffff)
    , predeclared_handle_(1)
    , unknown_handle_(0)
    , lookups_(0)
    , hits_(0)
    , collisions_(0)
    , in_file_(false)
{
  files_.push_back("");
  unknown_handle_ = add_encoded_location(FLC(unknown_fidx_, 0, 0));
  predeclared_handle_ = add_encoded_location(FLC(unknown_fidx_, 1, 1));
}

Llvm_linemap::~Llvm_linemap()
{
  std::cerr << "Stats: " << statistics() << "\n";
}

unsigned Llvm_linemap::add_encoded_location(const FLC &flc)
{
  unsigned char buf[32];
  unsigned l1 = llvm::encodeULEB128(flc.line, buf);
  unsigned l2 = llvm::encodeULEB128(flc.column, &buf[l1]);
  unsigned l3 = llvm::encodeULEB128(flc.fidx, &buf[l2]);
  unsigned tot = l1 + l2 + l3;
  unsigned handle = encoded_locations_.size();
  for (unsigned i = 0; i < tot; ++i)
    encoded_locations_.push_back(buf[i]);
  return handle;
}

Llvm_linemap::FLC Llvm_linemap::read_encoded_location(unsigned handle)
{
  FLC rval(0, 0, 0);
  assert(handle < encoded_locations_.size());
  unsigned char *lptr = &encoded_locations_[handle];
  unsigned c1;
  rval.line = llvm::decodeULEB128(lptr, &c1);
  unsigned c2;
  rval.column = llvm::decodeULEB128(&lptr[c1], &c2);
  unsigned c3;
  rval.fidx = llvm::decodeULEB128(&lptr[c2], &c3);
  assert(handle + c1 + c2 + c3 <= encoded_locations_.size());
  return rval;
}

// Start getting locations from a new file.

void
Llvm_linemap::start_file(const char *file_name, unsigned line_begin)
{
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

  FLC flc = read_encoded_location(location.handle());
  const char *path = files_[flc.fidx];
  std::stringstream ss;
  ss << lbasename(path) << ":" << flc.line;
  return ss.str();
}

// Return the line number for a given location (for debugging dumps)
int
Llvm_linemap::location_line(Location loc)
{
  FLC flc = read_encoded_location(loc.handle());
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
  FLC flc(current_fidx_, current_line_, column);

  // Seen before?
  unsigned hash = flc.hash();
  auto it = hmap_.find(hash);
  lookups_++;
  if (it != hmap_.end()) {
    hits_++;
    FLC existing = read_encoded_location(it->second);
    if (existing.equal(flc))
      return Location(it->second);
    else
      collisions_++;
  }

  // Add new entry
  unsigned handle = add_encoded_location(flc);
  hmap_[hash] = handle;
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

std::string
Llvm_linemap::statistics()
{
  std::stringstream ss;
  ss << "accesses: " << lookups_
     << " hits: " << hits_
     << " collisions: " << collisions_
     << " files: " << files_.size()
     << " locmem: " << encoded_locations_.size();
  return ss.str();
}

// Return the Linemap to use for the backend.

Linemap*
go_get_linemap()
{
  return new Llvm_linemap;
}
