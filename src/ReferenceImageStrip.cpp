#include "ReferenceImageStrip.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/Theme.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>

namespace
{
// Rounded, cropped thumbnail of a reference image, numbered by submission order; a base
// image (number 0) has an accent border instead of a number. An ignored one is dimmed,
// with a warning border and badge.
class ReferenceThumbnail final : public QWidget
{
public:
    ReferenceThumbnail(const QImage& image, int number, QWidget* parent)
        : QWidget(parent), m_number(number)
    {
        setObjectName(QStringLiteral("referenceThumbnail"));
        setFixedSize(ReferenceImageStrip::ThumbnailSize, ReferenceImageStrip::ThumbnailSize);
        m_image = image.scaled(size() * devicePixelRatioF(), Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation);
        m_image.setDevicePixelRatio(devicePixelRatioF());
    }
    void setIgnored(bool ignored)
    {
        m_ignored = ignored;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& tokens = Ardoise::Theme::tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(frame, tokens.metrics.radiusMedium, tokens.metrics.radiusMedium);
        painter.save();
        painter.setClipPath(clip);
        const QSizeF imageSize = QSizeF(m_image.size()) / m_image.devicePixelRatio();
        if (m_ignored) painter.setOpacity(0.35);
        painter.drawImage(QPointF((width() - imageSize.width()) / 2, (height() - imageSize.height()) / 2), m_image);
        painter.restore();
        painter.setBrush(Qt::NoBrush);
        if (m_number == 0 || m_ignored) {
            const qreal inset = tokens.metrics.focusBorderWidth / 2.0;
            painter.setPen(QPen(m_ignored ? tokens.colors.warning : tokens.colors.borderAccent,
                tokens.metrics.focusBorderWidth));
            painter.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset),
                tokens.metrics.radiusMedium, tokens.metrics.radiusMedium);
            if (m_number == 0) return;
        } else {
            painter.setPen(QPen(tokens.colors.borderNormal, 1));
            painter.drawPath(clip);
        }
        const qreal badge = tokens.metrics.iconSizeSmall;
        const QRectF badgeRect(tokens.metrics.spacing2, height() - badge - tokens.metrics.spacing2, badge, badge);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_ignored ? tokens.colors.warning : tokens.colors.accent);
        painter.drawEllipse(badgeRect);
        QFont font = painter.font();
        font.setPointSizeF(font.pointSizeF() + tokens.typography.captionOffset);
        painter.setFont(font);
        painter.setPen(tokens.colors.selectionText);
        painter.drawText(badgeRect, Qt::AlignCenter, QString::number(m_number));
    }

private:
    QImage m_image;
    int m_number;
    bool m_ignored = false;
};
}

ReferenceImageStrip::ReferenceImageStrip(const QString& removeButtonName, QWidget* parent)
    : QWidget(parent), m_removeButtonName(removeButtonName)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(Ardoise::Theme::tokens().metrics.spacing8);
    m_count = new QLabel(this);
    Ardoise::Theme::applyTypography(m_count, Ardoise::TextRole::Caption);
    m_layout->addWidget(m_count);
    m_layout->addStretch();
    hide();
}

bool ReferenceImageStrip::addReference(const QImage& image)
{
    if (image.isNull() || isFull())
        return false;
    m_references.append(image);
    updateReferences();
    return true;
}

bool ReferenceImageStrip::addReferences(const QList<QImage>& images)
{
    if (images.isEmpty() || m_references.size() + images.size() > m_maximum)
        return false;
    for (const auto& image : images)
        if (image.isNull()) return false;
    m_references.append(images);
    updateReferences();
    return true;
}

void ReferenceImageStrip::removeReference(int index)
{
    if (index < 0 || index >= m_references.size())
        return;
    m_references.removeAt(index);
    updateReferences();
}

void ReferenceImageStrip::clearReferences()
{
    if (m_references.isEmpty())
        return;
    m_references.clear();
    updateReferences();
}

void ReferenceImageStrip::setCountVisible(bool visible)
{
    m_count->setVisible(visible);
}

void ReferenceImageStrip::updateReferences()
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    // Rebuild the thumbnails in front of the counter and trailing stretch.
    while (m_layout->count() > 2) {
        // Deferred: the removal may come from a button inside this thumbnail.
        auto* item = m_layout->takeAt(0);
        item->widget()->hide();
        item->widget()->deleteLater();
        delete item;
    }
    for (int i = 0; i < m_references.size(); ++i) {
        auto* thumbnail = new ReferenceThumbnail(m_references.at(i), m_base ? 0 : i + 1, this);
        m_layout->insertWidget(i, thumbnail);
        if (m_removeButtonName.isEmpty())
            continue;
        auto* remove = new Ardoise::IconButton(Ardoise::Icon::Clear,
            m_base ? tr("Remove the base image") : tr("Remove reference image %1").arg(i + 1),
            thumbnail);
        remove->setObjectName(m_removeButtonName);
        remove->setFixedSize(metrics.iconSizeSmall + metrics.spacing2, metrics.iconSizeSmall + metrics.spacing2);
        remove->setIconSize(QSize(metrics.iconSizeSmall - metrics.spacing4, metrics.iconSizeSmall - metrics.spacing4));
        remove->move(thumbnail->width() - remove->width() - metrics.spacing2, metrics.spacing2);
        connect(remove, &QToolButton::clicked, this, [this, i] { removeReference(i); });
    }
    m_count->setText(tr("%1 of %2 references").arg(m_references.size()).arg(m_maximum));
    setVisible(!m_references.isEmpty());
    showIgnored();
    emit referencesChanged();
}

void ReferenceImageStrip::setIgnored(const QString& reason)
{
    if (reason == m_ignored) return;
    m_ignored = reason;
    showIgnored();
}

void ReferenceImageStrip::showIgnored()
{
    // The thumbnails come before the counter and trailing stretch.
    for (int i = 0; i + 2 < m_layout->count(); ++i) {
        auto* thumbnail = static_cast<ReferenceThumbnail*>(m_layout->itemAt(i)->widget());
        thumbnail->setIgnored(!m_ignored.isEmpty());
        const QString name = m_base ? tr("Base image") : tr("Reference image %1").arg(i + 1);
        thumbnail->setToolTip(m_ignored.isEmpty() ? name : name + QStringLiteral("\n") + m_ignored);
    }
}
