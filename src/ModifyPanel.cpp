#include "ModifyPanel.h"
#include "ImageAsset.h"
#include "ReferenceSlotWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStatusBar>
#include <QVBoxLayout>

ModifyPanel::ModifyPanel(QStatusBar *statusBar, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    auto *content = new QWidget(scrollArea);
    auto *contentLayout = new QVBoxLayout(content);

    auto *sourceLabel = new QLabel(tr("Source"), content);
    contentLayout->addWidget(sourceLabel);
    m_sourceName = new QLabel(tr("Current image"), content);
    contentLayout->addWidget(m_sourceName);
    m_sourceSize = new QLabel(tr("1024 × 768"), content);
    contentLayout->addWidget(m_sourceSize);

    auto *modificationPromptLabel = new QLabel(tr("Modification Prompt"), content);
    contentLayout->addWidget(modificationPromptLabel);
    auto *modificationPrompt = new QPlainTextEdit(content);
    modificationPrompt->setPlaceholderText(tr("Describe what you want to change..."));
    modificationPrompt->setMinimumHeight(90);
    contentLayout->addWidget(modificationPrompt);

    auto *negativePromptLabel = new QLabel(tr("Negative Prompt"), content);
    contentLayout->addWidget(negativePromptLabel);
    auto *negativePrompt = new QPlainTextEdit(content);
    negativePrompt->setPlaceholderText(tr("Things to avoid..."));
    negativePrompt->setFixedHeight(60);
    contentLayout->addWidget(negativePrompt);

    auto *strengthLabel = new QLabel(tr("Strength"), content);
    contentLayout->addWidget(strengthLabel);
    auto *strengthLayout = new QHBoxLayout;
    auto *strengthSlider = new QSlider(Qt::Horizontal, content);
    strengthSlider->setRange(0, 100);
    strengthSlider->setValue(45);
    strengthLayout->addWidget(strengthSlider, 1);
    auto *strengthValue = new QLabel(QStringLiteral("0.45"), content);
    strengthLayout->addWidget(strengthValue);
    contentLayout->addLayout(strengthLayout);

    auto *strengthDescriptionLayout = new QHBoxLayout;
    strengthDescriptionLayout->addWidget(new QLabel(tr("Preserve"), content));
    strengthDescriptionLayout->addStretch();
    strengthDescriptionLayout->addWidget(new QLabel(tr("Transform"), content));
    contentLayout->addLayout(strengthDescriptionLayout);
    connect(strengthSlider, &QSlider::valueChanged, this, [strengthValue](int value) {
        strengthValue->setText(QString::number(value / 100.0, 'f', 2));
    });

    auto *referencesLabel = new QLabel(tr("References"), content);
    contentLayout->addWidget(referencesLabel);
    m_faceReference = new ReferenceSlotWidget(tr("Face Reference"), content);
    contentLayout->addWidget(m_faceReference);
    m_styleReference = new ReferenceSlotWidget(tr("Style Reference"), content);
    contentLayout->addWidget(m_styleReference);
    m_maskReference = new ReferenceSlotWidget(tr("Mask"), content);
    contentLayout->addWidget(m_maskReference);
    connect(m_faceReference, &ReferenceSlotWidget::assignmentChanged, this, &ModifyPanel::referenceChanged);
    connect(m_styleReference, &ReferenceSlotWidget::assignmentChanged, this, &ModifyPanel::referenceChanged);
    connect(m_maskReference, &ReferenceSlotWidget::assignmentChanged, this, &ModifyPanel::referenceChanged);
    contentLayout->addStretch();

    scrollArea->setWidget(content);
    layout->addWidget(scrollArea, 1);

    m_modifyButton = new QPushButton(tr("Modify"), this);
    m_modifyButton->setDefault(true);
    m_modifyButton->setEnabled(false);
    connect(m_modifyButton, &QPushButton::clicked, statusBar, [statusBar] {
        statusBar->showMessage(QObject::tr("Modification not connected yet"));
    });
    layout->addWidget(m_modifyButton);
}

void ModifyPanel::setSourceImage(const ImageAsset &asset)
{
    m_sourceName->setText(asset.name);
    m_sourceSize->setText(tr("%1 × %2").arg(asset.image.width()).arg(asset.image.height()));
    m_sourcePath = asset.filePath;
    m_modifyButton->setEnabled(true);
}

void ModifyPanel::clearSourceImage()
{
    m_sourceName->setText(tr("No image selected"));
    m_sourceSize->clear();
    m_sourcePath.clear();
    m_modifyButton->setEnabled(false);
}

void ModifyPanel::setSelectedAsset(const ImageAsset *asset)
{
    m_faceReference->setSelectedAsset(asset);
    m_styleReference->setSelectedAsset(asset);
    m_maskReference->setSelectedAsset(asset);
}

void ModifyPanel::removeAsset(const ImageAsset *asset)
{
    if (asset == nullptr) {
        return;
    }

    if (m_sourcePath == asset->filePath) {
        clearSourceImage();
    }
    m_faceReference->clearAsset(asset);
    m_styleReference->clearAsset(asset);
    m_maskReference->clearAsset(asset);
}
