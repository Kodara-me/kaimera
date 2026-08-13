#pragma once

#include "GenerationProgress.h"

#include <QJsonObject>

namespace ServiceProtocol
{
constexpr int Version = 1;
QJsonObject progressToJson(const GenerationProgress &progress);
GenerationProgress progressFromJson(const QJsonObject &object);
QString stageName(GenerationProgressStage stage);
GenerationProgressStage stageFromName(const QString &name);
}
