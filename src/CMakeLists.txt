add_executable(kira-canvas
    main.cpp
    AppPaths.cpp
    AppPaths.h
    CuratedResource.h
    CuratedResourceManager.cpp
    CuratedResourceManager.h
    GenerationRequest.h
    GenerationProgress.h
    GeneratePanel.cpp
    GeneratePanel.h
    ImageAsset.h
    ImageLibraryWidget.cpp
    ImageLibraryWidget.h
    ImageViewer.cpp
    ImageViewer.h
    MainWindow.cpp
    MainWindow.h
    ModelDiscovery.cpp
    ModelDiscovery.h
    ModelInfo.h
    ReferenceSlotWidget.cpp
    ReferenceSlotWidget.h
    SdCliRunner.cpp
    SdCliRunner.h
    SdCliProgressParser.cpp
    SdCliProgressParser.h
    StartupPreparationWidget.cpp
    StartupPreparationWidget.h
    SystemIcon.h
    UpscaleDialog.cpp
    UpscaleDialog.h
)

target_link_libraries(kira-canvas PRIVATE Qt6::Network Qt6::Widgets)

if(APPLE)
    set_target_properties(kira-canvas PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Kira Canvas"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.kiracanvas.KiraCanvas"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kira-canvas PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(kira-canvas PRIVATE /W4)
endif()
