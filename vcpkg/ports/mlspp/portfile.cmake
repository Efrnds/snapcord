# Same configuration as the overlay port shipped with discord/libdave (cpp/vcpkg-alts/openssl_3).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO cisco/mlspp
    REF "${VERSION}"
    SHA512 5d37631e2c47daae1133ef074e60cc09ca2d395f9e11c416f829060e374051cf219d2d7fe98dae49d1d045292e07d6a09f4814a5f16e6cc05e67e7cd96f146c4
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DDISABLE_GREASE=ON
        -DMLS_CXX_NAMESPACE=mlspp
        -DTESTING=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME "MLSPP" CONFIG_PATH "share/MLSPP")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
