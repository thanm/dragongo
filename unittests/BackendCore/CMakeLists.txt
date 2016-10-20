
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
  BackendFcnTests.cpp
  )

add_gobackend_unittest(GoBackendCoreTests
  ${BackendCoreSources}
  )


