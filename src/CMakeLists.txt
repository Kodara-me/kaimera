add_subdirectory(danbooru)
include(WorkspaceSources.cmake)
add_executable(kaimera
    ${KIRA_WORKSPACE_SOURCES}
    CanvasServiceController.cpp
    CanvasServiceController.h
    LegacySettings.cpp
    LegacySettings.h
    MainWindow.cpp
    MainWindow.h
    ServicePreferencesDialog.cpp
    ServicePreferencesDialog.h
    resources.qrc
    main.cpp
)

target_link_libraries(kaimera PRIVATE danbooru-dictionary Nukleus::SDK Ardoise::Widgets Qt6::Multimedia Qt6::Network Qt6::Widgets)
target_compile_definitions(kaimera PRIVATE KAIMERA_VERSION="${PROJECT_VERSION}")

if(WIN32)
    set_target_properties(kaimera PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(kaimera PRIVATE kaimera.rc)
    set_source_files_properties(kaimera.rc PROPERTIES OBJECT_DEPENDS
        "${CMAKE_SOURCE_DIR}/assets/icons/kaimera.ico")
endif()

if(APPLE)
    set(APP_ICON "${CMAKE_SOURCE_DIR}/assets/icons/kaimera.icns")
    set_source_files_properties("${APP_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(kaimera PRIVATE "${APP_ICON}")
    set_target_properties(kaimera PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE "kaimera.icns"
        MACOSX_BUNDLE_BUNDLE_NAME "Kaimera"
        MACOSX_BUNDLE_GUI_IDENTIFIER "me.kodara.kaimera"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kaimera PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(kaimera PRIVATE /W4 /utf-8)
endif()

include(GNUInstallDirs)
if(APPLE)
    install(TARGETS kaimera BUNDLE DESTINATION .)
elseif(WIN32)
    install(TARGETS kaimera RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
elseif(UNIX)
    install(TARGETS kaimera RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    configure_file(
        ${CMAKE_SOURCE_DIR}/packaging/me.kodara.kaimera.desktop.in
        ${CMAKE_CURRENT_BINARY_DIR}/me.kodara.kaimera.desktop @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/me.kodara.kaimera.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    foreach(size 16 32 48 64 128 256 512 1024)
        install(FILES ${CMAKE_SOURCE_DIR}/assets/icons/kaimera-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME me.kodara.kaimera.png)
    endforeach()
endif()

find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
qt_add_translations(kaimera
    TS_FILES ${CMAKE_SOURCE_DIR}/translations/kaimera_fr.ts
        ${CMAKE_SOURCE_DIR}/translations/kaimera_en.ts
    QM_FILES_OUTPUT_VARIABLE KIRA_TRANSLATION_FILES)
qt_add_resources(kaimera canvas_translations
    PREFIX /translations
    BASE ${CMAKE_CURRENT_BINARY_DIR}
    FILES ${KIRA_TRANSLATION_FILES})
set(KIRA_TRANSLATION_FILES ${KIRA_TRANSLATION_FILES} PARENT_SCOPE)
