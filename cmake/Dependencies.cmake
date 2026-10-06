# Third-party libraries that need a bit of glue on top of vcpkg/FetchContent.

include(FetchContent)

# --- libdave: Discord's DAVE end-to-end encryption library -----------------------------------------
# Built from source because it has no vcpkg port; its dependencies (OpenSSL, mlspp, nlohmann-json)
# come from vcpkg.
FetchContent_Declare(libdave
    URL https://github.com/discord/libdave/archive/8de72b1f8a2ac3c5a5270755bb8091a62e3c6169.tar.gz
    SOURCE_SUBDIR cpp
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(libdave)

# --- RNNoise: neural network noise suppression ------------------------------------------------------
# The vcpkg port does not support Windows, so the release tarball is compiled directly. The release
# includes the trained model. On x86, SSE4.1/AVX2 kernels are selected at runtime.
FetchContent_Declare(rnnoise
    URL https://github.com/xiph/rnnoise/releases/download/v0.2/rnnoise-0.2.tar.gz
    URL_HASH SHA256=90fce4b00b9ff24c08dbfe31b82ffd43bae383d85c5535676d28b0a2b11c0d37
    SOURCE_SUBDIR no-cmake-project
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(rnnoise)

set(rnnoise_src "${rnnoise_SOURCE_DIR}/src")
add_library(rnnoise STATIC
    "${rnnoise_src}/denoise.c"
    "${rnnoise_src}/rnn.c"
    "${rnnoise_src}/pitch.c"
    "${rnnoise_src}/kiss_fft.c"
    "${rnnoise_src}/celt_lpc.c"
    "${rnnoise_src}/nnet.c"
    "${rnnoise_src}/nnet_default.c"
    "${rnnoise_src}/parse_lpcnet_weights.c"
    "${rnnoise_src}/rnnoise_data.c"
    "${rnnoise_src}/rnnoise_tables.c"
)
target_include_directories(rnnoise PUBLIC "${rnnoise_SOURCE_DIR}/include" PRIVATE "${rnnoise_src}")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|amd64)$")
    target_sources(rnnoise PRIVATE
        "${rnnoise_src}/x86/x86_dnn_map.c"
        "${rnnoise_src}/x86/x86cpu.c"
        "${rnnoise_src}/x86/nnet_sse4_1.c"
        "${rnnoise_src}/x86/nnet_avx2.c"
    )
    # Same definitions as the upstream autotools build with x86 runtime CPU detection.
    target_compile_definitions(rnnoise PRIVATE RNN_ENABLE_X86_RTCD $<$<NOT:$<C_COMPILER_ID:MSVC>>:CPU_INFO_BY_ASM>)
    if(MSVC)
        # MSVC doesn't announce the SSE levels that x64 guarantees, which RNNoise uses to pick its vector code.
        target_compile_definitions(rnnoise PRIVATE __SSE__ __SSE2__)
        set_source_files_properties("${rnnoise_src}/x86/nnet_sse4_1.c" PROPERTIES COMPILE_DEFINITIONS "__SSE4_1__")
        set_source_files_properties("${rnnoise_src}/x86/nnet_avx2.c" PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
    else()
        set_source_files_properties("${rnnoise_src}/x86/nnet_sse4_1.c" PROPERTIES COMPILE_OPTIONS "-msse4.1")
        set_source_files_properties("${rnnoise_src}/x86/nnet_avx2.c" PROPERTIES COMPILE_OPTIONS "-mavx;-mfma;-mavx2")
    endif()
endif()
# Third-party code: don't drown our build log in its warnings.
if(MSVC)
    target_compile_options(rnnoise PRIVATE /W0)
else()
    target_compile_options(rnnoise PRIVATE -w)
    target_link_libraries(rnnoise PRIVATE m)
endif()

# --- SpeexDSP: echo cancellation and automatic gain control -----------------------------------------
# The vcpkg port ships no CMake package, so build an imported target that picks the debug or release
# library to match the C runtime of the build.
find_path(SPEEXDSP_INCLUDE_DIR speex/speex_echo.h REQUIRED)
set(vcpkg_prefix "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
find_library(SPEEXDSP_LIBRARY_RELEASE NAMES speexdsp libspeexdsp PATHS "${vcpkg_prefix}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(SPEEXDSP_LIBRARY_DEBUG NAMES speexdsp libspeexdsp PATHS "${vcpkg_prefix}/debug/lib" NO_DEFAULT_PATH)
add_library(SpeexDSP::speexdsp UNKNOWN IMPORTED)
set_target_properties(SpeexDSP::speexdsp PROPERTIES
    IMPORTED_LOCATION "${SPEEXDSP_LIBRARY_RELEASE}"
    IMPORTED_LOCATION_RELEASE "${SPEEXDSP_LIBRARY_RELEASE}"
    INTERFACE_INCLUDE_DIRECTORIES "${SPEEXDSP_INCLUDE_DIR}"
)
if(SPEEXDSP_LIBRARY_DEBUG)
    set_target_properties(SpeexDSP::speexdsp PROPERTIES
        IMPORTED_LOCATION_DEBUG "${SPEEXDSP_LIBRARY_DEBUG}"
        IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
    )
endif()
