#include "VideoFrames.h"
#include <QBuffer>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
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
}
