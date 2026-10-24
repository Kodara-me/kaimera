#include "GenerationHistoryModel.h"
#include <Ardoise/Theme.h>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>

namespace
{
QPixmap thumbnailOf(const QImage& image)
{
    return QPixmap::fromImage(image.scaled(Ardoise::Theme::tokens().metrics.thumbnailSize * 2,
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// An animation's thumbnail: its first frame with a play mark in the bottom-left corner.
QPixmap animationThumbnail(const QImage& firstFrame)
{
    QPixmap pixmap = thumbnailOf(firstFrame);
    const auto& tokens = Ardoise::Theme::tokens();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal side = qMax(12.0, qMin(pixmap.width(), pixmap.height()) * 0.3);
    const qreal margin = side * 0.25;
    const QRectF badge(margin, pixmap.height() - margin - side, side, side);
    QColor background = tokens.colors.backgroundElevated;
    background.setAlpha(220);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawEllipse(badge);
    QPainterPath play;
    const QPointF center = badge.center();
    const qreal r = side * 0.24;
    play.moveTo(center.x() - r * 0.7, center.y() - r);
    play.lineTo(center.x() + r, center.y());
    play.lineTo(center.x() - r * 0.7, center.y() + r);
    play.closeSubpath();
    painter.setBrush(tokens.colors.textPrimary);
    painter.drawPath(play);
    return pixmap;
}
}

GenerationHistoryModel::GenerationHistoryModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int GenerationHistoryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant GenerationHistoryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return { };
    const auto& entry = m_entries.at(index.row());
    if (role == IdRole)
        return entry.id;
    if (role == StateRole)
        return int(entry.state);
    if (role == ProgressRole)
        return entry.percentage;
    if (role == StatusRole)
        return entry.status;
    if (role == KindRole)
        return int(entry.kind);
    if (entry.state != State::Ready && (role == Qt::ToolTipRole || role == Qt::AccessibleTextRole)) {
        if (entry.state == State::Failed)
            return tr("Generation failed: %1").arg(entry.status);
        return entry.percentage >= 0 ? tr("%1 (%2%)").arg(entry.status).arg(entry.percentage) : entry.status;
    }
    if (role == Qt::DecorationRole)
        return QIcon(entry.thumbnail);
    if (role == PromptRole)
        return entry.prompt;
    if (role == Qt::AccessibleTextRole || (role == Qt::ToolTipRole && entry.prompt.isEmpty()))
        return entry.label;
    if (role == Qt::ToolTipRole) {
        // Rich text lets long prompts wrap inside the tooltip.
        QString text = QStringLiteral("<qt><b>%1</b><br>%2").arg(entry.label.toHtmlEscaped(),
            tr("Prompt: %1").arg(entry.prompt.toHtmlEscaped()));
        if (!entry.negativePrompt.isEmpty())
            text += QStringLiteral("<br>") + tr("Negative: %1").arg(entry.negativePrompt.toHtmlEscaped());
        return text + QStringLiteral("</qt>");
    }
    return { };
}

QUuid GenerationHistoryModel::appendImage(const QImage& image, const QString& sourceLabel, const QString& prompt,
    const QString& negativePrompt)
{
    if (image.isNull())
        return { };
    const QUuid id = QUuid::createUuid();
    beginInsertRows({ }, m_entries.size(), m_entries.size());
    m_entries.append({ id, sourceLabel, image, thumbnailOf(image), prompt, negativePrompt, State::Ready, -1, { },
        Kind::Image, { } });
    endInsertRows();
    return id;
}

bool GenerationHistoryModel::appendPending(const QUuid& id, const QString& label, const QString& status)
{
    if (id.isNull() || row(id) >= 0)
        return false;
    beginInsertRows({ }, m_entries.size(), m_entries.size());
    Entry entry;
    entry.id = id;
    entry.label = label;
    entry.state = State::Pending;
    entry.status = status;
    m_entries.append(entry);
    endInsertRows();
    return true;
}

void GenerationHistoryModel::setProgress(const QUuid& id, const QString& status, int percentage)
{
    const int at = row(id);
    if (at < 0 || m_entries.at(at).state != State::Pending)
        return;
    auto& entry = m_entries[at];
    entry.status = status;
    entry.percentage = percentage;
    emit dataChanged(index(at), index(at));
}

bool GenerationHistoryModel::completePending(const QUuid& id, const QImage& image, const QString& prompt,
    const QString& negativePrompt)
{
    const int at = row(id);
    if (at < 0 || image.isNull() || m_entries.at(at).state != State::Pending)
        return false;
    auto& entry = m_entries[at];
    entry.state = State::Ready;
    entry.image = image;
    entry.thumbnail = thumbnailOf(image);
    entry.prompt = prompt;
    entry.negativePrompt = negativePrompt;
    entry.status.clear();
    emit dataChanged(index(at), index(at));
    return true;
}

bool GenerationHistoryModel::completeAnimation(const QUuid& id, const Animation& animation, const QImage& firstFrame,
    const QString& prompt)
{
    const int at = row(id);
    if (at < 0 || firstFrame.isNull() || animation.video.isEmpty() || m_entries.at(at).state != State::Pending)
        return false;
    auto& entry = m_entries[at];
    entry.state = State::Ready;
    entry.kind = Kind::Animation;
    entry.animation = animation;
    entry.thumbnail = animationThumbnail(firstFrame);
    entry.prompt = prompt;
    entry.status.clear();
    emit dataChanged(index(at), index(at));
    return true;
}

void GenerationHistoryModel::failPending(const QUuid& id, const QString& message)
{
    const int at = row(id);
    if (at < 0 || m_entries.at(at).state != State::Pending)
        return;
    auto& entry = m_entries[at];
    entry.state = State::Failed;
    entry.status = message;
    entry.percentage = -1;
    emit dataChanged(index(at), index(at));
}

int GenerationHistoryModel::row(const QUuid& id) const
{
    for (int at = 0; at < m_entries.size(); ++at)
        if (m_entries.at(at).id == id)
            return at;
    return -1;
}

QImage GenerationHistoryModel::image(const QUuid& id) const
{
    const int at = row(id);
    return at < 0 ? QImage() : m_entries.at(at).image;
}

QString GenerationHistoryModel::prompt(const QUuid& id) const
{
    const int at = row(id);
    return at < 0 ? QString() : m_entries.at(at).prompt;
}

GenerationHistoryModel::Kind GenerationHistoryModel::kind(const QUuid& id) const
{
    const int at = row(id);
    return at < 0 ? Kind::Image : m_entries.at(at).kind;
}

GenerationHistoryModel::Animation GenerationHistoryModel::animation(const QUuid& id) const
{
    const int at = row(id);
    return at < 0 ? Animation() : m_entries.at(at).animation;
}

GenerationHistoryModel::State GenerationHistoryModel::state(const QUuid& id) const
{
    const int at = row(id);
    return at < 0 ? State::Ready : m_entries.at(at).state;
}

void GenerationHistoryModel::clear()
{
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

bool GenerationHistoryModel::removeImage(const QUuid& id)
{
    for (int row = 0; row < m_entries.size(); ++row)
        if (m_entries.at(row).id == id)
        {
            beginRemoveRows({}, row, row);
            m_entries.removeAt(row);
            endRemoveRows();
            return true;
        }
    return false;
}
