find_package(Qt6 REQUIRED COMPONENTS Test)

function(snapcord_add_test name source)
    qt_add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARGN})
    set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE OFF MACOSX_BUNDLE OFF)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

snapcord_add_test(snapcord_tests VoicePipelineTest.cpp snapcord_core snapcord_voice ZLIB::ZLIB)
snapcord_add_test(snapcord_markdown_tests MarkdownTest.cpp snapcord_core)
snapcord_add_test(snapcord_member_list_tests MemberListTest.cpp snapcord_core)
snapcord_add_test(snapcord_game_detector_tests GameDetectorTest.cpp snapcord_core)
