set(FONT_VERSION 1.0.42)
set(FONT_REVISION f31c230af7fb4854f40d96560e2616eafdd85926)
vcpkg_download_distfile(FONT_ARCHIVE
    URLS "https://github.com/be5invis/Sarasa-Gothic/releases/download/v${FONT_VERSION}/SarasaUiSC-TTF-${FONT_VERSION}.7z"
    FILENAME "SarasaUiSC-TTF-${FONT_VERSION}.7z"
    SHA512 8032b0ba8e6a58e23cafb3b52da741c42f13a074fe2bc891c81695a8d0dd7404666c7f7a1eecc2c0710fc92155abcf2301b60e44aea0716af03af038cef52bfd
)
vcpkg_extract_source_archive(FONT_DIRECTORY ARCHIVE "${FONT_ARCHIVE}" NO_REMOVE_ONE_LEVEL)
vcpkg_download_distfile(FONT_LICENSE
    URLS "https://raw.githubusercontent.com/be5invis/Sarasa-Gothic/${FONT_REVISION}/LICENSE"
    FILENAME "Sarasa-${FONT_REVISION}-OFL.txt"
    SHA512 c8de279cacb387b1d884144cc1a8d84187c775ae2d0fc98498174f155824fe1275053f407a7a3ba468496ddb89791c959cc4795c0f73715ebedf9fe95f4a4b58
)
file(INSTALL "${FONT_DIRECTORY}/SarasaUiSC-Regular.ttf"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME EditorSans.ttf)
vcpkg_install_copyright(FILE_LIST "${FONT_LICENSE}")
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
