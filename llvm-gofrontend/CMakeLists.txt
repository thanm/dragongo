
add_llvm_library(LLVMCppGoFrontEnd
gofrontend/go/ast-dump.cc
gofrontend/go/escape.cc
gofrontend/go/export.cc
gofrontend/go/expressions.cc
gofrontend/go/go.cc
gofrontend/go/gogo.cc
gofrontend/go/go-optimize.cc
gofrontend/go/go-diagnostics.cc
gofrontend/go/import-archive.cc
gofrontend/go/import.cc
gofrontend/go/lex.cc
gofrontend/go/parse.cc
gofrontend/go/runtime.cc
gofrontend/go/statements.cc
gofrontend/go/types.cc
gofrontend/go/unsafe.cc
go-location.cpp
go-linemap.cpp
go-backend.cpp
go-llvm-diagnostics.cpp
go-sha1.cpp
go-llvm.cpp
  )

include_directories(${CMAKE_BINARY_DIR}/external/gmp)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/gofrontend/go)

