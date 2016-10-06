// go-linemap.cc -- LLVM implementation of Linemap.

// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "go-linemap.h"

// This class implements the Linemap interface defined by the
// frontend.

class Llvm_linemap : public Linemap
{
 public:
  Llvm_linemap()
    : Linemap(),
      in_file_(false)
  { }

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
  // Whether we are currently reading a file.
  bool in_file_;
};

Linemap* Linemap::instance_ = NULL;

// Start getting locations from a new file.

void
Llvm_linemap::start_file(const char *file_name, unsigned line_begin)
{
  go_assert(false);
  // not yet implemented
#if 0
  if (this->in_file_)
    linemap_add(line_table, LC_LEAVE, 0, NULL, 0);
  linemap_add(line_table, LC_ENTER, 0, file_name, line_begin);
  this->in_file_ = true;
#endif
}

// Stringify a location

std::string
Llvm_linemap::to_string(Location location)
{
  go_assert(false);
  // not yet implemented
  return std::string();
#if 0
  const line_map_ordinary *lmo;
  source_location resolved_location;

  // Screen out unknown and predeclared locations; produce output
  // only for simple file:line locations.
  resolved_location =
      linemap_resolve_location (line_table, location.gcc_location(),
                                LRK_SPELLING_LOCATION, &lmo);
  if (lmo == NULL || resolved_location < RESERVED_LOCATION_COUNT)
    return "";
  const char *path = LINEMAP_FILE (lmo);
  if (!path)
    return "";

  // Strip the source file down to the base file, to reduce clutter.
  std::stringstream ss;
  ss << lbasename(path) << ":" << SOURCE_LINE (lmo, location.gcc_location());
  return ss.str();
#endif
}

// Return the line number for a given location (for debugging dumps)
int
Llvm_linemap::location_line(Location loc)
{
  go_assert(false);
  // not yet implemented
  return 0;
#if 0
  return LOCATION_LINE(loc.gcc_location());
#endif
}

// Stop getting locations.

void
Llvm_linemap::stop()
{
  go_assert(false);
  // not yet implemented
#if 0
  linemap_add(line_table, LC_LEAVE, 0, NULL, 0);
  this->in_file_ = false;
#endif
}

// Start a new line.

void
Llvm_linemap::start_line(unsigned lineno, unsigned linesize)
{
  go_assert(false);
  // not yet implemented
#if 0
  linemap_line_start(line_table, lineno, linesize);
#endif
}

// Get a location.

Location
Llvm_linemap::get_location(unsigned column)
{
  go_assert(false);
  // not yet implemented
  return Location();
#if 0
  return Location(linemap_position_for_column(line_table, column));
#endif
}

// Get the unknown location.

Location
Llvm_linemap::get_unknown_location()
{
  // FIXME: check to see how clang handles this
  return Location();
}

// Get the predeclared location.

Location
Llvm_linemap::get_predeclared_location()
{
  // FIXME: check to see how clang handles this
  return Location();
}

// Return whether a location is the predeclared location.

bool
Llvm_linemap::is_predeclared(Location loc)
{
  go_assert(false);
  // not yet implemented
  return false;
#if 0
  return loc.gcc_location() == BUILTINS_LOCATION;
#endif
}

// Return whether a location is the unknown location.

bool
Llvm_linemap::is_unknown(Location loc)
{
  go_assert(false);
  // not yet implemented
  return false;
#if 0
  return loc.gcc_location() == UNKNOWN_LOCATION;
#endif
}

// Return the Linemap to use for the gcc backend.

Linemap*
go_get_linemap()
{
  return new Llvm_linemap;
}
