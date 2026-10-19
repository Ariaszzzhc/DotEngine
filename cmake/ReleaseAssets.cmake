# Pin the two non-source release artifacts independently of the vendor tree.
# 两种非源码发布产物独立固定版本与校验值，不放入 vendor 源码目录。
include(FetchContent)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(dot_typescript_platform darwin-arm64)
    set(dot_typescript_hash
        828c336abf4cc12fda456a7a7f76b8294ab34630198ceb2618d08ce8b713817ba6f9d05f82c05530df8082f38209b82e5f289c93a2c986906ccf131e6c9f2cc8)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(dot_typescript_platform win32-x64)
    set(dot_typescript_hash
        d014371e40071e528b4a9d6a46f7f7494846a46b03ba107f8e0170ef982ecaa6f126a11a4b40b0fd514ef22da71e094953340246d30c47f20100a137113302e2)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(dot_typescript_platform linux-x64)
    set(dot_typescript_hash
        11875fd9c360eeb8025899f109d27e177577f4ef2285bdfb78702ed4b2bca00162ce04d06cf38aef31c75db3edf2b5f6e023aa3835de237b087f47c25c6ec7fc)
else()
    message(FATAL_ERROR "Unsupported TypeScript host: ${CMAKE_HOST_SYSTEM_NAME}")
endif()

FetchContent_Declare(dot_typescript
    URL "https://registry.npmjs.org/@typescript/typescript-${dot_typescript_platform}/-/typescript-${dot_typescript_platform}-7.0.2.tgz"
    URL_HASH "SHA512=${dot_typescript_hash}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(dot_typescript)
file(GLOB_RECURSE dot_typescript_candidates
    "${dot_typescript_SOURCE_DIR}/tsc" "${dot_typescript_SOURCE_DIR}/tsc.exe")
list(LENGTH dot_typescript_candidates dot_typescript_count)
if(NOT dot_typescript_count EQUAL 1)
    message(FATAL_ERROR "Expected one TypeScript native compiler, found ${dot_typescript_count}")
endif()
list(GET dot_typescript_candidates 0 DOT_TYPESCRIPT_COMPILER)
get_filename_component(DOT_TYPESCRIPT_TOOL_DIRECTORY "${DOT_TYPESCRIPT_COMPILER}" DIRECTORY)
find_file(dot_typescript_license NAMES LICENSE
    PATHS "${dot_typescript_SOURCE_DIR}" "${dot_typescript_SOURCE_DIR}/package"
    NO_DEFAULT_PATH REQUIRED)
find_file(dot_typescript_notice NAMES NOTICE.txt
    PATHS "${dot_typescript_SOURCE_DIR}" "${dot_typescript_SOURCE_DIR}/package"
    NO_DEFAULT_PATH REQUIRED)
file(READ "${dot_typescript_license}" dot_typescript_license_text)
file(READ "${dot_typescript_notice}" dot_typescript_notice_text)
set(DOT_TYPESCRIPT_LICENSE "${CMAKE_BINARY_DIR}/generated/licenses/typescript.txt")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/licenses")
file(WRITE "${DOT_TYPESCRIPT_LICENSE}"
    "${dot_typescript_license_text}\n\n${dot_typescript_notice_text}")

FetchContent_Declare(dot_editor_font
    URL "https://github.com/be5invis/Sarasa-Gothic/releases/download/v1.0.42/SarasaUiSC-TTF-1.0.42.7z"
    URL_HASH "SHA512=8032b0ba8e6a58e23cafb3b52da741c42f13a074fe2bc891c81695a8d0dd7404666c7f7a1eecc2c0710fc92155abcf2301b60e44aea0716af03af038cef52bfd"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(dot_editor_font)
file(GLOB_RECURSE dot_font_candidates "${dot_editor_font_SOURCE_DIR}/SarasaUiSC-Regular.ttf")
list(LENGTH dot_font_candidates dot_font_count)
if(NOT dot_font_count EQUAL 1)
    message(FATAL_ERROR "Expected one SarasaUiSC-Regular.ttf, found ${dot_font_count}")
endif()
list(GET dot_font_candidates 0 DOT_EDITOR_FONT)

set(DOT_EDITOR_FONT_LICENSE "${CMAKE_BINARY_DIR}/generated/licenses/Sarasa-OFL.txt")
set(dot_font_license_hash
    c8de279cacb387b1d884144cc1a8d84187c775ae2d0fc98498174f155824fe1275053f407a7a3ba468496ddb89791c959cc4795c0f73715ebedf9fe95f4a4b58)
if(NOT EXISTS "${DOT_EDITOR_FONT_LICENSE}")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/be5invis/Sarasa-Gothic/f31c230af7fb4854f40d96560e2616eafdd85926/LICENSE"
        "${DOT_EDITOR_FONT_LICENSE}"
        EXPECTED_HASH "SHA512=${dot_font_license_hash}"
        STATUS dot_font_license_status TLS_VERIFY ON)
    list(GET dot_font_license_status 0 dot_font_license_code)
    if(NOT dot_font_license_code EQUAL 0)
        message(FATAL_ERROR "Failed to fetch Sarasa license: ${dot_font_license_status}")
    endif()
else()
    file(SHA512 "${DOT_EDITOR_FONT_LICENSE}" dot_existing_font_license_hash)
    if(NOT dot_existing_font_license_hash STREQUAL dot_font_license_hash)
        message(FATAL_ERROR "Cached Sarasa license hash mismatch")
    endif()
endif()

if(WIN32)
    # Dawn uses the fixed DXC release DLLs instead of rebuilding DXC's LLVM tree.
    # Dawn 使用固定的 DXC 发布 DLL，避免再次构建 DXC 的 LLVM 源码树。
    FetchContent_Declare(dot_dxc
        URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602.24/dxc_2026_05_27.zip"
        URL_HASH "SHA512=cf331112f1753fca68525c85bb4964714d85dbf7cb5e41af4720bbdffefdfddad1878c7a1a74898e2105dff32957a526d188d9ee3f18f0df310ec8548827a374"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(dot_dxc)
    foreach(dot_dxc_file IN ITEMS dxcapi.h dxcompiler.dll dxil.dll dxcompiler.lib)
        file(GLOB_RECURSE dot_dxc_candidates "${dot_dxc_SOURCE_DIR}/${dot_dxc_file}")
        list(FILTER dot_dxc_candidates INCLUDE REGEX
            "/(inc|bin/x64|lib/x64)/${dot_dxc_file}$")
        list(LENGTH dot_dxc_candidates dot_dxc_count)
        if(NOT dot_dxc_count EQUAL 1)
            message(FATAL_ERROR "Expected one x64 DXC ${dot_dxc_file}, found ${dot_dxc_count}")
        endif()
        list(GET dot_dxc_candidates 0 dot_dxc_path)
        if(dot_dxc_file STREQUAL "dxcapi.h")
            get_filename_component(DOT_DXC_INCLUDE "${dot_dxc_path}" DIRECTORY)
        elseif(dot_dxc_file STREQUAL "dxcompiler.dll")
            set(DOT_DXC_DLL "${dot_dxc_path}")
        elseif(dot_dxc_file STREQUAL "dxil.dll")
            set(DOT_DXIL_DLL "${dot_dxc_path}")
        else()
            set(DOT_DXC_IMPLIB "${dot_dxc_path}")
        endif()
    endforeach()
    set(DOT_DXC_LICENSE "${CMAKE_BINARY_DIR}/generated/licenses/directx-dxc.txt")
    set(dot_dxc_license_hash
        9feaa85ca6d42d5a2d6fe773706bbab8241e78390a9d61ea9061c8f0eeb5a3e380ff07c222e02fbf61af7f2b2f6dd31c5fc87247a94dae275dc0a20cdfcc8c9d)
    if(NOT EXISTS "${DOT_DXC_LICENSE}")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/microsoft/DirectXShaderCompiler/v1.9.2602.24/LICENSE.TXT"
            "${DOT_DXC_LICENSE}"
            EXPECTED_HASH "SHA512=${dot_dxc_license_hash}"
            STATUS dot_dxc_license_status TLS_VERIFY ON)
        list(GET dot_dxc_license_status 0 dot_dxc_license_code)
        if(NOT dot_dxc_license_code EQUAL 0)
            message(FATAL_ERROR "Failed to fetch DXC license: ${dot_dxc_license_status}")
        endif()
    else()
        file(SHA512 "${DOT_DXC_LICENSE}" dot_existing_dxc_license_hash)
        if(NOT dot_existing_dxc_license_hash STREQUAL dot_dxc_license_hash)
            message(FATAL_ERROR "Cached DXC license hash mismatch")
        endif()
    endif()
endif()
