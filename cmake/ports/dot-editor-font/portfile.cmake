set(FONT_REVISION a85815a42757630ce188fdad368c2dfc444d4773)
vcpkg_download_distfile(FONT
    URLS "https://raw.githubusercontent.com/google/fonts/${FONT_REVISION}/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf"
    FILENAME "NotoSansSC-${FONT_REVISION}.ttf"
    SHA512 a55cc1eb24d1e84569dd9a4f94cd0bb63a390895951d03e92d5da1deb2f4ef81a207556e3cdbff12910fa09340290b9c762e97828cc025cb86c49bc8324a5840
)
vcpkg_download_distfile(FONT_LICENSE
    URLS "https://raw.githubusercontent.com/google/fonts/${FONT_REVISION}/ofl/notosanssc/OFL.txt"
    FILENAME "NotoSansSC-OFL-${FONT_REVISION}.txt"
    SHA512 056ba1569fb325c0223e9a8769f89f2a8477ff39f978e9c71fc7116219334180d345d7936e69d84333857e32aeaa2f1965abaea46f1df11d5bf0b75af799ef74
)
file(INSTALL "${FONT}" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME NotoSansSC.ttf)
vcpkg_install_copyright(FILE_LIST "${FONT_LICENSE}")
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
