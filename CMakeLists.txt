
include(ExternalProject)
include(ProcessorCount)

add_llvm_library(LLVMCppGoFrontEnd
gofrontend/go/ast-dump.cc
gofrontend/go/escape.cc
gofrontend/go/export.cc
gofrontend/go/expressions.cc
gofrontend/go/go.cc
gofrontend/go/gogo.cc
gofrontend/go/go-optimize.cc
gofrontend/go/import-archive.cc
gofrontend/go/import.cc
gofrontend/go/lex.cc
gofrontend/go/parse.cc
gofrontend/go/runtime.cc
gofrontend/go/statements.cc
gofrontend/go/types.cc
gofrontend/go/unsafe.cc
go-sha1.cpp

DEPENDS gmp
  )

include_directories(${CMAKE_BINARY_DIR}/external/gmp)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/gofrontend/go)

processorcount(PROCESSOR_COUNT)

# Removed from below:
# DEPENDS clang

externalproject_add(gmp
  URL ftp://ftp.gnu.org/gnu/gmp/gmp-6.1.0.tar.bz2
  DOWNLOAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/gmp
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/gmp
  CONFIGURE_COMMAND <SOURCE_DIR>/configure "CC=${CMAKE_BINARY_DIR}/bin/clang"
  BUILD_COMMAND make -j${PROCESSOR_COUNT}
  INSTALL_COMMAND ""
  LOG_CONFIGURE 1
  LOG_BUILD 1
  )
