set(KIRA_WORKSPACE_SOURCES
    IdentityPanel.cpp IdentityPanel.h
    AvailableModelsDialog.cpp AvailableModelsDialog.h
    PromptEditorWidget.cpp EditBubble.cpp KiraSparkle.cpp ReferenceImageStrip.cpp QualityStepSlider.cpp StepHeader.cpp
    FloatingSurface.cpp GenerationCard.cpp JobActivityIndicator.cpp ResolutionPicker.cpp ShotPicker.cpp
    GenerationParametersWidget.cpp OutputSettingsWidget.cpp GenerationInspectorWidget.cpp
    GenerationPreviewWidget.cpp GenerationHistoryModel.cpp GenerationHistoryView.cpp GenerationHistoryWidget.cpp GenerationWorkspaceWidget.cpp)
list(TRANSFORM KIRA_WORKSPACE_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
