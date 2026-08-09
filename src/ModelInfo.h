#pragma once

#include <QString>

enum class ModelType
{
    Generation,
    Upscaler
};

struct ModelInfo
{
    QString name;
    QString filePath;
};
