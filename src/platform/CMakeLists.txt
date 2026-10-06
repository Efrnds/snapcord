add_library(snapcord_platform STATIC
    CredentialStore.cpp CredentialStore.h
    KeyState.cpp KeyState.h
)

target_include_directories(snapcord_platform PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(snapcord_platform PUBLIC Qt6::Core)

if(WIN32)
    target_link_libraries(snapcord_platform PRIVATE advapi32 user32)
endif()
