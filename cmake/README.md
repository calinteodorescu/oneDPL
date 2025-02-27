# CMake system description

The project uses CMake for integration and testing purposes.

Configuration phase can be customized by passing additional variables: `cmake -D<var1>=<val1> -D<var2>=<val2> ... -D<varN>=<valN> <sources-dir>`

The following variables are provided for oneDPL configuration:

| Variable                     | Type   | Description                                                                                   | Default value |
|------------------------------|--------|-----------------------------------------------------------------------------------------------|---------------|
| ONEDPL_BACKEND               | STRING | Threading backend; supported values: tbb, dpcpp, dpcpp_only, serial, ...; the default value is defined by compiler: dpcpp for DPC++ and tbb for others | tbb/dpcpp |
| ONEDPL_DEVICE_TYPE           | STRING | Device type, applicable only for DPC++ backends; supported values: GPU, CPU, FPGA_HW, FPGA_EMU | GPU           |
| ONEDPL_DEVICE_BACKEND        | STRING | Device backend type, applicable only for oneDPL DPC++ backends; supported values: opencl, level_zero. | Any(*) |
| ONEDPL_USE_UNNAMED_LAMBDA    | BOOL   | Pass `-fsycl-unnamed-lambda`, `-fno-sycl-unnamed-lambda` compile options or nothing           |               |
| ONEDPL_FPGA_STATIC_REPORT    | BOOL   | Enable the static report generation for the FPGA_HW device type                               | OFF           |
| ONEDPL_USE_AOT_COMPILATION   | BOOL   | Enable the ahead of time compilation via OpenCL™ Offline Compiler (OCLOC)                     | OFF           |
| ONEDPL_ENABLE_SIMD           | BOOL   | Enable SIMD vectorization by passing an OpenMP SIMD flag to the compiler if supported; the flag is passed to user project compilation string if enabled | ON           |
| ONEDPL_AOT_ARCH              | STRING | Architecture options for the ahead of time compilation, supported values can be found [here](https://software.intel.com/content/www/us/en/develop/documentation/oneapi-dpcpp-cpp-compiler-dev-guide-and-reference/top/compilation/ahead-of-time-compilation.html); the default value `*` means compilation for all available options | *             |

Some useful CMake variables ([here](https://cmake.org/cmake/help/latest/manual/cmake-variables.7.html) you can find a full list of CMake variables for the latest version):

- [`CMAKE_CXX_COMPILER`](https://cmake.org/cmake/help/latest/variable/CMAKE_LANG_COMPILER.html) - C++ compiler used for build, e.g. `CMAKE_CXX_COMPILER=dpcpp`.
- [`CMAKE_BUILD_TYPE`](https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html) - build type that affects optimization level and debug options, values: `RelWithDebInfo`, `Debug`, `Release`, ...; e.g. `CMAKE_BUILD_TYPE=RelWithDebInfo`.
- [`CMAKE_CXX_STANDARD`](https://cmake.org/cmake/help/latest/variable/CMAKE_CXX_STANDARD.html) - C++ standard, e.g. `CMAKE_CXX_STANDARD=17`.

## Testing

Steps:

1. Configure project using CMake.
2. Perform build [and run] using build system (e.g. `make build-onedpl-tests`).
3. (optional) Run tests using CTest.

**NOTE**: tests are not added to `all` target, so they are not built/run by default.
The following targets are available for build system after configuration:

- `<test-name>` - build specific test, e.g. `for_each.pass`;
- `run-<test-name>` - build and run specific test, e.g. `run-for_each.pass`;
- `build-onedpl-<tests-subdir>-tests` - build all tests from specific subdirectory under `<root>/test`, e.g. `build-onedpl-general-tests`;
- `run-onedpl-<tests-subdir>-tests` - build and run all tests from specific subdirectory under `<root>/test`, e.g. `run-onedpl-general-tests`;
- `build-onedpl-tests` - build all tests;
- `run-onedpl-tests` - build and run all tests.

Sudirectories are added as labels for each test and can be used with `ctest -L <label>`.
For example, `<root>/test/path/to/test.pass.cpp` will have `path` and `to` labels.

## How to use oneDPL from CMake
### Using oneDPL source files

Using oneDPL source files allows you to integrate oneDPL source code into your project with the [add_subdirectory](https://cmake.org/cmake/help/latest/command/add_subdirectory.html) command. `add_subdirectory(<oneDPL_root_dir> [<oneDPL_output_dir>])` adds oneDPL to the user project build.
* `<oneDPL_root_dir>` is a relative or absolute path to oneDPL root directory
* `<oneDPL_output_dir>` is a relative or absolute path to directory for holding output files for oneDPL
* If `<oneDPL_root_dir>` is the relative path, then `<oneDPL_output_dir>` is optional.

The variables from the table above can be specified before the `add_subdirectory` call to customize your oneDPL configuration and build.

For example:

```cmake
project(Foo)
add_executable(foo foo.cpp)

# Specify oneDPL backend
set(ONEDPL_BACKEND tbb)

# Add oneDPL to the build.
add_subdirectory(/path/to/oneDPL build_oneDPL)
```

oneDPL tests are not configured if oneDPL in such scenario. So they can't be built and run in this case.

### Using oneDPL package

oneDPLConfig.cmake and oneDPLConfigVersion.cmake are included into oneDPL distribution.

These files allow to integrate oneDPL into user project with the [find_package](https://cmake.org/cmake/help/latest/command/find_package.html) command. Successful invocation of `find_package(oneDPL <options>)` creates imported target `oneDPL` that can be passed to the [target_link_libraries](https://cmake.org/cmake/help/latest/command/target_link_libraries.html) command.

For example:

```cmake
project(Foo)
add_executable(foo foo.cpp)

# Search for oneDPL
find_package(oneDPL REQUIRED)

# Connect oneDPL to foo
target_link_libraries(foo oneDPL)
```

Use `ONEDPL_PAR_BACKEND` variable before the invocation of `find_package(oneDPL <options>)` to control standard host (par) backend:

- Supported values:
  - `tbb` for oneTBB backend;
  - `openmp` for OpenMP backend;
  - `serial` for serial backend.
- If this variable is not set then the first suitable backend is chosen among oneTBB, OpenMP and serial, they are considered in the order as specified.
- oneDPL is considered as not found (`oneDPL_FOUND=FALSE`) if `ONEDPL_PAR_BACKEND` is specified, but not found or not supported.
- Macro `ONEDPL_USE_OPENMP_BACKEND` is set to `0` if oneTBB backend is chosen.
- Macro `ONEDPL_USE_TBB_BACKEND` is set to `0` if OpenMP backend is chosen.
- Macro `ONEDPL_USE_TBB_BACKEND` is set to `0` and `ONEDPL_USE_OPENMP_BACKEND` is set to `0` if serial backend is chosen.

### oneDPLConfig files generation

This section is applicable for oneDPL packaging creation process, but not for usual development flow.

`cmake/scripts/generate_config.cmake` is provided to generate oneDPLConfig files for oneDPL package.

How to use:

`cmake [-DOUTPUT_DIR=<output_dir>] -P cmake/scripts/generate_config.cmake`
