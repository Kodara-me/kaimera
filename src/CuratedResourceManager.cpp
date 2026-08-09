#include "CuratedResourceManager.h"

#include "AppPaths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cstdio>

namespace
{
constexpr qsizetype VerificationChunkSize = 1024 * 1024;

const QList<CuratedResourceDescriptor> &resourceDescriptors()
{
    static const QList<CuratedResourceDescriptor> descriptors = {
        {CuratedResourceId::ImageGenerationDefault,
         QStringLiteral("Image generation model"),
         CuratedResourceAcquisitionPolicy::RequiredAtStartup,
         QStringLiteral("cyberrealistic-xl-v10.0-fp16"),
         QStringLiteral("generation"),
         QStringLiteral("CyberRealisticXLPlay_V10.0_FP16.safetensors"),
         QUrl(QStringLiteral("https://huggingface.co/cyberdelia/CyberRealisticXL/resolve/c04ae13b465d19763561a834d625a3d504a4f315/CyberRealisticXLPlay_V10.0_FP16.safetensors?download=true")),
         QStringLiteral("fd5e870b5bbce4bddeb64f4bb8e49c57f84ab793c0262a503f0123be435e667d"),
         6938041288,
         QStringLiteral("CreativeML Open RAIL-M"),
         QUrl(QStringLiteral("https://huggingface.co/cyberdelia/CyberRealisticXL")),
         QStringLiteral("c04ae13b465d19763561a834d625a3d504a4f315")},
        {CuratedResourceId::Upscaler2x,
         QStringLiteral("2× upscaler"),
         CuratedResourceAcquisitionPolicy::RequiredAtStartup,
         QStringLiteral("hf-64b5812d"),
         QStringLiteral("upscale"),
         QStringLiteral("2x-ESRGAN.gguf"),
         QUrl(QStringLiteral("https://huggingface.co/wbruna/upscalers-sdcpp-gguf/resolve/64b5812d1cfd8f5edf06cdf31fa0677163a77c35/2x-ESRGAN.gguf?download=true")),
         QStringLiteral("eaa12716e05ddab63f417fef6265bd33608a821284000386f0ebe8bbaad05c70"),
         66697696,
         QStringLiteral("Apache-2.0"),
         QUrl(QStringLiteral("https://huggingface.co/wbruna/upscalers-sdcpp-gguf")),
         QStringLiteral("64b5812d1cfd8f5edf06cdf31fa0677163a77c35")},
        {CuratedResourceId::Upscaler4x,
         QStringLiteral("4× upscaler"),
         CuratedResourceAcquisitionPolicy::RequiredAtStartup,
         QStringLiteral("hf-5fb34a8a"),
         QStringLiteral("upscale"),
         QStringLiteral("RealESRGAN_x4plus.gguf"),
         QUrl(QStringLiteral("https://huggingface.co/wbruna/upscalers-sdcpp-gguf/resolve/5fb34a8a88480768438728544e90f14b9e28ddb7/RealESRGAN_x4plus.gguf?download=true")),
         QStringLiteral("5a391e116e82d79863b270229bd18f5c6eba9d513f87c128405f8a701d0288ad"),
         66839328,
         QStringLiteral("BSD-3-Clause"),
         QUrl(QStringLiteral("https://huggingface.co/wbruna/upscalers-sdcpp-gguf")),
         QStringLiteral("5fb34a8a88480768438728544e90f14b9e28ddb7")},
    };
    return descriptors;
}

QString resourceKey(CuratedResourceId id)
{
    switch (id) {
    case CuratedResourceId::ImageGenerationDefault:
        return QStringLiteral("image-generation-default");
    case CuratedResourceId::Upscaler2x:
        return QStringLiteral("upscaler-2x");
    case CuratedResourceId::Upscaler4x:
        return QStringLiteral("upscaler-4x");
    }
    return {};
}

QString unavailableDownloadMessage(const CuratedResourceDescriptor &resource)
{
    return QObject::tr("Download configuration for %1 is unavailable.").arg(resource.displayName);
}

bool isSafeRelativeDestinationDirectory(const QString &directory)
{
    const QString cleanDirectory = QDir::cleanPath(directory);
    return !cleanDirectory.isEmpty() && cleanDirectory != QStringLiteral(".")
           && !QDir::isAbsolutePath(cleanDirectory)
           && cleanDirectory != QStringLiteral("..")
           && !cleanDirectory.startsWith(QStringLiteral("../"));
}

bool isSafeFileName(const QString &fileName)
{
    return !fileName.isEmpty() && QFileInfo(fileName).fileName() == fileName;
}
}

struct CuratedResourceManager::DownloadJob
{
    explicit DownloadJob(const CuratedResourceDescriptor &resourceDescriptor)
        : resource(resourceDescriptor)
        , sha256(QCryptographicHash::Sha256)
    {
    }

    CuratedResourceDescriptor resource;
    QString partPath;
    QString finalPath;
    QNetworkReply *reply = nullptr;
    std::unique_ptr<QFile> partFile;
    std::unique_ptr<QFile> verificationFile;
    QCryptographicHash sha256;
    qint64 bytesDownloaded = 0;
    std::optional<qint64> totalBytes;
};

CuratedResourceManager::CuratedResourceManager(QObject *parent)
    : CuratedResourceManager(resourceDescriptors(), {}, parent)
{
}

CuratedResourceManager::CuratedResourceManager(QList<CuratedResourceDescriptor> descriptors,
                                                 const QString &resourceRoot,
                                                 QObject *parent)
    : QObject(parent)
    , m_resources(std::move(descriptors))
    , m_networkManager(new QNetworkAccessManager(this))
{
    qRegisterMetaType<CuratedResourceProgress>();
    if (!resourceRoot.isEmpty()) {
        m_resourceRootOverride = QDir::cleanPath(resourceRoot);
    }
}

CuratedResourceManager::~CuratedResourceManager() = default;

QList<CuratedResourceDescriptor> CuratedResourceManager::resources() const
{
    return m_resources;
}

std::optional<CuratedResourceDescriptor>
CuratedResourceManager::descriptor(CuratedResourceId id) const
{
    const auto iterator = std::find_if(m_resources.cbegin(), m_resources.cend(), [id](const CuratedResourceDescriptor &resource) {
        return resource.id == id;
    });
    if (iterator == m_resources.cend()) {
        return std::nullopt;
    }
    return *iterator;
}

QList<CuratedResourceId> CuratedResourceManager::requiredAtStartupResourceIds() const
{
    QList<CuratedResourceId> ids;
    for (const CuratedResourceDescriptor &resource : m_resources) {
        if (resource.acquisitionPolicy == CuratedResourceAcquisitionPolicy::RequiredAtStartup) {
            ids.append(resource.id);
        }
    }
    return ids;
}

QString CuratedResourceManager::displayName(CuratedResourceId id) const
{
    const std::optional<CuratedResourceDescriptor> resource = descriptor(id);
    return resource.has_value() ? resource->displayName : QString();
}

std::optional<qint64> CuratedResourceManager::expectedFileSize(CuratedResourceId id) const
{
    const std::optional<CuratedResourceDescriptor> resource = descriptor(id);
    return resource.has_value() ? resource->expectedFileSize : std::nullopt;
}

QString CuratedResourceManager::rootPath() const
{
    if (m_resourceRootOverride.has_value()) {
        return *m_resourceRootOverride;
    }

    const QString dataRoot = AppPaths::applicationDataRoot();
    return dataRoot.isEmpty() ? QString() : QDir(dataRoot).filePath(QStringLiteral("resources"));
}

QString CuratedResourceManager::path(CuratedResourceId id) const
{
    const std::optional<CuratedResourceDescriptor> resource = descriptor(id);
    if (!resource.has_value() || !resource->localFileName.has_value()
        || !isSafeRelativeDestinationDirectory(resource->relativeDestinationDirectory)
        || !isSafeFileName(*resource->localFileName)) {
        return {};
    }

    const QString resourcesRoot = rootPath();
    if (resourcesRoot.isEmpty()) {
        return {};
    }
    return QDir(resourcesRoot).filePath(
        QDir::cleanPath(resource->relativeDestinationDirectory + QLatin1Char('/') + *resource->localFileName));
}

bool CuratedResourceManager::exists(CuratedResourceId id) const
{
    const QString resourcePath = path(id);
    return !resourcePath.isEmpty() && QFileInfo(resourcePath).isFile();
}

bool CuratedResourceManager::isInstalled(CuratedResourceId id) const
{
    const std::optional<CuratedResourceDescriptor> resource = descriptor(id);
    if (!resource.has_value() || !hasDownloadMetadata(*resource)) {
        return false;
    }

    const QString resourcePath = path(id);
    const QString resourceMetadataPath = metadataPath(id);
    if (resourcePath.isEmpty() || resourceMetadataPath.isEmpty()) {
        return false;
    }

    const QFileInfo fileInfo(resourcePath);
    if (!fileInfo.isFile()) {
        return false;
    }

    QFile metadataFile(resourceMetadataPath);
    if (!metadataFile.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(metadataFile.readAll());
    if (!document.isObject()) {
        return false;
    }

    const QJsonObject metadata = document.object();
    const QString expectedHash = resource->expectedSha256->toLower();
    const qint64 recordedSize = metadata.value(QStringLiteral("fileSize")).toVariant().toLongLong();
    return metadata.value(QStringLiteral("resourceId")).toString() == resourceKey(id)
           && metadata.value(QStringLiteral("version")).toString() == *resource->version
           && metadata.value(QStringLiteral("sha256")).toString().toLower() == expectedHash
           && metadata.value(QStringLiteral("verified")).toBool()
           && recordedSize == fileInfo.size()
           && (!resource->expectedFileSize.has_value() || fileInfo.size() == *resource->expectedFileSize);
}

QList<CuratedResourceDescriptor>
CuratedResourceManager::requiredAtStartupResources() const
{
    QList<CuratedResourceDescriptor> requiredResources;
    for (const CuratedResourceDescriptor &resource : m_resources) {
        if (resource.acquisitionPolicy == CuratedResourceAcquisitionPolicy::RequiredAtStartup) {
            requiredResources.append(resource);
        }
    }
    return requiredResources;
}

bool CuratedResourceManager::ensureRequiredResourceDirectories(
    QString *errorMessage) const
{
    const QString resourcesRoot = rootPath();
    if (resourcesRoot.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Application data directory is unavailable");
        }
        return false;
    }

    QStringList directories = {resourcesRoot, QDir(resourcesRoot).filePath(QStringLiteral("metadata"))};
    for (const CuratedResourceDescriptor &resource : requiredAtStartupResources()) {
        directories.append(QDir(resourcesRoot).filePath(resource.relativeDestinationDirectory));
    }

    directories.removeDuplicates();
    for (const QString &directory : directories) {
        if (!QDir().mkpath(directory)) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("Could not create %1").arg(directory);
            }
            return false;
        }
    }
    return true;
}

void CuratedResourceManager::download(CuratedResourceId id)
{
    const std::optional<CuratedResourceDescriptor> resource = descriptor(id);
    if (!resource.has_value()) {
        return;
    }

    if (isInstalled(id)) {
        emitProgress(*resource, CuratedResourceState::Ready);
        emit downloadFinished(id);
        return;
    }

    if (!hasDownloadMetadata(*resource)) {
        emitProgress(*resource, CuratedResourceState::Failed);
        emit downloadFailed(id, unavailableDownloadMessage(*resource));
        return;
    }

    const bool isActive = m_activeDownload != nullptr && m_activeDownload->resource.id == id;
    if (isActive || m_pendingDownloads.contains(id)) {
        return;
    }

    m_pendingDownloads.enqueue(id);
    emitProgress(*resource, CuratedResourceState::Pending);
    startNextDownload();
}

void CuratedResourceManager::downloadMissing(const QList<CuratedResourceId> &resourceIds)
{
    for (const CuratedResourceId id : resourceIds) {
        if (!isInstalled(id)) {
            download(id);
        }
    }
}

bool CuratedResourceManager::ensureResourceDirectory(const CuratedResourceDescriptor &resource,
                                                      QString *errorMessage) const
{
    const QString resourcesRoot = rootPath();
    if (resourcesRoot.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Application data directory is unavailable");
        }
        return false;
    }

    const QString destinationDirectory = QDir(resourcesRoot).filePath(resource.relativeDestinationDirectory);
    const QString metadataDirectory = QDir(resourcesRoot).filePath(QStringLiteral("metadata"));
    for (const QString &directory : {destinationDirectory, metadataDirectory}) {
        if (!QDir().mkpath(directory)) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("Could not prepare storage for %1.").arg(resource.displayName);
            }
            return false;
        }
    }
    return true;
}

QString CuratedResourceManager::metadataPath(CuratedResourceId id) const
{
    const QString resourcesRoot = rootPath();
    const QString key = resourceKey(id);
    if (resourcesRoot.isEmpty() || key.isEmpty()) {
        return {};
    }
    return QDir(resourcesRoot).filePath(QStringLiteral("metadata/%1.json").arg(key));
}

bool CuratedResourceManager::hasDownloadMetadata(const CuratedResourceDescriptor &resource) const
{
    const QString scheme = resource.downloadUrl.has_value() ? resource.downloadUrl->scheme() : QString();
    const bool supportedScheme = scheme == QStringLiteral("https")
                                 || (m_resourceRootOverride.has_value() && scheme == QStringLiteral("http"));
    return resource.version.has_value() && !resource.version->isEmpty()
           && resource.localFileName.has_value() && isSafeFileName(*resource.localFileName)
           && isSafeRelativeDestinationDirectory(resource.relativeDestinationDirectory)
           && resource.downloadUrl.has_value() && resource.downloadUrl->isValid()
           && supportedScheme
           && resource.expectedSha256.has_value()
           && resource.expectedSha256->size() == 64
           && resource.expectedSha256->contains(QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")))
           && resource.expectedFileSize.has_value() && *resource.expectedFileSize > 0;
}

bool CuratedResourceManager::writeInstallationMetadata(const CuratedResourceDescriptor &resource,
                                                        qint64 fileSize) const
{
    const QString resourceMetadataPath = metadataPath(resource.id);
    if (resourceMetadataPath.isEmpty()) {
        return false;
    }

    QJsonObject metadata;
    metadata.insert(QStringLiteral("resourceId"), resourceKey(resource.id));
    metadata.insert(QStringLiteral("version"), *resource.version);
    metadata.insert(QStringLiteral("sha256"), resource.expectedSha256->toLower());
    metadata.insert(QStringLiteral("verified"), true);
    metadata.insert(QStringLiteral("fileSize"), fileSize);

    QSaveFile metadataFile(resourceMetadataPath);
    if (!metadataFile.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (metadataFile.write(QJsonDocument(metadata).toJson(QJsonDocument::Compact)) < 0) {
        return false;
    }
    return metadataFile.commit();
}

void CuratedResourceManager::emitProgress(const CuratedResourceDescriptor &resource,
                                          CuratedResourceState state,
                                          qint64 bytesDownloaded,
                                          std::optional<qint64> totalBytes)
{
    std::optional<int> percentage;
    if (totalBytes.has_value() && *totalBytes > 0) {
        percentage = static_cast<int>(std::min<qint64>(100, (bytesDownloaded * 100) / *totalBytes));
    }
    emit progressChanged({resource.id, resource.displayName, bytesDownloaded, totalBytes, percentage, state});
}

void CuratedResourceManager::startNextDownload()
{
    if (m_activeDownload != nullptr || m_pendingDownloads.isEmpty()) {
        return;
    }

    const CuratedResourceId id = m_pendingDownloads.dequeue();
    const std::optional<CuratedResourceDescriptor> resource = descriptor(id);
    if (!resource.has_value()) {
        startNextDownload();
        return;
    }
    startDownload(*resource);
}

void CuratedResourceManager::startDownload(const CuratedResourceDescriptor &resource)
{
    QString directoryError;
    if (!ensureResourceDirectory(resource, &directoryError)) {
        emitProgress(resource, CuratedResourceState::Failed);
        emit downloadFailed(resource.id, directoryError);
        startNextDownload();
        return;
    }

    auto download = std::make_unique<DownloadJob>(resource);
    download->finalPath = path(resource.id);
    download->partPath = download->finalPath + QStringLiteral(".part");
    if (download->finalPath.isEmpty()) {
        emitProgress(resource, CuratedResourceState::Failed);
        emit downloadFailed(resource.id, unavailableDownloadMessage(resource));
        startNextDownload();
        return;
    }

    if (QFileInfo::exists(download->partPath) && !QFile::remove(download->partPath)) {
        emitProgress(resource, CuratedResourceState::Failed);
        emit downloadFailed(resource.id, QObject::tr("Could not prepare storage for %1.").arg(resource.displayName));
        startNextDownload();
        return;
    }

    download->partFile = std::make_unique<QFile>(download->partPath);
    if (!download->partFile->open(QIODevice::WriteOnly)) {
        emitProgress(resource, CuratedResourceState::Failed);
        emit downloadFailed(resource.id, QObject::tr("Could not prepare storage for %1.").arg(resource.displayName));
        startNextDownload();
        return;
    }

    QNetworkRequest request(*resource.downloadUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_activeDownload = std::move(download);
    QNetworkReply *reply = m_networkManager->get(request);
    m_activeDownload->reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (m_activeDownload != nullptr && m_activeDownload->reply == reply) {
            writeAvailableData();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64 total) {
        if (m_activeDownload == nullptr || m_activeDownload->reply != reply) {
            return;
        }

        m_activeDownload->bytesDownloaded = received;
        m_activeDownload->totalBytes = total > 0 ? std::optional<qint64>(total) : std::nullopt;
        emitProgress(m_activeDownload->resource,
                     CuratedResourceState::Downloading,
                     received,
                     m_activeDownload->totalBytes);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_activeDownload != nullptr && m_activeDownload->reply == reply) {
            finishNetworkDownload();
        }
    });

    emitProgress(resource, CuratedResourceState::Downloading);
}

void CuratedResourceManager::writeAvailableData()
{
    const QByteArray data = m_activeDownload->reply->readAll();
    if (data.isEmpty()) {
        return;
    }
    if (m_activeDownload->partFile->write(data) != data.size()) {
        failActiveDownload(QObject::tr("Could not save %1.").arg(m_activeDownload->resource.displayName));
    }
}

void CuratedResourceManager::finishNetworkDownload()
{
    QNetworkReply *reply = m_activeDownload->reply;
    writeAvailableData();
    if (m_activeDownload == nullptr) {
        return;
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || statusCode < 200 || statusCode >= 300) {
        failActiveDownload(QObject::tr("Could not download %1.").arg(m_activeDownload->resource.displayName));
        return;
    }

    m_activeDownload->reply = nullptr;
    reply->deleteLater();
    m_activeDownload->partFile->close();
    startVerification();
}

void CuratedResourceManager::startVerification()
{
    const qint64 partSize = QFileInfo(m_activeDownload->partPath).size();
    if (partSize != *m_activeDownload->resource.expectedFileSize) {
        failActiveDownload(QObject::tr("Could not verify %1.").arg(m_activeDownload->resource.displayName));
        return;
    }

    m_activeDownload->verificationFile = std::make_unique<QFile>(m_activeDownload->partPath);
    if (!m_activeDownload->verificationFile->open(QIODevice::ReadOnly)) {
        failActiveDownload(QObject::tr("Could not verify %1.").arg(m_activeDownload->resource.displayName));
        return;
    }

    emitProgress(m_activeDownload->resource,
                 CuratedResourceState::Verifying,
                 m_activeDownload->bytesDownloaded,
                 m_activeDownload->totalBytes);
    QTimer::singleShot(0, this, &CuratedResourceManager::verifyNextChunk);
}

void CuratedResourceManager::verifyNextChunk()
{
    if (m_activeDownload == nullptr || m_activeDownload->verificationFile == nullptr) {
        return;
    }

    const QByteArray data = m_activeDownload->verificationFile->read(VerificationChunkSize);
    if (!data.isEmpty()) {
        m_activeDownload->sha256.addData(data);
        QTimer::singleShot(0, this, &CuratedResourceManager::verifyNextChunk);
        return;
    }

    if (m_activeDownload->verificationFile->error() != QFile::NoError) {
        failActiveDownload(QObject::tr("Could not verify %1.").arg(m_activeDownload->resource.displayName));
        return;
    }

    const QString actualHash = QString::fromLatin1(m_activeDownload->sha256.result().toHex());
    if (actualHash.compare(*m_activeDownload->resource.expectedSha256, Qt::CaseInsensitive) != 0) {
        failActiveDownload(QObject::tr("Could not verify %1.").arg(m_activeDownload->resource.displayName));
        return;
    }

    m_activeDownload->verificationFile->close();
    completeActiveDownload();
}

void CuratedResourceManager::completeActiveDownload()
{
    const QByteArray partPath = QFile::encodeName(m_activeDownload->partPath);
    const QByteArray finalPath = QFile::encodeName(m_activeDownload->finalPath);
    if (std::rename(partPath.constData(), finalPath.constData()) != 0) {
        failActiveDownload(QObject::tr("Could not install %1.").arg(m_activeDownload->resource.displayName));
        return;
    }

    const CuratedResourceDescriptor resource = m_activeDownload->resource;
    const qint64 fileSize = QFileInfo(m_activeDownload->finalPath).size();
    if (!writeInstallationMetadata(resource, fileSize)) {
        failActiveDownload(QObject::tr("Could not record installation of %1.").arg(resource.displayName));
        return;
    }

    m_activeDownload.reset();
    emitProgress(resource, CuratedResourceState::Ready, fileSize, fileSize);
    emit downloadFinished(resource.id);
    startNextDownload();
}

void CuratedResourceManager::failActiveDownload(const QString &errorMessage)
{
    if (m_activeDownload == nullptr) {
        return;
    }

    const CuratedResourceDescriptor resource = m_activeDownload->resource;
    if (m_activeDownload->reply != nullptr) {
        QNetworkReply *reply = m_activeDownload->reply;
        m_activeDownload->reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    if (m_activeDownload->partFile != nullptr) {
        m_activeDownload->partFile->close();
    }
    if (m_activeDownload->verificationFile != nullptr) {
        m_activeDownload->verificationFile->close();
    }
    if (!m_activeDownload->partPath.isEmpty()) {
        QFile::remove(m_activeDownload->partPath);
    }

    m_activeDownload.reset();
    emitProgress(resource, CuratedResourceState::Failed);
    emit downloadFailed(resource.id, errorMessage);
    startNextDownload();
}
