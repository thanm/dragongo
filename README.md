
# dragongo is now: [gollvm](https://go.googlesource.com/gollvm)

## dragongo

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

// Additional steps needed for MacOS only:
% brew install gmp mpfr mpc

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

The executable llvm-goparse is a driver program that runs the gofrontend parser on a specific go program, with the LLVM-specific backend instance connected to the parser. This program is still mostly a skeleton -- it can create LLVM based on the Backend method calls made by the front end, however it doesn't actually do anything with the IR yet (just dumps it out). 

```
// From within <workarea>/build.opt:

% ninja llvm-goparse
% cat micro.go
package foo
func bar() int {
	return 1
}
% ./bin/llvm-goparse ~/micro.go
...
define hidden i64 @foo.bar() {
entry:
  %"$ret0" = alloca i64
  store i64 0, i64* %"$ret0"
  store i64 1, i64* %"$ret0"
  %"$ret0.ld.0" = load i64, i64* %"$ret0"
  ret i64 %"$ret0.ld.0"
}
%
```

## Using llvm-goparse in combination with a GCCGO installation

At the moment llvm-goparse is not capable of building the Go libraries + runtime (libgo), which makes it difficult/unwieldy to use for running actual Go programs. As an interim workaround, I've written a shim/wrapper script that allows you to use llvm-goparse in combination with an existing GCCGO installation, using gccgo for the runtime/libraries and the linking step, but llvm-goparse for any compilation. 

The wrapper script can be found in the tools/ subdir. To use it, build a copy of GCCGO and run "make install" to copy the bits into an install directory. From the GCCGO install directory, you can insert the wrapper by running it with the "--install" option:

```
 % cd /my/gccgo/install
 % /my/dragongo/sandbox/tools/gollvm-wrap.py --install
 executing: mv bin/gccgo bin/gccgo.real
 executing: chmod 0755 bin/gccgo
 executing: cp /my/dragongo/sandbox/tools/gollvm-wrap.py bin
 executing: cp /my/dragongo/sandbox/tools/script_utils.py bin
 wrapper installed successfully
 %

```

At this point you can now run "go build", "go run", etc using GCCGO -- the compilation steps will be performed by llvm-goparse, and the remainder (linking, incorporation of runtime) will be done by gccgo. Example:

```
% cd $GOPATH/src/himom
% go run himom.go
hi mom!
% go run -compiler gccgo himom.go
hi mom!
% GOLLVM_WRAP_OPTIONS=-t go run -compiler gccgo himom.go
# command-line-arguments
+ llvm-goparse -I $WORK -c -g -m64 -fgo-relative-import-path=_/mygopath/src/himom -o $WORK/command-line-arguments/_obj/_go_.o.s ./himom.go -L /my/gccgo/install/lib64/go/8.0.0/x86_64-pc-linux-gnu
hi mom
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

The test above makes sure that the LLVM type we get as a result of calling Backend::complex_type() is kosher and matches up to expectations.

