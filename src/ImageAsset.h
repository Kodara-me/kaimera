#pragma once

#include <QImage>
#include <QString>

enum class ImageAssetCategory
{
    Generated,
    Imported,
    Modified,
    Upscaled
};

struct ImageAsset
{
    QString id;
    QString name;
    QString filePath;
    QImage image;
    ImageAssetCategory category = ImageAssetCategory::Imported;
};
