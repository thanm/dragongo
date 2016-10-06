// go-location.h -- GCC specific Location declaration.   -*- C++ -*-

// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef GO_LOCATION_H
#define GO_LOCATION_H

#include "go-system.h"

#include "llvm/IR/DebugLoc.h"

//typedef int source_location;
//typedef int location;

namespace llvm {
  class DILocation;
}

// A location in an input source file.

class Location
{
 public:
  Location()
      : debug_loc_()
  { }

  explicit Location(llvm::DebugLoc loc)
    : debug_loc_(loc)
  { }

  llvm::DebugLoc
  debug_location() const
  { return this->debug_loc_; }

 private:
  llvm::DebugLoc debug_loc_;
};

// The Go frontend requires the ability to compare Locations.
extern bool operator<(Location loca, Location locb);
extern bool operator==(Location loca, Location locb);

#endif // !defined(GO_LOCATION_H)
