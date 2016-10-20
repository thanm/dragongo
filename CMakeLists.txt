

# Notes:
#
# The code in gofrontend depends on functions from GMP, MPC, and MPFR; these libraries are currently handled via the cmake "ExternalProject" utility
# 

include(ExternalProject)
include(ProcessorCount)
include(LLVMExternalProjectUtils)

# Subdir for gofrontend itself and for the middle layer that translates
# Backend method calls into LLVM IR.
add_subdirectory(llvm-gofrontend)

# Subdir for unit tests that test the go middle layer
add_subdirectory(unittests)

processorcount(PROCESSOR_COUNT)

#externalproject_add(libgmp
# URL ftp://ftp.gnu.org/gnu/gmp/gmp-6.1.0.tar.bz2
# URL ftp://gcc.gnu.org/pub/gcc/infrastructure/mpfr-3.1.4.tar.bz2
# URL ftp://gcc.gnu.org/pub/gcc/infrastructure/mpc-1.0.3.tar.gz

externalproject_add(libmpc
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/mpc
  DOWNLOAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/mpc
  CONFIGURE_COMMAND <SOURCE_DIR>/configure "CC=${CMAKE_BINARY_DIR}/bin/clang"
  BUILD_COMMAND make -j${PROCESSOR_COUNT}
  INSTALL_COMMAND ""
  LOG_CONFIGURE 1
  LOG_BUILD 1
  )
set(MPCLIBDIR ${CMAKE_CURRENT_BINARY_DIR}/external/mpc/src/.libs)

externalproject_add(libmpfr
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/mpfr
  DOWNLOAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/mpfr
  CONFIGURE_COMMAND <SOURCE_DIR>/configure "CC=${CMAKE_BINARY_DIR}/bin/clang"
  BUILD_COMMAND make -j${PROCESSOR_COUNT}
  INSTALL_COMMAND ""
  LOG_CONFIGURE 1
  LOG_BUILD 1
  )
set(MPFRLIBDIR ${CMAKE_CURRENT_BINARY_DIR}/external/mpfr/src/.libs)

externalproject_add(libgmp
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/gmp
  DOWNLOAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/gmp
  CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/external/build-gmp "CC=${CMAKE_BINARY_DIR}/bin/clang"
  BUILD_COMMAND make -j${PROCESSOR_COUNT}
  INSTALL_COMMAND make install
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1
  )
set(GMPLIBDIR ${CMAKE_CURRENT_BINARY_DIR}/external/build-gmp/lib)

# Things we need to link into llvm-goparse

set(LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
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

# Include directories needed by llvm-goparse

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/gofrontend/go)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/llvm-gofrontend)

# The llvm-goparse executable itself

add_llvm_tool(llvm-goparse
  llvm-goparse.cpp
  )

# Record the fact that llvm-goparse depends on these libs

add_dependencies(llvm-goparse libmpfr libmpc libgmp)

# Add in the archives for the llvm-goparse dependencies
# FIXME: this is a hacky way of doing this; it would be better
# to 

target_link_libraries(llvm-goparse ${MPCLIBDIR}/libmpc.a ${MPFRLIBDIR}/libmpfr.a ${GMPLIBDIR}/libgmp.a)

