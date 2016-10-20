

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

# So that we can issue "make -jN" cmds in externalproject_add 
processorcount(PROCESSOR_COUNT)

# FIXME: add in URL args for the projects below.
# URL ftp://ftp.gnu.org/gnu/gmp/gmp-6.1.0.tar.bz2
# URL ftp://gcc.gnu.org/pub/gcc/infrastructure/mpfr-3.1.4.tar.bz2
# URL ftp://gcc.gnu.org/pub/gcc/infrastructure/mpc-1.0.3.tar.gz

# Open issue: should the configure cmds below be run with 
# "CC=${CMAKE_BINARY_DIR}/bin/clang"

set(MPCINSTALL ${CMAKE_CURRENT_BINARY_DIR}/external/build-mpc)
externalproject_add(libmpc
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/mpc
  DOWNLOAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/mpc
  CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${MPCINSTALL} 
  BUILD_COMMAND make -j${PROCESSOR_COUNT}
  INSTALL_COMMAND make -j${PROCESSOR_COUNT} install
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1
  )

set(MPFRINSTALL ${CMAKE_CURRENT_BINARY_DIR}/external/build-mpfr)
externalproject_add(libmpfr
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/mpfr
  DOWNLOAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/mpfr
  CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${MPFRINSTALL} 
  BUILD_COMMAND make -j${PROCESSOR_COUNT}
  INSTALL_COMMAND make -j${PROCESSOR_COUNT} install
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1
  )

set(GMPINSTALL ${CMAKE_CURRENT_BINARY_DIR}/external/build-gmp)
externalproject_add(libgmp
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/gmp
  DOWNLOAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/gmp
  CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${GMPINSTALL} 
  BUILD_COMMAND make -j${PROCESSOR_COUNT}
  INSTALL_COMMAND make -j${PROCESSOR_COUNT} install
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1
  )

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

# Add in the archives for the llvm-goparse dependencies.
target_link_libraries(llvm-goparse
  ${MPCINSTALL}/lib/libmpc.a
  ${MPFRINSTALL}/lib/libmpfr.a
  ${GMPINSTALL}/lib/libgmp.a
  )

