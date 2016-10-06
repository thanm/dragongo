
include(ExternalProject)
include(ProcessorCount)

add_subdirectory(llvm-gofrontend)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/gofrontend/go)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/llvm-gofrontend)

processorcount(PROCESSOR_COUNT)

# Removed from below:
# DEPENDS clang

externalproject_add(libgmp
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

# LLVMCppGoFrontEnd

set(LLVM_LINK_COMPONENTS
  CppGoFrontEnd
  CodeGen
  Core
  IRReader
  MC
  Support
  Target
  Object
  Support
  )

add_llvm_tool(llvm-goparse
  llvm-goparse.cpp
  )


