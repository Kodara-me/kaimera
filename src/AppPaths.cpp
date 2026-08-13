#include "AppPaths.h"

#include <QDir>
#include <QObject>
#include <QStandardPaths>

#include <array>

QString AppPaths::managedRootPath()
{
    const QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return picturesPath.isEmpty() ? QString() : QDir(picturesPath).filePath(QStringLiteral("Kira Canvas"));
}

QString AppPaths::generatedPath()
{
    const QString rootPath = managedRootPath();
    return rootPath.isEmpty() ? QString() : QDir(rootPath).filePath(QStringLiteral("Generated"));
}

QString AppPaths::upscaledPath()
{
    const QString rootPath = managedRootPath();
    return rootPath.isEmpty() ? QString() : QDir(rootPath).filePath(QStringLiteral("Upscaled"));
}

QString AppPaths::applicationDataRoot()
{
    const QString dataPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return dataPath.isEmpty() ? QString() : QDir(dataPath).filePath(QStringLiteral("kira-canvas"));
}

QString AppPaths::modelsPath()
{
    const QString dataRoot = applicationDataRoot();
    return dataRoot.isEmpty() ? QString() : QDir(dataRoot).filePath(QStringLiteral("models"));
}

QString AppPaths::upscaleModelsPath()
{
    const QString modelsDirectory = modelsPath();
    return modelsDirectory.isEmpty() ? QString() : QDir(modelsDirectory).filePath(QStringLiteral("upscale"));
}

bool AppPaths::ensureManagedDirectories(QString *errorMessage)
{
    const QString rootPath = managedRootPath();
    if (rootPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Pictures directory is unavailable");
        }
        return false;
    }

    const std::array<QString, 3> paths = {rootPath, generatedPath(), upscaledPath()};
    for (const QString &path : paths) {
        if (!QDir().mkpath(path)) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("Could not create %1").arg(path);
            }
            return false;
        }
    }
    return true;
}

bool AppPaths::ensureModelDirectories(QString *errorMessage)
{
    const QString dataRoot = applicationDataRoot();
    if (dataRoot.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Application data directory is unavailable");
        }
        return false;
    }

    const std::array<QString, 3> paths = {dataRoot, modelsPath(), upscaleModelsPath()};
    for (const QString &path : paths) {
        if (!QDir().mkpath(path)) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("Could not create %1").arg(path);
            }
            return false;
        }
    }
    return true;
}
