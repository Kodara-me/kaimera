#include "OutputSettingsWidget.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/Theme.h>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QLineEdit>
#include <QSettings>
#include <QStandardPaths>

OutputSettingsWidget::OutputSettingsWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_format = new QComboBox(this);
    m_format->setObjectName(QStringLiteral("outputFormat"));
    const auto supported = QImageWriter::supportedImageFormats();
    for (const QByteArray& format : { QByteArray("png"), QByteArray("jpeg"), QByteArray("webp") })
        if (supported.contains(format))
            m_format->addItem(QString::fromLatin1(format).toUpper(), format);
    form->addRow(tr("Output format:"), m_format);
    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Ardoise::Theme::tokens().metrics.spacing6);
    m_directory = new QLineEdit(row);
    m_directory->setObjectName(QStringLiteral("outputDirectory"));
    m_directory->setAccessibleName(tr("Output folder"));
    m_directory->setMinimumWidth(0);
    layout->addWidget(m_directory, 1);
    auto* browse = new Ardoise::IconButton(Ardoise::Icon::Folder, tr("Choose output folder"), row);
    layout->addWidget(browse);
    form->addRow(tr("Output folder:"), row);
    connect(browse, &QToolButton::clicked, this,
        [this]
        {
            const auto selected
                = QFileDialog::getExistingDirectory(this, tr("Choose output folder"), directory());
            if (!selected.isEmpty())
                m_directory->setText(selected);
        });
    resetDefaults();
    m_format->setCurrentIndex(m_format->findData(preferredFormat()));
    m_directory->setText(preferredDirectory());
}

QByteArray OutputSettingsWidget::format() const { return m_format->currentData().toByteArray(); }

QString OutputSettingsWidget::directory() const { return m_directory->text().trimmed(); }

void OutputSettingsWidget::resetDefaults()
{
    m_format->setCurrentIndex(0);
    m_directory->setText(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
}

void OutputSettingsWidget::savePreferences() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("Output/Format"), format());
    settings.setValue(QStringLiteral("Output/Directory"), directory());
}

QByteArray OutputSettingsWidget::preferredFormat()
{
    const auto format = QSettings().value(QStringLiteral("Output/Format"), QByteArray("png")).toByteArray();
    if ((format == "png" || format == "jpeg" || format == "webp")
        && QImageWriter::supportedImageFormats().contains(format))
        return format;
    return QByteArray("png");
}

QString OutputSettingsWidget::preferredDirectory()
{
    const auto directory = QSettings().value(QStringLiteral("Output/Directory")).toString().trimmed();
    return directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) : directory;
}
