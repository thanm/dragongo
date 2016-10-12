//===-- go-backend.cpp - backend specific go utility routines -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Backend-specific helper routines invoked by the go frontend.
//
//===----------------------------------------------------------------------===//

#include "llvm-includes.h"
#include <ctype.h>

#ifndef GO_EXPORT_SEGMENT_NAME
#define GO_EXPORT_SEGMENT_NAME "__GNU_GO"
#endif

/* The section name we use when reading and writing export data.  */

#ifndef GO_EXPORT_SECTION_NAME
#define GO_EXPORT_SECTION_NAME ".go_export"
#endif

/* Return whether or not we've reported any errors.  */

bool
saw_errors (void)
{
  // FIXME
  assert(false && "saw_errors not yet implemented");
  return false;
}

// Called by the Go frontend proper if the unsafe package was imported.
// Implies that type-based aliasing is no longer safe.

void
go_imported_unsafe (void)
{
  // FIXME
  assert(false && "go_imported_unsafe not yet implemented");
}

/* This is called by the Go frontend proper to add data to the
   section containing Go export data.  */

void
go_write_export_data (const char *bytes, unsigned int size)
{
  // FIXME
  assert(false && "go_write_export_data not yet implemented");
}

/* The go_read_export_data function is called by the Go frontend
   proper to read Go export data from an object file.  FD is a file
   descriptor open for reading.  OFFSET is the offset within the file
   where the object file starts; this will be 0 except when reading an
   archive.  On success this returns NULL and sets *PBUF to a buffer
   allocated using malloc, of size *PLEN, holding the export data.  If
   the data is not found, this returns NULL and sets *PBUF to NULL and
   *PLEN to 0.  If some error occurs, this returns an error message
   and sets *PERR to an errno value or 0 if there is no relevant
   errno.  */

const char *
go_read_export_data (int fd, off_t offset, char **pbuf, size_t *plen,
                     int *perr)
{
  // FIXME
  assert(false && "go_read_export_data not yet implemented");
  return "";
}

const char *lbasename(const char *path)
{
  // TODO: add windows support
  const char *cur, *rval = path;

  for (cur = path; *cur; cur++)
    if (*cur == '/')
      rval = cur + 1;

  return rval;
}

const char *xstrerror(int e)
{
  static char unknown_ebuf[128];
  const char *se = strerror(e);
  if (se)
    return se;
  sprintf(unknown_ebuf, "unknown error #%d", e);
  se = unknown_ebuf;
  return se;
}

bool IS_DIR_SEPARATOR(char c)
{
  // TODO: windows support
  return c == '/';
}

extern bool ISXDIGIT(char c)
{
  // TODO: update if better locale support added
  return isxdigit(c);
}
