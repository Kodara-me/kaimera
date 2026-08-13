#include "ServiceClient.h"

#include "ServiceProtocol.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QUuid>
#include <QWebSocket>

namespace
{
const QString EndpointKey = QStringLiteral("Service/Endpoint");
const QString TokenKey = QStringLiteral("Service/Token");
}

ServiceClient::ServiceClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QWebSocket(QStringLiteral("Kira Canvas"), QWebSocketProtocol::VersionLatest, this))
{
    QSettings settings;
    m_endpoint = QUrl(settings.value(EndpointKey, QStringLiteral("ws://127.0.0.1:9384")).toString());
    m_token = settings.value(TokenKey).toString();
    connect(m_socket, &QWebSocket::connected, this, &ServiceClient::authenticate);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &ServiceClient::handleTextMessage);
    connect(m_socket, &QWebSocket::binaryMessageReceived, this, &ServiceClient::handleBinaryMessage);
    connect(m_socket, &QWebSocket::disconnected, this, [this] {
        m_authenticated = false; m_generationAvailable = false; m_upscaleAvailable = false;
        emit capabilitiesChanged(false, false);
        if (m_jobOperation != JobOperation::None) finish(false, tr("Service connection was closed"));
        const QString state = m_connectionFailureState.isEmpty() ? QStringLiteral("offline") : m_connectionFailureState;
        const QString message = m_connectionFailureMessage.isEmpty() ? tr("Service connection was closed") : m_connectionFailureMessage;
        m_connectionFailureState.clear();
        m_connectionFailureMessage.clear();
        emit serviceStateChanged(state, message);
    });
    connect(m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_connectionFailureState.isEmpty()) {
            m_connectionFailureState = QStringLiteral("connection-error");
            m_connectionFailureMessage = tr("Could not connect to %1: %2").arg(m_endpoint.toString(), m_socket->errorString());
        }
    });
}

QUrl ServiceClient::endpoint() const { return m_endpoint; }
QString ServiceClient::token() const { return m_token; }

void ServiceClient::setConfiguration(const QUrl &endpoint, const QString &token, bool persist)
{
    m_endpoint = endpoint;
    m_token = token;
    if (persist) { QSettings settings; settings.setValue(EndpointKey, endpoint.toString()); settings.setValue(TokenKey, token); }
}

void ServiceClient::connectToService()
{
    if (!m_endpoint.isValid() || (m_endpoint.scheme() != QStringLiteral("ws") && m_endpoint.scheme() != QStringLiteral("wss"))) {
        emit serviceStateChanged(QStringLiteral("offline"), tr("Enter a valid ws:// or wss:// service address")); return;
    }
    m_authenticated = false;
    m_connectionFailureState.clear();
    m_connectionFailureMessage.clear();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
    emit serviceStateChanged(QStringLiteral("connecting"), tr("Connecting to service…"));
    m_socket->open(m_endpoint);
}

bool ServiceClient::isReady() const { return m_authenticated && (m_generationAvailable || m_upscaleAvailable); }
bool ServiceClient::canGenerate() const { return m_authenticated && m_generationAvailable; }
bool ServiceClient::canUpscale() const { return m_authenticated && m_upscaleAvailable; }

void ServiceClient::authenticate()
{
    m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument({{QStringLiteral("type"), QStringLiteral("authenticate")}, {QStringLiteral("version"), ServiceProtocol::Version}, {QStringLiteral("token"), m_token}}).toJson(QJsonDocument::Compact)));
}

bool ServiceClient::startGeneration(const ImageGenerationRequest &request, const QString &outputPath)
{
    QJsonObject options{{QStringLiteral("prompt"), request.prompt}, {QStringLiteral("negativePrompt"), request.negativePrompt}, {QStringLiteral("sampler"), request.sampler}, {QStringLiteral("scheduler"), request.scheduler}, {QStringLiteral("width"), request.width}, {QStringLiteral("height"), request.height}, {QStringLiteral("steps"), request.steps}, {QStringLiteral("cfgScale"), request.cfgScale}, {QStringLiteral("seed"), request.seed}, {QStringLiteral("hasInput"), !request.baseImagePath.isEmpty()}, {QStringLiteral("baseImageStrength"), request.baseImageStrength}};
    return submit(JobOperation::Generate, options, request.baseImagePath, outputPath);
}

bool ServiceClient::startUpscale(const ImageUpscaleRequest &request, const QString &outputPath)
{
    return submit(JobOperation::Upscale, {{QStringLiteral("scale"), request.scale}, {QStringLiteral("hasInput"), true}}, request.sourceImagePath, outputPath);
}

bool ServiceClient::submit(JobOperation operation, const QJsonObject &options, const QString &inputPath, const QString &outputPath)
{
    if (m_jobOperation != JobOperation::None || !(operation == JobOperation::Generate ? canGenerate() : canUpscale())) return false;
    if (!inputPath.isEmpty()) { QFile input(inputPath); if (!input.open(QIODevice::ReadOnly)) { emit serviceStateChanged(QStringLiteral("error"), tr("Could not read source image")); return false; } m_inputBytes = input.readAll(); }
    m_jobId = QUuid::createUuid().toString(QUuid::WithoutBraces); m_jobOperation = operation; m_inputPath = inputPath; m_outputPath = outputPath;
    const QString operationName = operation == JobOperation::Generate ? QStringLiteral("image.generate") : QStringLiteral("image.upscale");
    m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument({{QStringLiteral("type"), QStringLiteral("submit")}, {QStringLiteral("jobId"), m_jobId}, {QStringLiteral("operation"), operationName}, {QStringLiteral("options"), options}}).toJson(QJsonDocument::Compact)));
    return true;
}

void ServiceClient::handleTextMessage(const QString &message)
{
    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8()); if (!document.isObject()) return;
    const QJsonObject object = document.object(); const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("authenticated")) { m_authenticated = true; emit serviceStateChanged(QStringLiteral("connected"), tr("Connected to service")); return; }
    if (type == QStringLiteral("capabilities")) {
        const QString state = object.value(QStringLiteral("state")).toString(); const QJsonArray operations = object.value(QStringLiteral("operations")).toArray();
        m_generationAvailable = operations.contains(QStringLiteral("image.generate")); m_upscaleAvailable = operations.contains(QStringLiteral("image.upscale"));
        const QString statusMessage = state == QStringLiteral("ready")
                                          ? tr("Service is ready")
                                          : (state == QStringLiteral("preparing")
                                                 ? tr("Connected; service is preparing resources")
                                                 : tr("Connected, but the service backend is unavailable. Check the service journal for sd-cli and resource errors."));
        emit capabilitiesChanged(canGenerate(), canUpscale());
        emit serviceStateChanged(state, statusMessage);
        return;
    }
    if (type == QStringLiteral("error")) {
        const QString errorCode = object.value(QStringLiteral("code")).toString();
        if (!m_authenticated && errorCode == QStringLiteral("authentication-failed")) {
            m_connectionFailureState = QStringLiteral("authentication-failed");
            m_connectionFailureMessage = tr("Service rejected the shared token. Check Service Preferences and save the exact token again.");
            m_socket->close();
            return;
        }
        if (object.value(QStringLiteral("jobId")).toString() == m_jobId) {
            finish(false, object.value(QStringLiteral("message")).toString());
        }
        return;
    }
    if (object.value(QStringLiteral("jobId")).toString() != m_jobId) return;
    if (type == QStringLiteral("upload-required")) { m_socket->sendBinaryMessage(m_inputBytes); return; }
    if (type == QStringLiteral("progress")) { emit generationProgressChanged(ServiceProtocol::progressFromJson(object.value(QStringLiteral("progress")).toObject())); return; }
    if (type == QStringLiteral("output")) { emit generationOutput(object.value(QStringLiteral("stream")).toString(), object.value(QStringLiteral("text")).toString()); return; }
    if (type == QStringLiteral("queued")) { emit serviceStateChanged(QStringLiteral("queued"), tr("Job queued")); return; }
    if (type == QStringLiteral("queue-position")) { emit serviceStateChanged(QStringLiteral("queued"), tr("Job queued (%1 ahead)").arg(object.value(QStringLiteral("position")).toInt() - 1)); return; }
    if (type == QStringLiteral("running")) { emit serviceStateChanged(QStringLiteral("running"), tr("Processing image…")); return; }
    if (type == QStringLiteral("result")) { m_waitingForResult = true; return; }
}

void ServiceClient::handleBinaryMessage(const QByteArray &data)
{
    if (!m_waitingForResult || m_jobOperation == JobOperation::None) return;
    QSaveFile output(m_outputPath);
    const bool saved = output.open(QIODevice::WriteOnly) && output.write(data) == data.size() && output.commit();
    finish(saved, saved ? QString() : tr("Could not save service result"));
}

void ServiceClient::finish(bool succeeded, const QString &message)
{
    const JobOperation operation = m_jobOperation; const QString outputPath = m_outputPath;
    m_jobOperation = JobOperation::None; m_jobId.clear(); m_inputPath.clear(); m_outputPath.clear(); m_inputBytes.clear(); m_waitingForResult = false;
    if (operation == JobOperation::Generate) emit generationFinished({succeeded, succeeded ? 0 : -1, {}, {}, message, outputPath});
    else if (operation == JobOperation::Upscale) emit upscaleFinished({succeeded, succeeded ? 0 : -1, outputPath});
}
