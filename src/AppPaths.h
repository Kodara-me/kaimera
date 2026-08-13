#pragma once

#include <QString>

class AppPaths final
{
public:
    static QString managedRootPath();
    static QString generatedPath();
    static QString upscaledPath();
    static QString applicationDataRoot();
    static QString modelsPath();
    static QString upscaleModelsPath();
    static bool ensureManagedDirectories(QString *errorMessage = nullptr);
    static bool ensureModelDirectories(QString *errorMessage = nullptr);
};
