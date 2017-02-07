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
#include <iostream>

#include "go-llvm-diagnostics.h"
#include "go-c.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"

// Size of archive member header in bytes
#define ARCHIVE_MEMBER_HEADER_SIZE 60

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
  return go_be_saw_errors();
}

// Called by the Go frontend proper if the unsafe package was imported.
//
// FIXME: make a determination about whether we need to run TBAA
// in a different way based on this information.

void
go_imported_unsafe (void)
{
  // FIXME
  std::cerr << "FIXME: go_imported_unsafe not yet implemented\n";
}

static const char *
readExportDataFromObject(llvm::object::ObjectFile *obj,
                         int *perr,
                         char **pbuf,
                         size_t *plen)
{
  // Walk sections
  for (llvm::object::section_iterator si = obj->section_begin(),
           se = obj->section_end(); si != se; ++si) {
    llvm::object::SectionRef sref = *si;
    llvm::StringRef sname;
    std::error_code error = sref.getName(sname);
    if (error)
      break;
    if (sname == GO_EXPORT_SECTION_NAME) {
      // Extract section of interest
      llvm::StringRef bytes;
      if (sref.getContents(bytes)) {
        *perr = errno;
        return "get section contents";
      }
      char *buf = new char[bytes.size()];
      if (! buf) {
        *perr = errno;
        return "malloc";
      }
      memcpy(buf, bytes.data(), bytes.size());
      *pbuf = buf;
      *plen = bytes.size();
      return nullptr;
    }
  }
  return nullptr;
}

static const char *
readExportDataFromArchive(llvm::object::Archive *archive,
                          off_t offset,
                          int *perr,
                          char **pbuf,
                          size_t *plen)
{
  llvm::Error err = llvm::Error::success();

  // The gofrontend archive reader passes in an offset that points
  // past the the archive member header, whereas the llvm::object::Archive
  // code considers "child offset" to be the start of the region in the
  // archive at the point of the member header. Adjust accordingly.
  assert(!offset || offset > ARCHIVE_MEMBER_HEADER_SIZE);
  uint64_t uoffset = static_cast<uint64_t>(offset) -
      ARCHIVE_MEMBER_HEADER_SIZE;
  for (auto &child : archive->children(err)) {
    if (err)
      return "unable to open as archive";
    if (child.getChildOffset() != uoffset)
      continue;
    // found.
    llvm::Expected<std::unique_ptr<llvm::object::Binary>> childOrErr =
        child.getAsBinary();
    if (!childOrErr)
      return "archive member is not an object";
    llvm::object::ObjectFile *o =
        llvm::dyn_cast<llvm::object::ObjectFile>(&*childOrErr.get());
    if (o)
      return readExportDataFromObject(o, perr, pbuf, plen);
  }

  if (err)
    return "unable to open as archive";
  else
    return nullptr;
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
  *pbuf = NULL;
  *plen = 0;

  // Create memory buffer for this file descriptor
  auto BuffOrErr = llvm::MemoryBuffer::getOpenFile(fd, "", -1);
  if (! BuffOrErr)
    return nullptr; // ignore this error
  std::unique_ptr<llvm::MemoryBuffer> Buffer = std::move(BuffOrErr.get());

  // Examine buffer as binary
  llvm::Expected<std::unique_ptr<llvm::object::Binary>> BinOrErr =
      llvm::object::createBinary(Buffer->getMemBufferRef());
  if (!BinOrErr)
    return nullptr; // also ignore here
  std::unique_ptr<llvm::object::Binary> Binary = std::move(BinOrErr.get());

  // Examine binary as archive
  if (llvm::object::Archive *a =
      llvm::dyn_cast<llvm::object::Archive>(Binary.get()))
    return readExportDataFromArchive(a, offset, perr, pbuf, plen);

  // Examine binary as object
  llvm::object::ObjectFile *o =
      llvm::dyn_cast<llvm::object::ObjectFile>(Binary.get());
  if (o)
    return readExportDataFromObject(o, perr, pbuf, plen);

  return nullptr;
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
