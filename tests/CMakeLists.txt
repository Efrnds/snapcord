find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(snapcord_tests VoicePipelineTest.cpp)
target_link_libraries(snapcord_tests PRIVATE Qt6::Test snapcord_core snapcord_voice ZLIB::ZLIB)
set_target_properties(snapcord_tests PROPERTIES WIN32_EXECUTABLE OFF MACOSX_BUNDLE OFF)

add_test(NAME snapcord_tests COMMAND snapcord_tests)
