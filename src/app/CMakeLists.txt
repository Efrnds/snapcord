qt_add_executable(snapcord WIN32 MACOSX_BUNDLE
    main.cpp
    AppController.cpp AppController.h
    Avatar.cpp Avatar.h
    ChannelSidebar.cpp ChannelSidebar.h
    ChatView.cpp ChatView.h
    ConnectionInfoPopup.cpp ConnectionInfoPopup.h
    EmojiPicker.cpp EmojiPicker.h
    ImageCache.cpp ImageCache.h
    IncomingCallWindow.cpp IncomingCallWindow.h
    Language.cpp Language.h
    LoginWindow.cpp LoginWindow.h
    MainWindow.cpp MainWindow.h
    MessageView.cpp MessageView.h
    Notifier.cpp Notifier.h
    ServerRail.cpp ServerRail.h
    SettingsDialog.cpp SettingsDialog.h
    UserPanel.cpp UserPanel.h
    VoiceChannelView.cpp VoiceChannelView.h
    VoiceController.cpp VoiceController.h
    VoicePanel.cpp VoicePanel.h
)

set(resource_dir "${PROJECT_SOURCE_DIR}/resources")
qt_add_resources(snapcord "app_resources"
    PREFIX "/"
    BASE "${resource_dir}"
    FILES
        "${resource_dir}/theme/dark.qss"
        "${resource_dir}/icons/snapcord.svg"
        "${resource_dir}/icons/home.svg"
        "${resource_dir}/icons/plus.svg"
        "${resource_dir}/icons/hash.svg"
        "${resource_dir}/icons/speaker.svg"
        "${resource_dir}/icons/mic.svg"
        "${resource_dir}/icons/mic-off.svg"
        "${resource_dir}/icons/headphones.svg"
        "${resource_dir}/icons/headphones-off.svg"
        "${resource_dir}/icons/settings.svg"
        "${resource_dir}/icons/disconnect.svg"
        "${resource_dir}/icons/call.svg"
        "${resource_dir}/icons/speaker-active.svg"
        "${resource_dir}/icons/at.svg"
        "${resource_dir}/icons/arrow-right.svg"
        "${resource_dir}/icons/file.svg"
)

# Interface translations. English is the source language; each .ts file adds another language.
qt_add_translations(snapcord
    TS_FILES "${PROJECT_SOURCE_DIR}/translations/snapcord_pt_BR.ts"
    SOURCE_TARGETS snapcord snapcord_core snapcord_voice snapcord_platform
    RESOURCE_PREFIX "/i18n"
)

target_link_libraries(snapcord PRIVATE
    Qt6::Widgets Qt6::Svg
    snapcord_core snapcord_voice snapcord_platform
    unofficial::nayuki-qr-code-generator::nayuki-qr-code-generator
)
target_compile_definitions(snapcord PRIVATE SNAPCORD_VERSION="${PROJECT_VERSION}")

set_target_properties(snapcord PROPERTIES
    OUTPUT_NAME "Snapcord"
    MACOSX_BUNDLE_BUNDLE_NAME "Snapcord"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.snapcord"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# On Windows, copy the Qt DLLs next to the executable so it can be opened with a double click.
if(WIN32)
    get_target_property(qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(qt_bin_dir "${qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET snapcord POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations --no-opengl-sw --no-system-d3d-compiler --no-compiler-runtime
                "$<TARGET_FILE:snapcord>"
            VERBATIM)
    endif()
endif()
