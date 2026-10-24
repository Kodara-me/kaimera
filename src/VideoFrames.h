#pragma once
#include <QByteArray>
#include <QImage>
#include <QList>
#include <QSize>
#include <functional>
class QObject;
// Decoding the videos the service returns, through Qt Multimedia.
namespace VideoFrames
{
// Decodes video's first frame without showing or playing it aloud; done runs once, in context's
// thread, with a null image when the video cannot be decoded. Nothing runs if context is deleted.
void readFirstFrame(const QByteArray& video, QObject* context, std::function<void(const QImage&)> done);
// Decodes every frame of video in playback order; done runs once, in context's thread, with an empty
// list when the video cannot be decoded. Nothing runs if context is deleted.
void readAllFrames(const QByteArray& video, QObject* context, std::function<void(const QList<QImage>&)> done);
// The sprite sheet grid of count frames: ceil(sqrt(count)) columns, then as many rows as needed.
QSize spriteSheetGrid(int count);
// Every frame at full size in playback order, left to right then top to bottom, with transparent
// unused cells; null when there is no frame or the frames differ in size.
QImage spriteSheet(const QList<QImage>& frames);
}
