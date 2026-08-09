#include "ModelDiscovery.h"

#include "AppPaths.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace
{
QList<ModelInfo> discoverModels(const QString &directoryPath, const QStringList &extensions)
{
    QDir directory(directoryPath);
    QList<ModelInfo> models;
    for (const QFileInfo &fileInfo : directory.entryInfoList(QDir::Files | QDir::Readable, QDir::Name)) {
        if (!extensions.contains(fileInfo.suffix(), Qt::CaseInsensitive)) {
            continue;
        }
        models.append({fileInfo.completeBaseName(), fileInfo.absoluteFilePath()});
    }
    std::sort(models.begin(), models.end(), [](const ModelInfo &left, const ModelInfo &right) {
        return QString::compare(left.name, right.name, Qt::CaseInsensitive) < 0;
    });
    return models;
}
}

QList<ModelInfo> ModelDiscovery::generationModels()
{
    return discoverModels(AppPaths::modelsPath(), {QStringLiteral("safetensors"), QStringLiteral("ckpt"), QStringLiteral("gguf")});
}

QList<ModelInfo> ModelDiscovery::upscaleModels()
{
    return discoverModels(AppPaths::upscaleModelsPath(), {QStringLiteral("gguf")});
}
