#pragma once
#include <QAbstractListModel>
#include <QImage>
#include <QPixmap>
#include <QUuid>
class GenerationHistoryModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    // Queued generations hold a pending entry until their image replaces it or they fail.
    enum class State { Ready, Pending, Failed };
    // A still image, or an animation: a video the service returned, shown by its first frame.
    enum class Kind { Image, Animation };
    // An animation's video exactly as received, with what the service reported about it.
    struct Animation
    {
        QByteArray video;
        QString mimeType;
        int frames = 0;
        int fps = 0;
        QSize size;
    };
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        // Final positive prompt used by the service, empty when unknown.
        PromptRole,
        StateRole,
        // Pending entries: percentage (-1 when unknown) and status; failed entries: the message.
        ProgressRole,
        StatusRole,
        KindRole
    };
    explicit GenerationHistoryModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = { }) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QUuid appendImage(const QImage &image, const QString &sourceLabel, const QString &prompt = { },
        const QString &negativePrompt = { });
    // Appends a pending entry under id for a generation that has not delivered yet.
    bool appendPending(const QUuid& id, const QString& label, const QString& status);
    void setProgress(const QUuid& id, const QString& status, int percentage);
    // Replaces a pending entry with its image, keeping its place in the history.
    bool completePending(const QUuid& id, const QImage& image, const QString& prompt = { },
        const QString& negativePrompt = { });
    // Replaces a pending entry with an animation, its first frame as thumbnail with a video mark.
    bool completeAnimation(const QUuid& id, const Animation& animation, const QImage& firstFrame,
        const QString& prompt = { });
    void failPending(const QUuid& id, const QString& message);
    // A still image; null for animations, which are not images to edit.
    QImage image(const QUuid &id) const;
    Kind kind(const QUuid& id) const;
    // An animation entry's video; empty for other entries.
    Animation animation(const QUuid& id) const;
    // The service's final positive prompt of a generated entry, or empty.
    QString prompt(const QUuid& id) const;
    State state(const QUuid& id) const;
    bool removeImage(const QUuid& id);
    void clear();

private:
    struct Entry
    {
        QUuid id;
        QString label;
        QImage image;
        QPixmap thumbnail;
        QString prompt;
        QString negativePrompt;
        State state = State::Ready;
        int percentage = -1;
        QString status;
        Kind kind = Kind::Image;
        Animation animation;
    };
    int row(const QUuid& id) const;
    QList<Entry> m_entries;
};
