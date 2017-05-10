
set(LLVM_LINK_COMPONENTS
  CodeGen
  Core
  Support
  )

add_llvm_library(LLVMCppGoFrontEnd
gofrontend/go/ast-dump.cc
gofrontend/go/escape.cc
gofrontend/go/export.cc
gofrontend/go/expressions.cc
gofrontend/go/go-diagnostics.cc
gofrontend/go/go-encode-id.cc
gofrontend/go/go-dump.cc
gofrontend/go/go-optimize.cc
gofrontend/go/go.cc
gofrontend/go/gogo.cc
gofrontend/go/import-archive.cc
gofrontend/go/import.cc
gofrontend/go/lex.cc
gofrontend/go/parse.cc
gofrontend/go/runtime.cc
gofrontend/go/statements.cc
gofrontend/go/types.cc
gofrontend/go/unsafe.cc
gofrontend/go/wb.cc
go-backend.cpp
go-llvm-bexpression.cpp
go-llvm-bfunction.cpp
go-llvm-bnode.cpp
go-llvm-bstatement.cpp
go-llvm-btype.cpp
go-llvm-builtins.cpp
go-llvm-bvariable.cpp
go-llvm-cabi-oracle.cpp
go-llvm-diagnostics.cpp
go-llvm-dibuildhelper.cpp
go-llvm-genblocks.cpp
go-llvm-irbuilders.cpp
go-llvm-linemap.cpp
go-llvm-tree-integrity.cpp
go-llvm-typemanager.cpp
go-llvm.cpp
go-sha1.cpp
)

add_dependencies(LLVMCppGoFrontEnd intrinsics_gen)

include_directories(${MPCINSTALL}/include)
include_directories(${MPFRINSTALL}/include)
include_directories(${GMPINSTALL}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/gofrontend/go)

