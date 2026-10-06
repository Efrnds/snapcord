add_library(snapcord_core STATIC
    ClientProperties.cpp ClientProperties.h
    Gateway.cpp Gateway.h
    Log.cpp Log.h
    Models.cpp Models.h
    Permissions.cpp Permissions.h
    RemoteAuth.cpp RemoteAuth.h
    RestClient.cpp RestClient.h
    RsaKey.cpp RsaKey.h
    Session.cpp Session.h
    ZlibStream.cpp ZlibStream.h
)

target_include_directories(snapcord_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(snapcord_core
    PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets
    PRIVATE OpenSSL::Crypto ZLIB::ZLIB
)
