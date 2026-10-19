if(NOT BUILD_TESTING)
    return()
endif()

include(../src/WorkspaceSources.cmake)
find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
add_executable(canvas-test
    ${KIRA_WORKSPACE_SOURCES}
    CanvasTest.cpp
    ../src/LocalStyles.cpp ../src/LocalStyles.h
    ../src/resources.qrc
    ../src/CanvasServiceController.cpp
    ../src/LegacySettings.cpp
    ../src/MainWindow.cpp
    ../src/ServicePreferencesDialog.cpp
)
target_include_directories(canvas-test PRIVATE ../src ${CMAKE_BINARY_DIR}/src)
target_link_libraries(canvas-test PRIVATE danbooru-dictionary Nukleus::SDK Ardoise::Widgets Qt6::Network Qt6::Test Qt6::Widgets)
add_test(NAME canvas-test COMMAND canvas-test)
set_tests_properties(canvas-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_resources(canvas-test canvas_test_translations
    PREFIX /translations
    BASE ${CMAKE_BINARY_DIR}/src
    FILES ${KIRA_TRANSLATION_FILES})

if(MSVC)
    target_compile_options(canvas-test PRIVATE /utf-8)
endif()

add_executable(available-models-dialog-test
    AvailableModelsDialogTest.cpp ../src/AvailableModelsDialog.cpp ../src/AvailableModelsDialog.h
    ../src/CanvasServiceController.cpp ../src/CanvasServiceController.h)
target_include_directories(available-models-dialog-test PRIVATE ../src)
target_link_libraries(available-models-dialog-test PRIVATE danbooru-dictionary Nukleus::SDK Ardoise::Widgets Qt6::Test Qt6::Widgets)
qt_add_resources(available-models-dialog-test models_test_translations
    PREFIX /translations BASE ${CMAKE_BINARY_DIR}/src FILES ${KIRA_TRANSLATION_FILES})
add_test(NAME available-models-dialog-test COMMAND available-models-dialog-test)
set_tests_properties(available-models-dialog-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(MSVC)
    target_compile_options(available-models-dialog-test PRIVATE /utf-8)
endif()

add_test(NAME kaimera-version COMMAND kaimera --version)
set_tests_properties(kaimera-version PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    PASS_REGULAR_EXPRESSION "^Kaimera ${PROJECT_VERSION}\n$")

# The installer is a POSIX shell script for Linux and macOS.
if(UNIX)
    add_test(NAME get-kaimera-test
        COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/GetKaimeraTest.sh" "${PROJECT_SOURCE_DIR}/scripts/get-kaimera.sh")
endif()
