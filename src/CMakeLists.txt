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
    UpscaleDialog.cpp
    UpscaleDialog.h
)

target_link_libraries(kira-canvas PRIVATE KF6::KIOCore Qt6::Network Qt6::Widgets)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kira-canvas PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(kira-canvas PRIVATE /W4)
endif()
