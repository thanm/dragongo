
include(ExternalProject)
include(ProcessorCount)
include(LLVMExternalProjectUtils)

add_subdirectory(llvm-gofrontend)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/gofrontend/go)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/llvm-gofrontend)

processorcount(PROCESSOR_COUNT)

#externalproject_add(libgmp
# URL ftp://ftp.gnu.org/gnu/gmp/gmp-6.1.0.tar.bz2
# URL ftp://gcc.gnu.org/pub/gcc/infrastructure/mpfr-3.1.4.tar.bz2

externalproject_add(libgmp
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/gmp
  DOWNLOAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/gmp
  CONFIGURE_COMMAND <SOURCE_DIR>/configure "CC=${CMAKE_BINARY_DIR}/bin/clang"
  BUILD_COMMAND make -j${PROCESSOR_COUNT}
  INSTALL_COMMAND ""
  LOG_CONFIGURE 1
  LOG_BUILD 1
  )

externalproject_add(libmpfr
  DEPENDS libgmp
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/mpfr
  DOWNLOAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/mpfr
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

target_link_libraries(llvm-goparse libmpfr.a libmpc.a libgmp.a)


