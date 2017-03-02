
# FIXME: this is a hack way to locate the include dirs
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../llvm-gofrontend)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../llvm-gofrontend/gofrontend/go)

# Note:
# - at the moment these unit tests only exercise code in Llvm_backend
#   and related classes, so although we're linking in the gofrontend,
#   we don't need any of the gofrontend deps (e.g. libgmp etc). If
#   shared linkage were used, this would have to change.

set(LLVM_LINK_COMPONENTS
  CppGoFrontEnd
  CodeGen
  Core
  Support
  )

set(BackendCoreSources
  BackendCoreTests.cpp
  BackendArrayStruct.cpp
  BackendCABIOracleTests.cpp
  BackendExprTests.cpp
  BackendPointerExprTests.cpp
  BackendFcnTests.cpp
  BackendCallTests.cpp
  BackendDebugEmit.cpp
  BackendStmtTests.cpp
  BackendVarTests.cpp
  BackendTreeIntegrity.cpp
  BackendNodeTests.cpp
  LinemapTests.cpp
  Sha1Tests.cpp
  TestUtilsTest.cpp
  TestUtils.cpp
  )

add_gobackend_unittest(GoBackendCoreTests
  ${BackendCoreSources}
  )

include_directories(${MPCINSTALL}/include)
include_directories(${MPFRINSTALL}/include)
include_directories(${GMPINSTALL}/include)

# Record the fact that llvm-goparse depends on these libs
add_dependencies(GoBackendCoreTests libmpfr libmpc libgmp)

# Add in the archives for the llvm-goparse dependencies.
target_link_libraries(GoBackendCoreTests
  ${MPCINSTALL}/lib/libmpc.a
  ${MPFRINSTALL}/lib/libmpfr.a
  ${GMPINSTALL}/lib/libgmp.a
  )
