add_library(snapcord_platform STATIC
    CredentialStore.h
    KeyState.cpp KeyState.h
    ProcessList.h
)

target_include_directories(snapcord_platform PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(snapcord_platform PUBLIC Qt6::Core)

# Where the login token is kept on each system.
if(WIN32)
    target_sources(snapcord_platform PRIVATE CredentialStoreWindows.cpp ProcessListWindows.cpp)
    target_link_libraries(snapcord_platform PRIVATE advapi32 user32)
elseif(APPLE)
    target_sources(snapcord_platform PRIVATE CredentialStoreMac.cpp ProcessListMac.cpp)
    target_link_libraries(snapcord_platform PRIVATE "-framework CoreFoundation" "-framework Security")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
    target_sources(snapcord_platform PRIVATE CredentialStoreLinux.cpp ProcessListLinux.cpp)
    target_link_libraries(snapcord_platform PRIVATE PkgConfig::LIBSECRET)
endif()
