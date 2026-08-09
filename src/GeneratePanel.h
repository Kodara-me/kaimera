#pragma once

#include "GenerationRequest.h"
#include "ImageAsset.h"

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStatusBar;
class QSpinBox;
class QSlider;
class ReferenceSlotWidget;

class GeneratePanel final : public QWidget
{
    Q_OBJECT

public:
    explicit GeneratePanel(QStatusBar *statusBar, QWidget *parent = nullptr);

    void setSelectedAsset(const ImageAsset *asset);
    void removeAsset(const ImageAsset *asset);
    void setGenerationRunning(bool running);

signals:
    void referenceChanged(const QString &title, const ImageAsset *asset);
    void generationRequested(const GenerationRequest &request);

private:
    void updateGenerateEnabled();

    QPlainTextEdit *m_promptEdit;
    QPlainTextEdit *m_negativePromptEdit;
    QComboBox *m_samplerSelector;
    QComboBox *m_schedulerSelector;
    QSpinBox *m_widthSpinBox;
    QSpinBox *m_heightSpinBox;
    QSpinBox *m_stepsSpinBox;
    QDoubleSpinBox *m_cfgSpinBox;
    QSpinBox *m_seedSpinBox;
    QPushButton *m_generateButton;
    ReferenceSlotWidget *m_baseImageReference = nullptr;
    QLabel *m_baseImageHelper = nullptr;
    QWidget *m_baseImageStrengthContainer = nullptr;
    QSlider *m_baseImageStrengthSlider = nullptr;
    const ImageAsset *m_baseImage = nullptr;
    bool m_generationRunning = false;
};
