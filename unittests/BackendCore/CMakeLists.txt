
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../llvm-gofrontend)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../llvm-gofrontend/gofrontend/go)

set(LLVM_LINK_COMPONENTS
  CppGoFrontEnd
  CodeGen
  Core
  Support
  )

set(BackendCoreSources
  BackendCoreTests.cpp
  )

add_gobackend_unittest(GoBackendCoreTests
  ${BackendCoreSources}
  )


