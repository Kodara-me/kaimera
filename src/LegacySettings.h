#pragma once

class QSettings;
class QString;

// Kaimera was named Kira Canvas. These copy what that name saved, once, while nothing
// is saved under the current name; the former settings and files are left untouched.
void migrateLegacySettings(const QSettings& legacy, QSettings& current);
void migrateLegacyDataFile(const QString& legacyDirectory, const QString& currentDirectory,
    const QString& fileName);
