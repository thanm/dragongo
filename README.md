# dragongo

LLVM IR generation "middle end" for LLVM-based go compiler. This is currently very much in a prototype phase, with a fairly brittle/fragile build process, so please take careful note of the setup instructions.

Dragongo (name still not finalized) is layered on top of LLVM, much in the same way that things work for "clang" or "compiler-rt": you check out a copy of the LLVM source tree and then within that tree you check out additional git repos. 

You'll need to have an up-to-date copy of cmake on your system (3.6 vintage).

## Setting up a dragongo work area:

```
// Here 'workarea' will contain a copy of the LLVM source tree and one or more build areas
% mkdir workarea
% cd workarea

// Sources
% git clone http://llvm.org/git/llvm.git
% cd llvm/tools
% git clone https://github.com/thanm/dragongo
% cd dragongo/llvm-gofrontend
% git clone https://go.googlesource.com/gofrontend
% cd ../../../..

// Create a build directory and run cmake
% mkdir build.opt
% cd build.opt
% cmake -DCMAKE_BUILD_TYPE=Debug -G Ninja ../llvm

// Prebuild
ninja libgmp libmpfr libmpc

// Now regular build
ninja <dragongo target(s)>
```

## Source code structure

Within <workarea>/llvm/tools/dragongo, the following directories are of interest:

.../llvm/tools/dragongo:

 * contains build rules and source code for llvm-goparse
 
.../llvm/tools/dragongo/llvm-gofrontend:

 * contains build rules for the libLLVMCppGoFrontEnd.a, a library that contains both the gofrontend code and the LLVM-specific middle layer (for example, the definition of the class Llvm_backend, which inherits from Backend).
 
.../llvm/tools/dragongo/unittests:

 * source code for the unit tests
 
## Building and running llvm-goparse

The executable llvm-goparse is a driver program that will (at some point) kick off the go parser on a specified go program. At the moment llvm-goparse doesn't do anything, only creates an instance of the back end and then exits. 

```
// From within <workarea>/build.opt:

% ninja llvm-goparse
% ./bin/llvm-goparse ~/himom.go
%
```

## Building and running the unit tests

Here are instructions on building and running the unit tests for the middle layer:

```
// From within <workarea>/build.opt:

// Build unit test
% ninja GoBackendCoreTests 

// Run unit test
% ./tools/dragongo/unittests/BackendCore/GoBackendCoreTests
[==========] Running 10 tests from 2 test cases.
[----------] Global test environment set-up.
[----------] 9 tests from BackendCoreTests
[ RUN      ] BackendCoreTests.MakeBackend
[       OK ] BackendCoreTests.MakeBackend (1 ms)
[ RUN      ] BackendCoreTests.ScalarTypes
[       OK ] BackendCoreTests.ScalarTypes (0 ms)
[ RUN      ] BackendCoreTests.StructTypes
[       OK ] BackendCoreTests.StructTypes (1 ms)
[ RUN      ] BackendCoreTests.ComplexTypes
[       OK ] BackendCoreTests.ComplexTypes (0 ms)
[ RUN      ] BackendCoreTests.FunctionTypes
[       OK ] BackendCoreTests.FunctionTypes (0 ms)
[ RUN      ] BackendCoreTests.PlaceholderTypes
[       OK ] BackendCoreTests.PlaceholderTypes (0 ms)
[ RUN      ] BackendCoreTests.ArrayTypes
[       OK ] BackendCoreTests.ArrayTypes (0 ms)
[ RUN      ] BackendCoreTests.NamedTypes
[       OK ] BackendCoreTests.NamedTypes (0 ms)
[ RUN      ] BackendCoreTests.TypeUtils

...

[  PASSED  ] 10 tests.
```

The unit tests currently work by instantiating an LLVM Backend instance and making backend method calls (to mimic what the frontend would do), then inspects the results to make sure they are as expected. Here is an example:

```
TEST(BackendCoreTests, ComplexTypes) {
  LLVMContext C;

  Type *ft = Type::getFloatTy(C);
  Type *dt = Type::getDoubleTy(C);

  std::unique_ptr<Backend> be(go_get_backend(C));
  Btype *c32 = be->complex_type(64);
  ASSERT_TRUE(c32 != NULL);
  ASSERT_EQ(c32->type(), mkTwoFieldLLvmStruct(C, ft, ft));
  Btype *c64 = be->complex_type(128);
  ASSERT_TRUE(c64 != NULL);
  ASSERT_EQ(c64->type(), mkTwoFieldLLvmStruct(C, dt, dt));
}
```

The test above makes sure that the LLVM type we get as a result of claling Backend::complex_type() is kosher and matches up to expectations.

