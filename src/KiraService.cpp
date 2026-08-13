#include "KiraService.h"

#include "CuratedResourceManager.h"
#include "GenerationRequest.h"
#include "ServiceProtocol.h"
#include "SdCliRunner.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QStandardPaths>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketServer>

namespace
{
constexpr int MaximumQueuedJobs = 8;

bool validPositiveInteger(const QJsonObject &object, const QString &name, int minimum, int maximum)
{
    const int value = object.value(name).toInt(-1);
    return value >= minimum && value <= maximum;
}
}

struct KiraService::Submission
{
    QString id;
    Operation operation = Operation::Generate;
    QJsonObject options;
    QPointer<QWebSocket> client;
};

struct KiraService::Job
{
    QString id;
    Operation operation = Operation::Generate;
    QJsonObject options;
    QPointer<QWebSocket> client;
    std::unique_ptr<QTemporaryDir> directory = std::make_unique<QTemporaryDir>();
    QString inputPath;
    QString outputPath;
};

KiraService::KiraService(KiraServiceConfiguration configuration, QObject *parent)
    : QObject(parent)
    , m_configuration(std::move(configuration))
    , m_resources(std::make_unique<CuratedResourceManager>())
{
    connect(m_resources.get(), &CuratedResourceManager::downloadFinished, this, [this](CuratedResourceId) { updateResourceState(); });
    connect(m_resources.get(), &CuratedResourceManager::downloadFailed, this, [this](CuratedResourceId, const QString &errorMessage) {
        qWarning().noquote() << QStringLiteral("[Kira Service] Resource preparation failed: %1").arg(errorMessage);
        m_resourcesFailed = true;
        updateResourceState();
    });
}

KiraService::~KiraService() = default;

bool KiraService::start(QString *errorMessage)
{
    if (!m_configuration.listenAddress.isLoopback() && m_configuration.token.isEmpty()) {
        if (errorMessage) *errorMessage = tr("A token is required when listening on the LAN");
        return false;
    }
    m_server = std::make_unique<QWebSocketServer>(QStringLiteral("Kira Service"), QWebSocketServer::NonSecureMode, this);
    if (!m_server->listen(m_configuration.listenAddress, m_configuration.port)) {
        if (errorMessage) *errorMessage = m_server->errorString();
        return false;
    }
    connect(m_server.get(), &QWebSocketServer::newConnection, this, &KiraService::acceptConnection);
    initializeResources();
    return true;
}

void KiraService::initializeResources()
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("sd-cli"));
    if (executable.isEmpty()) {
        qCritical().noquote() << QStringLiteral("[Kira Service] sd-cli was not found on the service PATH.");
        m_resourcesFailed = true;
        return;
    }
    m_runner = std::make_unique<SdCliRunner>(executable);
    connect(m_runner.get(), &SdCliRunner::generationProgressChanged, this, [this](const GenerationProgress &progress) {
        if (m_activeJob) send(m_activeJob->client, {{QStringLiteral("type"), QStringLiteral("progress")}, {QStringLiteral("jobId"), m_activeJob->id}, {QStringLiteral("progress"), ServiceProtocol::progressToJson(progress)}});
    });
    connect(m_runner.get(), &SdCliRunner::generationOutput, this, [this](const QString &stream, const QString &text) {
        if (m_activeJob) send(m_activeJob->client, {{QStringLiteral("type"), QStringLiteral("output")}, {QStringLiteral("jobId"), m_activeJob->id}, {QStringLiteral("stream"), stream}, {QStringLiteral("text"), text}});
    });
    connect(m_runner.get(), &SdCliRunner::generationFinished, this, &KiraService::handleGenerationFinished);
    connect(m_runner.get(), &SdCliRunner::upscaleFinished, this, &KiraService::handleUpscaleFinished);
    m_resources->downloadMissing(m_resources->requiredAtStartupResourceIds());
    updateResourceState();
}

bool KiraService::isReady() const
{
    if (m_resourcesFailed || !m_runner) return false;
    for (const CuratedResourceId id : m_resources->requiredAtStartupResourceIds()) {
        if (!m_resources->isInstalled(id)) return false;
    }
    return true;
}

void KiraService::updateResourceState()
{
    for (auto it = m_authenticatedClients.cbegin(); it != m_authenticatedClients.cend(); ++it) sendCapabilities(it.key());
}

void KiraService::acceptConnection()
{
    QWebSocket *socket = m_server->nextPendingConnection();
    socket->setMaxAllowedIncomingMessageSize(m_configuration.maximumImageBytes + 4096);
    connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &message) { processTextMessage(socket, message); });
    connect(socket, &QWebSocket::binaryMessageReceived, this, [this, socket](const QByteArray &data) { processBinaryMessage(socket, data); });
    connect(socket, &QWebSocket::disconnected, this, [this, socket] { disconnectClient(socket); });
}

void KiraService::disconnectClient(QWebSocket *socket)
{
    cancelJobsFor(socket);
    m_authenticatedClients.remove(socket);
    m_pendingUploads.remove(socket);
    socket->deleteLater();
}

void KiraService::processTextMessage(QWebSocket *socket, const QString &message)
{
    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8());
    if (!document.isObject()) { sendError(socket, QStringLiteral("invalid-message"), tr("Expected a JSON object")); return; }
    const QJsonObject object = document.object();
    const QString type = object.value(QStringLiteral("type")).toString();
    if (!m_authenticatedClients.contains(socket)) {
        if (type != QStringLiteral("authenticate") || object.value(QStringLiteral("version")).toInt() != ServiceProtocol::Version
            || (!m_configuration.token.isEmpty() && object.value(QStringLiteral("token")).toString() != m_configuration.token)) {
            qWarning().noquote() << QStringLiteral("[Kira Service] Rejected authentication from %1.").arg(socket->peerAddress().toString());
            sendError(socket, QStringLiteral("authentication-failed"), tr("Authentication failed")); socket->close(); return;
        }
        m_authenticatedClients.insert(socket, true);
        qInfo().noquote() << QStringLiteral("[Kira Service] Authenticated client from %1.").arg(socket->peerAddress().toString());
        send(socket, {{QStringLiteral("type"), QStringLiteral("authenticated")}});
        sendCapabilities(socket);
        return;
    }
    if (type == QStringLiteral("submit")) submit(socket, object);
    else if (type == QStringLiteral("capabilities")) sendCapabilities(socket);
    else sendError(socket, QStringLiteral("unsupported-message"), tr("Unsupported message"));
}

void KiraService::submit(QWebSocket *socket, const QJsonObject &message)
{
    if (!isReady()) { sendError(socket, QStringLiteral("backend-not-ready"), tr("The service is preparing its resources")); return; }
    if (m_queue.size() >= MaximumQueuedJobs) { sendError(socket, QStringLiteral("queue-full"), tr("The service queue is full")); return; }
    const QString operationName = message.value(QStringLiteral("operation")).toString();
    const bool generate = operationName == QStringLiteral("image.generate");
    const bool upscale = operationName == QStringLiteral("image.upscale");
    const QJsonObject options = message.value(QStringLiteral("options")).toObject();
    const QString jobId = message.value(QStringLiteral("jobId")).toString();
    bool duplicateId = jobForId(jobId) != nullptr;
    for (const auto &pending : m_pendingUploads) {
        duplicateId = duplicateId || pending->id == jobId;
    }
    if ((!generate && !upscale) || jobId.isEmpty() || duplicateId) { sendError(socket, QStringLiteral("invalid-request"), tr("Invalid job request")); return; }
    if (generate && (options.value(QStringLiteral("prompt")).toString().trimmed().isEmpty()
        || !validPositiveInteger(options, QStringLiteral("width"), 64, 4096)
        || !validPositiveInteger(options, QStringLiteral("height"), 64, 4096)
        || !validPositiveInteger(options, QStringLiteral("steps"), 1, 200))) { sendError(socket, QStringLiteral("invalid-request"), tr("Invalid generation options"), jobId); return; }
    if (upscale && (options.value(QStringLiteral("scale")).toInt() != 2 && options.value(QStringLiteral("scale")).toInt() != 4)) { sendError(socket, QStringLiteral("invalid-request"), tr("Unsupported upscale scale"), jobId); return; }
    const bool needsInput = upscale || options.value(QStringLiteral("hasInput")).toBool();
    auto submission = std::make_shared<Submission>();
    submission->id = jobId;
    submission->operation = generate ? Operation::Generate : Operation::Upscale;
    submission->options = options;
    submission->client = socket;
    if (needsInput) {
        m_pendingUploads.insert(socket, submission);
        send(socket, {{QStringLiteral("type"), QStringLiteral("upload-required")}, {QStringLiteral("jobId"), jobId}});
        return;
    }
    auto job = std::make_shared<Job>();
    job->id = jobId; job->operation = submission->operation; job->options = options; job->client = socket;
    enqueue(job);
}

void KiraService::processBinaryMessage(QWebSocket *socket, const QByteArray &data)
{
    const auto submission = m_pendingUploads.take(socket);
    if (!submission) { sendError(socket, QStringLiteral("unexpected-binary"), tr("No image upload was requested")); return; }
    if (data.isEmpty() || data.size() > m_configuration.maximumImageBytes) { sendError(socket, QStringLiteral("invalid-image"), tr("Image upload is too large"), submission->id); return; }
    auto job = std::make_shared<Job>();
    job->id = submission->id; job->operation = submission->operation; job->options = submission->options; job->client = socket;
    if (!job->directory->isValid()) { sendError(socket, QStringLiteral("storage-error"), tr("Could not create job storage"), job->id); return; }
    job->inputPath = job->directory->filePath(QStringLiteral("input.png"));
    QFile file(job->inputPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) { sendError(socket, QStringLiteral("storage-error"), tr("Could not save image upload"), job->id); return; }
    file.close();
    enqueue(job);
}

void KiraService::enqueue(std::shared_ptr<Job> job)
{
    m_queue.enqueue(job);
    send(job->client, {{QStringLiteral("type"), QStringLiteral("queued")}, {QStringLiteral("jobId"), job->id}});
    broadcastQueuePositions();
    startNextJob();
}

void KiraService::startNextJob()
{
    if (m_activeJob || m_queue.isEmpty() || !m_runner) return;
    m_activeJob = m_queue.dequeue();
    if (m_activeJob->client.isNull()) { m_activeJob.reset(); startNextJob(); return; }
    m_activeJob->outputPath = m_activeJob->directory->filePath(QStringLiteral("result.png"));
    send(m_activeJob->client, {{QStringLiteral("type"), QStringLiteral("running")}, {QStringLiteral("jobId"), m_activeJob->id}});
    if (m_activeJob->operation == Operation::Generate) {
        GenerationRequest request;
        const QJsonObject &o = m_activeJob->options;
        request.prompt = o.value(QStringLiteral("prompt")).toString(); request.negativePrompt = o.value(QStringLiteral("negativePrompt")).toString();
        request.sampler = o.value(QStringLiteral("sampler")).toString(); request.scheduler = o.value(QStringLiteral("scheduler")).toString();
        request.width = o.value(QStringLiteral("width")).toInt(); request.height = o.value(QStringLiteral("height")).toInt(); request.steps = o.value(QStringLiteral("steps")).toInt();
        request.cfgScale = o.value(QStringLiteral("cfgScale")).toDouble(); request.seed = o.value(QStringLiteral("seed")).toInt();
        request.baseImageStrength = o.value(QStringLiteral("baseImageStrength")).toDouble(0.55); request.outputPath = m_activeJob->outputPath;
        request.modelPath = m_resources->path(CuratedResourceId::ImageGenerationDefault);
        if (o.value(QStringLiteral("hasInput")).toBool()) request.baseImagePath = m_activeJob->inputPath;
        if (!m_runner->runGeneration(request)) { sendError(m_activeJob->client, QStringLiteral("start-failed"), tr("Could not start generation"), m_activeJob->id); m_activeJob.reset(); startNextJob(); }
    } else {
        const int scale = m_activeJob->options.value(QStringLiteral("scale")).toInt();
        const QString upscaler = m_resources->path(scale == 4 ? CuratedResourceId::Upscaler4x : CuratedResourceId::Upscaler2x);
        const QStringList arguments = {QStringLiteral("--mode"), QStringLiteral("upscale"), QStringLiteral("--init-img"), m_activeJob->inputPath, QStringLiteral("--upscale-model"), upscaler, QStringLiteral("--upscale-repeats"), QStringLiteral("1"), QStringLiteral("--upscale-tile-size"), QStringLiteral("128"), QStringLiteral("--output"), m_activeJob->outputPath};
        if (!m_runner->runUpscale(arguments, m_activeJob->outputPath)) { sendError(m_activeJob->client, QStringLiteral("start-failed"), tr("Could not start upscaling"), m_activeJob->id); m_activeJob.reset(); startNextJob(); }
    }
    broadcastQueuePositions();
}

void KiraService::handleGenerationFinished(const SdCliGenerationResult &result)
{
    if (!m_activeJob) return;
    const auto job = m_activeJob;
    if (!result.succeeded || !QFileInfo(job->outputPath).isFile()) sendError(job->client, QStringLiteral("generation-failed"), tr("Image generation failed"), job->id);
    else { QFile output(job->outputPath); const bool opened = output.open(QIODevice::ReadOnly); const QByteArray bytes = opened ? output.readAll() : QByteArray(); if (bytes.isEmpty()) sendError(job->client, QStringLiteral("result-read-failed"), tr("Could not read generated image"), job->id); else { send(job->client, {{QStringLiteral("type"), QStringLiteral("result")}, {QStringLiteral("jobId"), job->id}, {QStringLiteral("operation"), QStringLiteral("image.generate")}, {QStringLiteral("mimeType"), QStringLiteral("image/png")}}); job->client->sendBinaryMessage(bytes); } }
    m_activeJob.reset(); broadcastQueuePositions(); startNextJob();
}

void KiraService::handleUpscaleFinished(const SdCliUpscaleResult &result)
{
    if (!m_activeJob) return;
    const auto job = m_activeJob;
    if (!result.succeeded || !QFileInfo(job->outputPath).isFile()) sendError(job->client, QStringLiteral("upscale-failed"), tr("Image upscaling failed"), job->id);
    else { QFile output(job->outputPath); const bool opened = output.open(QIODevice::ReadOnly); const QByteArray bytes = opened ? output.readAll() : QByteArray(); if (bytes.isEmpty()) sendError(job->client, QStringLiteral("result-read-failed"), tr("Could not read upscaled image"), job->id); else { send(job->client, {{QStringLiteral("type"), QStringLiteral("result")}, {QStringLiteral("jobId"), job->id}, {QStringLiteral("operation"), QStringLiteral("image.upscale")}, {QStringLiteral("mimeType"), QStringLiteral("image/png")}}); job->client->sendBinaryMessage(bytes); } }
    m_activeJob.reset(); broadcastQueuePositions(); startNextJob();
}

void KiraService::cancelJobsFor(QWebSocket *socket)
{
    QQueue<std::shared_ptr<Job>> kept;
    while (!m_queue.isEmpty()) { const auto job = m_queue.dequeue(); if (job->client != socket) kept.enqueue(job); }
    m_queue = std::move(kept);
    if (m_activeJob && m_activeJob->client == socket && m_runner) m_runner->cancel();
    broadcastQueuePositions();
}

void KiraService::broadcastQueuePositions()
{
    for (int i = 0; i < m_queue.size(); ++i) send(m_queue.at(i)->client, {{QStringLiteral("type"), QStringLiteral("queue-position")}, {QStringLiteral("jobId"), m_queue.at(i)->id}, {QStringLiteral("position"), i + 1}});
}

void KiraService::sendCapabilities(QWebSocket *socket) const
{
    const QString state = isReady() ? QStringLiteral("ready") : (m_resourcesFailed ? QStringLiteral("unavailable") : QStringLiteral("preparing"));
    QJsonArray operations;
    if (isReady()) { operations.append(QStringLiteral("image.generate")); operations.append(QStringLiteral("image.upscale")); }
    send(socket, {{QStringLiteral("type"), QStringLiteral("capabilities")}, {QStringLiteral("state"), state}, {QStringLiteral("operations"), operations}});
}

void KiraService::send(QWebSocket *socket, QJsonObject object) const
{
    if (socket) socket->sendTextMessage(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

void KiraService::sendError(QWebSocket *socket, const QString &code, const QString &message, const QString &jobId) const
{
    QJsonObject event{{QStringLiteral("type"), QStringLiteral("error")}, {QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
    if (!jobId.isEmpty()) event.insert(QStringLiteral("jobId"), jobId);
    send(socket, event);
}

std::shared_ptr<KiraService::Job> KiraService::jobForId(const QString &id) const
{
    if (m_activeJob && m_activeJob->id == id) return m_activeJob;
    for (const auto &job : m_queue) if (job->id == id) return job;
    return {};
}
