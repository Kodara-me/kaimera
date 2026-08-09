#pragma once

#include "CuratedResource.h"

#include <QList>
#include <QQueue>
#include <QString>

#include <QObject>

#include <memory>
#include <optional>

class QNetworkAccessManager;

class CuratedResourceManager final : public QObject
{
    Q_OBJECT

public:
    explicit CuratedResourceManager(QObject *parent = nullptr);
    CuratedResourceManager(QList<CuratedResourceDescriptor> descriptors,
                           const QString &resourceRoot,
                           QObject *parent = nullptr);
    ~CuratedResourceManager() override;

    QList<CuratedResourceDescriptor> resources() const;
    std::optional<CuratedResourceDescriptor> descriptor(CuratedResourceId id) const;
    QList<CuratedResourceId> requiredAtStartupResourceIds() const;
    QString displayName(CuratedResourceId id) const;
    std::optional<qint64> expectedFileSize(CuratedResourceId id) const;
    QString rootPath() const;
    QString path(CuratedResourceId id) const;
    bool exists(CuratedResourceId id) const;
    bool isInstalled(CuratedResourceId id) const;
    QList<CuratedResourceDescriptor> requiredAtStartupResources() const;
    bool ensureRequiredResourceDirectories(QString *errorMessage = nullptr) const;

    void download(CuratedResourceId id);
    void downloadMissing(const QList<CuratedResourceId> &resourceIds);

signals:
    void progressChanged(const CuratedResourceProgress &progress);
    void downloadFinished(CuratedResourceId id);
    void downloadFailed(CuratedResourceId id, const QString &errorMessage);

private:
    struct DownloadJob;

    bool ensureResourceDirectory(const CuratedResourceDescriptor &resource, QString *errorMessage) const;
    QString metadataPath(CuratedResourceId id) const;
    bool hasDownloadMetadata(const CuratedResourceDescriptor &resource) const;
    bool writeInstallationMetadata(const CuratedResourceDescriptor &resource, qint64 fileSize) const;
    void emitProgress(const CuratedResourceDescriptor &resource,
                      CuratedResourceState state,
                      qint64 bytesDownloaded = 0,
                      std::optional<qint64> totalBytes = std::nullopt);
    void startNextDownload();
    void startDownload(const CuratedResourceDescriptor &resource);
    void writeAvailableData();
    void finishNetworkDownload();
    void startVerification();
    void verifyNextChunk();
    void completeActiveDownload();
    void failActiveDownload(const QString &errorMessage);

    QList<CuratedResourceDescriptor> m_resources;
    std::optional<QString> m_resourceRootOverride;
    QNetworkAccessManager *m_networkManager;
    QQueue<CuratedResourceId> m_pendingDownloads;
    std::unique_ptr<DownloadJob> m_activeDownload;
};
