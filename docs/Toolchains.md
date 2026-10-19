# Toolchain configuration

The project configuration selects LLVM Clang (`clang` for C, `clang++` for C++) and
LLD on all supported targets. `CMakeLists.txt` describes targets and links; the
toolchain selected by `CMakePresets.json` supplies compiler and runtime policy.
`CMakePresets.json` selects `cmake/Environment.cmake` directly. Source dependencies
use the selected toolchain; no system or local LLVM path is stored in the repository.

| Target | Compiler/linker | C++ runtime |
| --- | --- | --- |
| arm64 macOS | Clang drivers and `ld64.lld` | LLVM libc++, libc++abi and libunwind from one LLVM installation |
| x86_64 Linux | Clang drivers and `ld.lld` | LLVM libc++, libc++abi and libunwind from one LLVM installation |
| x86_64 Windows | `clang`/`clang++` in CL driver mode and `lld-link` | MSVC STL/SDK with dynamic MSVC CRT (`/MD`, `/MDd` in Debug) |

Install matching LLVM headers and runtime libraries alongside Clang. The toolchain
resolves `clang` and `clang++` from PATH, or from `DOT_LLVM_ROOT/bin` when the
environment variable is set. It requires C and C++ drivers from the same LLVM
installation. On macOS/Linux, it locates libc++ headers and libc++, libc++abi and
libunwind libraries under that root and fails configuration if any are missing.
LLD is selected with CMake's `CMAKE_LINKER_TYPE=LLD`; the corresponding platform
driver must be on PATH. Windows additionally requires the MSVC/Windows SDK
environment for the headers and import libraries used by the CL driver mode.
On macOS, Objective-C and Objective-C++ builds disable Clang's selector and class
message stubs because the current `ld64.lld` does not synthesize the latter. Dawn
compiles its `.mm` files as CXX, so the C++ driver also receives these flags; pure
C++ files ignore them without an unused-argument warning. The policy applies to
vendored native libraries as well as project targets.

`CPPFLAGS`, `CFLAGS`, `CXXFLAGS` and `LDFLAGS` remain input to CMake and the vendored
native libraries. `CC`/`CXX` do not select another compiler. Use a fresh build
directory when changing LLVM installations or runtime libraries; CMake caches
compiler identities and dependency options. The default Debug/Release presets
use separate `build/debug` and `build/release` directories.

Local checks establish only the platforms actually run. On macOS, inspect a real
link command and `otool -L` to confirm LLD and the LLVM runtime libraries. Windows
must verify the MSVC ABI, `/MD` mode, `lld-link` and runtime dependencies on a
Windows host. Linux must inspect its ELF interpreter, `NEEDED` entries and rpaths.
Product export and runtime-kit checks additionally verify relocation and signatures
where applicable; a successful configure alone does not establish portability.
