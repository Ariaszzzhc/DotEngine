# Standalone official Go compiler; Node/npm wrappers are not installed.
# 直接使用官方独立 Go 编译器，不安装 Node/npm 包装器。
# Preserve vendor binaries/signatures; these tools have no vcpkg library dependencies.
# 保留官方二进制与签名；这些工具不依赖 vcpkg 库。
set(VCPKG_FIXUP_MACHO_RPATH OFF)
set(VCPKG_FIXUP_ELF_RPATH OFF)

if(VCPKG_HOST_IS_OSX)
    set(PLATFORM "darwin-arm64")
    set(ARCHIVE_HASH 828c336abf4cc12fda456a7a7f76b8294ab34630198ceb2618d08ce8b713817ba6f9d05f82c05530df8082f38209b82e5f289c93a2c986906ccf131e6c9f2cc8)
elseif(VCPKG_HOST_IS_LINUX)
    set(PLATFORM "linux-x64")
    set(ARCHIVE_HASH 11875fd9c360eeb8025899f109d27e177577f4ef2285bdfb78702ed4b2bca00162ce04d06cf38aef31c75db3edf2b5f6e023aa3835de237b087f47c25c6ec7fc)
elseif(VCPKG_HOST_IS_WINDOWS)
    set(PLATFORM "win32-x64")
    set(ARCHIVE_HASH d014371e40071e528b4a9d6a46f7f7494846a46b03ba107f8e0170ef982ecaa6f126a11a4b40b0fd514ef22da71e094953340246d30c47f20100a137113302e2)
endif()

vcpkg_download_distfile(ARCHIVE
    URLS "https://registry.npmjs.org/@typescript/typescript-${PLATFORM}/-/typescript-${PLATFORM}-${VERSION}.tgz"
    FILENAME "typescript-${PLATFORM}-${VERSION}.tgz"
    SHA512 "${ARCHIVE_HASH}"
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")
file(COPY "${SOURCE_PATH}/lib/" DESTINATION "${CURRENT_PACKAGES_DIR}/tools/typescript-native"
    USE_SOURCE_PERMISSIONS)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE" "${SOURCE_PATH}/NOTICE.txt")
