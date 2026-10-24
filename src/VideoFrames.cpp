#include "VideoFrames.h"
#include <QBuffer>
#include <QPainter>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <cmath>
#include <memory>

namespace VideoFrames
{
void readFirstFrame(const QByteArray& video, QObject* context, std::function<void(const QImage&)> done)
{
    auto* player = new QMediaPlayer(context);
    auto* buffer = new QBuffer(player);
    buffer->setData(video);
    buffer->open(QIODevice::ReadOnly);
    auto* sink = new QVideoSink(player);
    player->setVideoSink(sink);
    auto* timeout = new QTimer(player);
    timeout->setSingleShot(true);
    const auto finished = std::make_shared<bool>(false);
    const auto finish = [player, finished, done](const QImage& frame) {
        if (std::exchange(*finished, true)) return;
        player->stop();
        player->deleteLater();
        done(frame);
    };
    QObject::connect(sink, &QVideoSink::videoFrameChanged, player, [finish](const QVideoFrame& frame) {
        if (frame.isValid()) finish(frame.toImage());
    });
    QObject::connect(player, &QMediaPlayer::errorOccurred, player, [finish] { finish({}); });
    QObject::connect(player, &QMediaPlayer::mediaStatusChanged, player, [finish](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::InvalidMedia || status == QMediaPlayer::EndOfMedia) finish({});
    });
    // A video that never shows a frame is unreadable rather than a job that waits forever.
    QObject::connect(timeout, &QTimer::timeout, player, [finish] { finish({}); });
    timeout->start(15000);
    // The name tells the backend the container; the bytes stay in memory.
    player->setSourceDevice(buffer, QUrl(QStringLiteral("animation.mp4")));
    player->play();
}

void readAllFrames(const QByteArray& video, QObject* context, std::function<void(const QList<QImage>&)> done)
{
    auto* player = new QMediaPlayer(context);
    auto* buffer = new QBuffer(player);
    buffer->setData(video);
    buffer->open(QIODevice::ReadOnly);
    auto* sink = new QVideoSink(player);
    player->setVideoSink(sink);
    auto* timeout = new QTimer(player);
    timeout->setSingleShot(true);
    const auto finished = std::make_shared<bool>(false);
    const auto frames = std::make_shared<QList<QImage>>();
    const auto lastStart = std::make_shared<qint64>(-1);
    const auto finish = [player, finished, frames, done](bool complete) {
        if (std::exchange(*finished, true)) return;
        player->stop();
        player->deleteLater();
        done(complete ? *frames : QList<QImage>());
    };
    // A frame is told from the previous one by its start time, which the player repeats while it renders.
    QObject::connect(sink, &QVideoSink::videoFrameChanged, player, [frames, lastStart](const QVideoFrame& frame) {
        if (!frame.isValid() || (frame.startTime() >= 0 && frame.startTime() == *lastStart)) return;
        *lastStart = frame.startTime();
        frames->append(frame.toImage());
    });
    QObject::connect(player, &QMediaPlayer::errorOccurred, player, [finish] { finish(false); });
    QObject::connect(player, &QMediaPlayer::mediaStatusChanged, player,
        [finish, frames](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::InvalidMedia) finish(false);
            else if (status == QMediaPlayer::EndOfMedia) finish(!frames->isEmpty());
        });
    // The video plays once at its own pace; one that never ends is unreadable rather than a wait forever.
    QObject::connect(timeout, &QTimer::timeout, player, [finish] { finish(false); });
    timeout->start(60000);
    player->setSourceDevice(buffer, QUrl(QStringLiteral("animation.mp4")));
    player->play();
}

QSize spriteSheetGrid(int count)
{
    if (count < 1) return {};
    const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    return {columns, (count + columns - 1) / columns};
}

QImage spriteSheet(const QList<QImage>& frames)
{
    if (frames.isEmpty() || frames.first().isNull()) return {};
    const QSize cell = frames.first().size();
    for (const auto& frame : frames)
        if (frame.size() != cell) return {};
    const QSize grid = spriteSheetGrid(static_cast<int>(frames.size()));
    QImage sheet(cell.width() * grid.width(), cell.height() * grid.height(), QImage::Format_ARGB32);
    sheet.fill(Qt::transparent);
    QPainter painter(&sheet);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (int i = 0; i < frames.size(); ++i)
        painter.drawImage(QPoint((i % grid.width()) * cell.width(), (i / grid.width()) * cell.height()), frames.at(i));
    return sheet;
}
}
