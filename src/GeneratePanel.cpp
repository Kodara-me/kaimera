#include "GeneratePanel.h"

#include "ReferenceSlotWidget.h"
#include "SystemIcon.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <limits>

namespace
{
QVBoxLayout *labeledColumn(const QString &label, QWidget *control)
{
    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(label, control->parentWidget()));
    layout->addWidget(control);
    return layout;
}

QLabel *sectionLabel(const QString &text, const QString &objectName, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(objectName);
    return label;
}
}

GeneratePanel::GeneratePanel(QStatusBar *statusBar, QWidget *parent)
    : QWidget(parent)
{
    Q_UNUSED(statusBar)
    auto *layout = new QVBoxLayout(this);
#if defined(Q_OS_MACOS)
    m_serviceStatus = new QLabel(this);
    m_serviceStatus->setObjectName(QStringLiteral("generateServiceStatus"));
    m_serviceStatus->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(m_serviceStatus);
#endif
    auto *scrollArea = new QScrollArea(this);
#if defined(Q_OS_MACOS)
    scrollArea->setFrameShape(QFrame::NoFrame);
#endif
    scrollArea->setWidgetResizable(true);
    auto *content = new QWidget(scrollArea);
    auto *contentLayout = new QVBoxLayout(content);

    auto *promptLabel = sectionLabel(tr("Prompt"), QStringLiteral("generatePromptSection"), content);
    contentLayout->addWidget(promptLabel);
    m_promptEdit = new QPlainTextEdit(content);
    m_promptEdit->setPlaceholderText(tr("Describe the image you want to generate..."));
    m_promptEdit->setMinimumHeight(90);
    contentLayout->addWidget(m_promptEdit);

    auto *negativePromptLabel = new QLabel(tr("Negative Prompt"), content);
    contentLayout->addWidget(negativePromptLabel);
    m_negativePromptEdit = new QPlainTextEdit(content);
    m_negativePromptEdit->setPlaceholderText(tr("Things to avoid..."));
    m_negativePromptEdit->setFixedHeight(60);
    contentLayout->addWidget(m_negativePromptEdit);

#if defined(Q_OS_MACOS)
    contentLayout->addWidget(sectionLabel(tr("Model Settings"), QStringLiteral("generateModelSection"), content));
#endif
    auto *samplerLabel = new QLabel(tr("Sampler"), content);
    contentLayout->addWidget(samplerLabel);
    m_samplerSelector = new QComboBox(content);
    m_samplerSelector->addItem(QStringLiteral("DPM++ 2M"), QStringLiteral("dpm++2m"));
    m_samplerSelector->addItem(QStringLiteral("Euler"), QStringLiteral("euler"));
    m_samplerSelector->addItem(QStringLiteral("Euler ancestral"), QStringLiteral("euler_a"));
    m_samplerSelector->addItem(QStringLiteral("Heun"), QStringLiteral("heun"));
    m_samplerSelector->addItem(QStringLiteral("DPM++ 2M SDE"), QStringLiteral("dpm++2m_sde"));
    m_samplerSelector->addItem(QStringLiteral("DDIM trailing"), QStringLiteral("ddim_trailing"));
    contentLayout->addWidget(m_samplerSelector);

    auto *schedulerLabel = new QLabel(tr("Scheduler"), content);
    contentLayout->addWidget(schedulerLabel);
    m_schedulerSelector = new QComboBox(content);
    m_schedulerSelector->addItem(QStringLiteral("Karras"), QStringLiteral("karras"));
    m_schedulerSelector->addItem(QStringLiteral("Discrete"), QStringLiteral("discrete"));
    m_schedulerSelector->addItem(QStringLiteral("Exponential"), QStringLiteral("exponential"));
    m_schedulerSelector->addItem(QStringLiteral("AYS"), QStringLiteral("ays"));
    m_schedulerSelector->addItem(QStringLiteral("GITS"), QStringLiteral("gits"));
    contentLayout->addWidget(m_schedulerSelector);

#if defined(Q_OS_MACOS)
    contentLayout->addWidget(sectionLabel(tr("Resolution"), QStringLiteral("generateResolutionSection"), content));
#endif
    m_widthSpinBox = new QSpinBox(content);
    m_widthSpinBox->setRange(256, 4096);
    m_widthSpinBox->setSingleStep(64);
    m_widthSpinBox->setValue(1024);
    m_heightSpinBox = new QSpinBox(content);
    m_heightSpinBox->setRange(256, 4096);
    m_heightSpinBox->setSingleStep(64);
    m_heightSpinBox->setValue(1024);
    auto *resolutionLayout = new QHBoxLayout;
    resolutionLayout->addLayout(labeledColumn(tr("Width"), m_widthSpinBox));
    resolutionLayout->addLayout(labeledColumn(tr("Height"), m_heightSpinBox));
    contentLayout->addLayout(resolutionLayout);

#if defined(Q_OS_MACOS)
    contentLayout->addWidget(sectionLabel(tr("Generation Settings"), QStringLiteral("generateGenerationSection"), content));
#endif
    m_stepsSpinBox = new QSpinBox(content);
    m_stepsSpinBox->setRange(1, 100);
    m_stepsSpinBox->setValue(28);
    m_cfgSpinBox = new QDoubleSpinBox(content);
    m_cfgSpinBox->setRange(1.0, 15.0);
    m_cfgSpinBox->setDecimals(1);
    m_cfgSpinBox->setSingleStep(0.5);
    m_cfgSpinBox->setValue(7.0);
    auto *generationSettingsLayout = new QHBoxLayout;
    generationSettingsLayout->addLayout(labeledColumn(tr("Steps"), m_stepsSpinBox));
    generationSettingsLayout->addLayout(labeledColumn(tr("CFG Scale"), m_cfgSpinBox));
    contentLayout->addLayout(generationSettingsLayout);

    auto *seedLabel = sectionLabel(tr("Seed"), QStringLiteral("generateSeedSection"), content);
    contentLayout->addWidget(seedLabel);
    auto *seedLayout = new QHBoxLayout;
    m_seedSpinBox = new QSpinBox(content);
    m_seedSpinBox->setRange(-1, std::numeric_limits<int>::max());
    m_seedSpinBox->setSpecialValueText(tr("Random"));
    m_seedSpinBox->setValue(-1);
    seedLayout->addWidget(m_seedSpinBox, 1);
    auto *randomizeSeedButton = new QToolButton(content);
    randomizeSeedButton->setIcon(systemIcon(QStringLiteral("view-refresh"), QStyle::SP_BrowserReload, this));
    randomizeSeedButton->setToolTip(tr("Randomize seed"));
    connect(randomizeSeedButton, &QToolButton::clicked, this, [this] {
        m_seedSpinBox->setValue(QRandomGenerator::global()->bounded(1, 1000000000));
    });
    seedLayout->addWidget(randomizeSeedButton);
    contentLayout->addLayout(seedLayout);

    contentLayout->addWidget(sectionLabel(tr("Image Input"), QStringLiteral("generateImageInputSection"), content));
    m_baseImageReference = new ReferenceSlotWidget(tr("Base image"), content);
    m_baseImageReference->setToolTip(tr("Uses this image as the starting point for generation."));
    contentLayout->addWidget(m_baseImageReference);
    m_baseImageHelper = new QLabel(tr("Start generation from an existing image."), content);
    m_baseImageHelper->setObjectName(QStringLiteral("baseImageHelper"));
    m_baseImageHelper->setForegroundRole(QPalette::PlaceholderText);
    m_baseImageHelper->setWordWrap(true);
    contentLayout->addWidget(m_baseImageHelper);
    m_baseImageStrengthContainer = new QWidget(content);
    m_baseImageStrengthContainer->setObjectName(QStringLiteral("baseImageStrengthContainer"));
    auto *baseImageStrengthLayout = new QVBoxLayout(m_baseImageStrengthContainer);
    baseImageStrengthLayout->setContentsMargins(0, 0, 0, 0);
    auto *baseImageStrengthRow = new QHBoxLayout;
    baseImageStrengthRow->addWidget(new QLabel(tr("Strength"), m_baseImageStrengthContainer));
    m_baseImageStrengthSlider = new QSlider(Qt::Horizontal, m_baseImageStrengthContainer);
    m_baseImageStrengthSlider->setRange(0, 100);
    m_baseImageStrengthSlider->setValue(55);
    m_baseImageStrengthSlider->setToolTip(tr("Controls how much the generated image may differ from the base image."));
    baseImageStrengthRow->addWidget(m_baseImageStrengthSlider, 1);
    auto *baseImageStrengthValue = new QLabel(QStringLiteral("0.55"), m_baseImageStrengthContainer);
    baseImageStrengthRow->addWidget(baseImageStrengthValue);
    baseImageStrengthLayout->addLayout(baseImageStrengthRow);
    auto *baseImageStrengthHelper = new QLabel(tr("Higher values allow more change."), m_baseImageStrengthContainer);
    baseImageStrengthHelper->setObjectName(QStringLiteral("baseImageStrengthHelper"));
    baseImageStrengthHelper->setForegroundRole(QPalette::PlaceholderText);
    baseImageStrengthHelper->setWordWrap(true);
    baseImageStrengthLayout->addWidget(baseImageStrengthHelper);
    connect(m_baseImageStrengthSlider, &QSlider::valueChanged, this, [baseImageStrengthValue](int value) {
        baseImageStrengthValue->setText(QString::number(value / 100.0, 'f', 2));
    });
    m_baseImageStrengthContainer->hide();
    contentLayout->addWidget(m_baseImageStrengthContainer);
    connect(m_baseImageReference,
            &ReferenceSlotWidget::assignmentChanged,
            this,
            [this](const QString &title, const ImageAsset *asset) {
                m_baseImage = asset;
                m_baseImageHelper->setVisible(asset == nullptr);
                m_baseImageStrengthContainer->setVisible(asset != nullptr);
                emit referenceChanged(title, asset);
            });
    contentLayout->addStretch();

    scrollArea->setWidget(content);
    layout->addWidget(scrollArea, 1);

    m_generateButton = new QPushButton(tr("Generate"), this);
    m_generateButton->setObjectName(QStringLiteral("generateButton"));
    m_generateButton->setAccessibleName(tr("Generate image"));
    m_generateButton->setDefault(true);
    m_generateButton->setEnabled(false);
    connect(m_generateButton, &QPushButton::clicked, this, [this] {
        ImageGenerationRequest request;
        request.prompt = m_promptEdit->toPlainText();
        request.negativePrompt = m_negativePromptEdit->toPlainText();
        request.sampler = m_samplerSelector->currentData().toString();
        request.scheduler = m_schedulerSelector->currentData().toString();
        request.width = m_widthSpinBox->value();
        request.height = m_heightSpinBox->value();
        request.steps = m_stepsSpinBox->value();
        request.cfgScale = m_cfgSpinBox->value();
        request.seed = m_seedSpinBox->value();
        if (m_baseImage != nullptr) {
            request.baseImagePath = m_baseImage->filePath;
            request.baseImageStrength = m_baseImageStrengthSlider->value() / 100.0;
        }
        emit generationRequested(request);
    });
    layout->addWidget(m_generateButton);

    connect(m_promptEdit, &QPlainTextEdit::textChanged, this, &GeneratePanel::updateGenerateEnabled);
}

void GeneratePanel::setGenerationRunning(bool running)
{
    m_generationRunning = running;
    m_generateButton->setText(running ? tr("Generating…") : tr("Generate"));
    updateGenerateEnabled();
}

void GeneratePanel::setGenerationAvailable(bool available)
{
    m_generationAvailable = available;
    updateGenerateEnabled();
}

void GeneratePanel::setServiceStatus(const QString &status)
{
#if defined(Q_OS_MACOS)
    m_serviceStatus->setText(status);
#else
    Q_UNUSED(status)
#endif
}

void GeneratePanel::updateGenerateEnabled()
{
    m_generateButton->setEnabled(m_generationAvailable && !m_generationRunning && !m_promptEdit->toPlainText().trimmed().isEmpty());
}

void GeneratePanel::setSelectedAsset(const ImageAsset *asset)
{
    m_baseImageReference->setSelectedAsset(asset);
}

void GeneratePanel::removeAsset(const ImageAsset *asset)
{
    m_baseImageReference->clearAsset(asset);
}
