#include "LegacySettings.h"

#include <QDir>
#include <QFile>
#include <QSettings>

void migrateLegacySettings(const QSettings& legacy, QSettings& current)
{
    if (!current.allKeys().isEmpty())
    {
        return;
    }
    for (const auto& key : legacy.allKeys())
    {
        current.setValue(key, legacy.value(key));
    }
    current.sync();
}

void migrateLegacyDataFile(const QString& legacyDirectory, const QString& currentDirectory,
    const QString& fileName)
{
    const QString source = QDir(legacyDirectory).filePath(fileName);
    const QString target = QDir(currentDirectory).filePath(fileName);
    if (legacyDirectory.isEmpty() || currentDirectory.isEmpty() || !QFile::exists(source)
        || QFile::exists(target) || !QDir().mkpath(currentDirectory))
    {
        return;
    }
    QFile::copy(source, target);
}
