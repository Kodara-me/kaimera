#pragma once

#include <QString>
#include <QUrl>

#include <QMetaType>
#include <QtTypes>

#include <optional>

enum class CuratedResourceId
{
    ImageGenerationDefault,
    Upscaler2x,
    Upscaler4x
};

enum class CuratedResourceAcquisitionPolicy
{
    RequiredAtStartup,
    OnDemand
};

struct CuratedResourceDescriptor
{
    CuratedResourceId id;
    QString displayName;
    CuratedResourceAcquisitionPolicy acquisitionPolicy;
    std::optional<QString> version;
    QString relativeDestinationDirectory;
    std::optional<QString> localFileName;
    std::optional<QUrl> downloadUrl;
    std::optional<QString> expectedSha256;
    std::optional<qint64> expectedFileSize;
    std::optional<QString> license;
    std::optional<QUrl> sourceRepositoryUrl;
    std::optional<QString> upstreamRevision;
};

enum class CuratedResourceState
{
    Pending,
    Downloading,
    Verifying,
    Ready,
    Failed
};

struct CuratedResourceProgress
{
    CuratedResourceId id;
    QString displayName;
    qint64 bytesDownloaded = 0;
    std::optional<qint64> totalBytes;
    std::optional<int> percentage;
    CuratedResourceState state = CuratedResourceState::Pending;
};

Q_DECLARE_METATYPE(CuratedResourceProgress)
