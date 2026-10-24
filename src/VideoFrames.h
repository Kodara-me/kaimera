#pragma once
#include <QByteArray>
#include <QImage>
#include <functional>
class QObject;
// Decoding the videos the service returns, through Qt Multimedia.
namespace VideoFrames
{
// Decodes video's first frame without showing or playing it aloud; done runs once, in context's
// thread, with a null image when the video cannot be decoded. Nothing runs if context is deleted.
void readFirstFrame(const QByteArray& video, QObject* context, std::function<void(const QImage&)> done);
}
