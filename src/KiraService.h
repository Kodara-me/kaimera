#pragma once

#include <QHostAddress>
#include <QObject>
#include <QQueue>
#include <QTemporaryDir>

#include <memory>

class CuratedResourceManager;
class QWebSocket;
class QWebSocketServer;
class SdCliRunner;
struct GenerationProgress;
struct SdCliGenerationResult;
struct SdCliUpscaleResult;

struct KiraServiceConfiguration
{
    QHostAddress listenAddress = QHostAddress::LocalHost;
    quint16 port = 9384;
    QString token;
    qint64 maximumImageBytes = 64LL * 1024 * 1024;
};

class KiraService final : public QObject
{
    Q_OBJECT

public:
    explicit KiraService(KiraServiceConfiguration configuration, QObject *parent = nullptr);
    ~KiraService() override;
    bool start(QString *errorMessage = nullptr);

private:
    enum class Operation { Generate, Upscale };
    struct Submission;
    struct Job;

    void initializeResources();
    void updateResourceState();
    void acceptConnection();
    void disconnectClient(QWebSocket *socket);
    void processTextMessage(QWebSocket *socket, const QString &message);
    void processBinaryMessage(QWebSocket *socket, const QByteArray &data);
    void submit(QWebSocket *socket, const QJsonObject &message);
    void enqueue(std::shared_ptr<Job> job);
    void startNextJob();
    void cancelJobsFor(QWebSocket *socket);
    void broadcastQueuePositions();
    void handleGenerationFinished(const SdCliGenerationResult &result);
    void handleUpscaleFinished(const SdCliUpscaleResult &result);
    void sendCapabilities(QWebSocket *socket) const;
    void send(QWebSocket *socket, QJsonObject object) const;
    void sendError(QWebSocket *socket, const QString &code, const QString &message, const QString &jobId = {}) const;
    bool isReady() const;
    std::shared_ptr<Job> jobForId(const QString &id) const;

    KiraServiceConfiguration m_configuration;
    std::unique_ptr<QWebSocketServer> m_server;
    std::unique_ptr<CuratedResourceManager> m_resources;
    std::unique_ptr<SdCliRunner> m_runner;
    QHash<QWebSocket *, bool> m_authenticatedClients;
    QHash<QWebSocket *, std::shared_ptr<Submission>> m_pendingUploads;
    QQueue<std::shared_ptr<Job>> m_queue;
    std::shared_ptr<Job> m_activeJob;
    bool m_resourcesFailed = false;
};
