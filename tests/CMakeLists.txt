if(NOT BUILD_TESTING)
    return()
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Network Test)

add_executable(curated-resource-manager-test
    CuratedResourceManagerTest.cpp
    ../src/AppPaths.cpp
    ../src/AppPaths.h
    ../src/CuratedResource.h
    ../src/CuratedResourceManager.cpp
    ../src/CuratedResourceManager.h
    ../src/GenerationRequest.h
    ../src/GenerationProgress.h
    ../src/ImageOperationRequest.h
    ../src/GeneratePanel.cpp
    ../src/GeneratePanel.h
    ../src/ImageAsset.h
    ../src/ImageLibraryWidget.cpp
    ../src/ImageLibraryWidget.h
    ../src/ImageViewer.cpp
    ../src/ImageViewer.h
    ../src/ModelInfo.h
    ../src/MainWindow.cpp
    ../src/MainWindow.h
    ../src/ReferenceSlotWidget.cpp
    ../src/ReferenceSlotWidget.h
    ../src/SdCliRunner.cpp
    ../src/SdCliRunner.h
    ../src/SdCliProgressParser.cpp
    ../src/SdCliProgressParser.h
    ../src/ServiceClient.cpp
    ../src/ServiceClient.h
    ../src/ServicePreferencesDialog.cpp
    ../src/ServicePreferencesDialog.h
    ../src/ServiceProtocol.cpp
    ../src/ServiceProtocol.h
    ../src/StartupPreparationWidget.cpp
    ../src/StartupPreparationWidget.h
    ../src/UpscaleDialog.cpp
    ../src/UpscaleDialog.h
)

target_include_directories(curated-resource-manager-test PRIVATE ../src)
target_link_libraries(curated-resource-manager-test PRIVATE Qt6::Network Qt6::Test Qt6::WebSockets Qt6::Widgets)

add_test(NAME curated-resource-manager-test COMMAND curated-resource-manager-test)
set_tests_properties(curated-resource-manager-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME kira-service-installer-test
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/KiraServiceInstallerTest.sh
                 ${CMAKE_SOURCE_DIR} $<TARGET_FILE:kira-service>)
