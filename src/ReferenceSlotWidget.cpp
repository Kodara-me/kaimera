#include "ReferenceSlotWidget.h"

#include "SystemIcon.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

ReferenceSlotWidget::ReferenceSlotWidget(const QString &title, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_thumbnail = new QLabel(this);
    m_thumbnail->setFixedSize(48, 42);
    m_thumbnail->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_thumbnail);

    layout->addWidget(new QLabel(m_title, this), 1);

    m_assignButton = new QToolButton(this);
    m_assignButton->setIcon(systemIcon(QStringLiteral("list-add"), QStyle::SP_FileDialogNewFolder, this));
    m_assignButton->setText(tr("Use Selected"));
    m_assignButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_assignButton->setToolTip(tr("Use selected library image"));
    connect(m_assignButton, &QToolButton::clicked, this, [this] {
        if (m_selectedAsset == nullptr) {
            return;
        }

        m_assignedAsset = m_selectedAsset;
        updateDisplay();
        emit assignmentChanged(m_title, m_assignedAsset);
    });
    layout->addWidget(m_assignButton);

    m_clearButton = new QToolButton(this);
    m_clearButton->setIcon(systemIcon(QStringLiteral("edit-clear"), QStyle::SP_DialogCancelButton, this));
    m_clearButton->setToolTip(tr("Clear %1").arg(m_title));
    connect(m_clearButton, &QToolButton::clicked, this, [this] {
        if (m_assignedAsset == nullptr) {
            return;
        }

        m_assignedAsset = nullptr;
        updateDisplay();
        emit assignmentChanged(m_title, nullptr);
    });
    layout->addWidget(m_clearButton);

    setSelectedAsset(nullptr);
    updateDisplay();
}

void ReferenceSlotWidget::setSelectedAsset(const ImageAsset *asset)
{
    m_selectedAsset = asset;
    m_assignButton->setEnabled(m_selectedAsset != nullptr);
}

void ReferenceSlotWidget::clearAsset(const ImageAsset *asset)
{
    if (asset == nullptr) {
        return;
    }

    if (m_selectedAsset == asset) {
        setSelectedAsset(nullptr);
    }
    if (m_assignedAsset != asset) {
        return;
    }

    m_assignedAsset = nullptr;
    updateDisplay();
    emit assignmentChanged(m_title, nullptr);
}

void ReferenceSlotWidget::updateDisplay()
{
    const bool isPopulated = m_assignedAsset != nullptr;
    m_clearButton->setEnabled(isPopulated);
    if (isPopulated) {
        m_thumbnail->setPixmap(QPixmap::fromImage(m_assignedAsset->image).scaled(
            m_thumbnail->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        return;
    }

    QPixmap placeholder(m_thumbnail->size());
    placeholder.fill(palette().color(QPalette::AlternateBase));
    QPainter painter(&placeholder);
    const QPixmap icon = systemIcon(QStringLiteral("list-add"), QStyle::SP_FileDialogNewFolder, this).pixmap(QSize(20, 20));
    painter.drawPixmap((placeholder.width() - icon.width()) / 2,
                       (placeholder.height() - icon.height()) / 2,
                       icon);
    m_thumbnail->setPixmap(placeholder);
}
