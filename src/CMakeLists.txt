add_executable(kira-canvas
    main.cpp
    AppPaths.cpp
    AppPaths.h
    GenerationProgress.h
    ImageOperationRequest.h
    GeneratePanel.cpp
    GeneratePanel.h
    ImageAsset.h
    ImageLibraryWidget.cpp
    ImageLibraryWidget.h
    ImageViewer.cpp
    ImageViewer.h
    MainWindow.cpp
    MainWindow.h
    ReferenceSlotWidget.cpp
    ReferenceSlotWidget.h
    ServiceClient.cpp
    ServiceClient.h
    ServicePreferencesDialog.cpp
    ServicePreferencesDialog.h
    ServiceProtocol.cpp
    ServiceProtocol.h
    ServiceJobResult.h
    SystemIcon.h
    UpscaleDialog.cpp
    UpscaleDialog.h
)

target_link_libraries(kira-canvas PRIVATE Qt6::Network Qt6::WebSockets Qt6::Widgets)

add_executable(kira-service
    ../src/kira-service-main.cpp
    AppPaths.cpp
    AppPaths.h
    CuratedResource.h
    CuratedResourceManager.cpp
    CuratedResourceManager.h
    GenerationRequest.h
    GenerationProgress.h
    KiraService.cpp
    KiraService.h
    SdCliProgressParser.cpp
    SdCliProgressParser.h
    SdCliRunner.cpp
    SdCliRunner.h
    ServiceJobResult.h
    ServiceProtocol.cpp
    ServiceProtocol.h
)
target_link_libraries(kira-service PRIVATE Qt6::Network Qt6::WebSockets)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kira-service PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(kira-service PRIVATE /W4)
endif()

if(APPLE)
    target_sources(kira-canvas PRIVATE
        MacPanelSurface.h
        MacPanelSurface.cpp
        MacSystemAppearance.h
        MacSystemAppearance.mm
        MacWorkspaceSplitter.h
        MacWorkspaceSplitter.cpp
    )
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
