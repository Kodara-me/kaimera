#include "UpscaleDialog.h"


#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>

namespace
{
QString resolutionText(const QSize &size)
{
    return QObject::tr("%1 × %2").arg(size.width()).arg(size.height());
}
}

UpscaleDialog::UpscaleDialog(const QSize &sourceSize, QWidget *parent)
    : QDialog(parent)
    , m_sourceSize(sourceSize)
{
    setWindowTitle(tr("Upscale Image"));

    auto *layout = new QFormLayout(this);
    auto *baseSizeLabel = new QLabel(resolutionText(m_sourceSize), this);
    baseSizeLabel->setObjectName(QStringLiteral("upscaleBaseResolutionLabel"));
    layout->addRow(tr("Base resolution"), baseSizeLabel);

    m_scaleSelector = new QComboBox(this);
    m_scaleSelector->setObjectName(QStringLiteral("upscaleScaleSelector"));
    m_scaleSelector->addItem(tr("2×"), 2);
    m_scaleSelector->addItem(tr("4×"), 4);
    layout->addRow(tr("Scale"), m_scaleSelector);

    m_outputSizeLabel = new QLabel(this);
    m_outputSizeLabel->setObjectName(QStringLiteral("upscaleFinalResolutionLabel"));
    layout->addRow(tr("Final resolution"), m_outputSizeLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addRow(buttons);

    connect(m_scaleSelector, &QComboBox::currentIndexChanged, this, &UpscaleDialog::updateOutputSize);
    updateOutputSize();
}

int UpscaleDialog::scale() const
{
    return m_scaleSelector->currentData().toInt();
}

QSize UpscaleDialog::outputSize() const
{
    return {m_sourceSize.width() * scale(), m_sourceSize.height() * scale()};
}

void UpscaleDialog::updateOutputSize()
{
    m_outputSizeLabel->setText(resolutionText(outputSize()));
}
