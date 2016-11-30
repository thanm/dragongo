
# FIXME: this is a hacky way to locate the include dirs
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../llvm-gofrontend)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../llvm-gofrontend/gofrontend/go)

set(LLVM_LINK_COMPONENTS
  CppGoFrontEnd
  CodeGen
  Core
  Support
  )

set(ParserTestSources
  ParserTest.cpp
  )

add_gobackend_unittest(GoParserTests
  ${ParserTestSources}
  )

include_directories(${MPCINSTALL}/include)
include_directories(${MPFRINSTALL}/include)
include_directories(${GMPINSTALL}/include)

# Record the fact that llvm-goparse depends on these libs
add_dependencies(GoBackendCoreTests libmpfr libmpc libgmp)

# Add in the archives for the llvm-goparse dependencies.
target_link_libraries(GoParserTests
  ${MPCINSTALL}/lib/libmpc.a
  ${MPFRINSTALL}/lib/libmpfr.a
  ${GMPINSTALL}/lib/libgmp.a
  )
