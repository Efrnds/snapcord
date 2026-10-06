add_library(snapcord_voice STATIC
    AudioEngine.cpp AudioEngine.h
    AudioProcessor.cpp AudioProcessor.h
    DaveSession.cpp DaveSession.h
    JitterBuffer.cpp JitterBuffer.h
    MiniaudioImpl.c
    OpusCodec.cpp OpusCodec.h
    Rtp.cpp Rtp.h
    SoundEffects.cpp SoundEffects.h
    TransportCipher.cpp TransportCipher.h
    UdpSocket.cpp UdpSocket.h
    VoiceConnection.cpp VoiceConnection.h
    VoiceGateway.cpp VoiceGateway.h
    VoiceSettings.cpp VoiceSettings.h
)

target_include_directories(snapcord_voice
    PUBLIC "${PROJECT_SOURCE_DIR}/src"
    PRIVATE "${MINIAUDIO_INCLUDE_DIR}"
)
target_link_libraries(snapcord_voice
    PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets snapcord_core snapcord_platform
    PRIVATE libdave Opus::opus OpenSSL::Crypto unofficial-sodium::sodium rnnoise SpeexDSP::speexdsp
)

# miniaudio only needs a few platform libraries; everything else is loaded at runtime.
if(WIN32)
    target_link_libraries(snapcord_voice PRIVATE ws2_32)
elseif(APPLE)
    target_link_libraries(snapcord_voice PRIVATE
        "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
else()
    target_link_libraries(snapcord_voice PRIVATE ${CMAKE_DL_LIBS} pthread m)
endif()
