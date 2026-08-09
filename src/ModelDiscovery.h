#pragma once

#include "ModelInfo.h"

#include <QList>

class ModelDiscovery final
{
public:
    static QList<ModelInfo> generationModels();
    static QList<ModelInfo> upscaleModels();
};
