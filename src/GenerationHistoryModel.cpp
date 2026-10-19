#include "GenerationHistoryModel.h"
#include <Ardoise/Theme.h>
#include <QIcon>

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
    m_entries.append({ id, sourceLabel, image,
        QPixmap::fromImage(image.scaled(Ardoise::Theme::tokens().metrics.thumbnailSize * 2,
            Qt::KeepAspectRatio, Qt::SmoothTransformation)), prompt, negativePrompt, State::Ready, -1, { } });
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
    entry.thumbnail = QPixmap::fromImage(image.scaled(Ardoise::Theme::tokens().metrics.thumbnailSize * 2,
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    entry.prompt = prompt;
    entry.negativePrompt = negativePrompt;
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
