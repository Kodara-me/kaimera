#pragma once

#include "ImageOperationRequest.h"
#include "GenerationProgress.h"
#include "ServiceJobResult.h"

#include <QObject>
#include <QUrl>

class QWebSocket;

class ServiceClient final : public QObject
{
    Q_OBJECT

public:
    explicit ServiceClient(QObject *parent = nullptr);
    QUrl endpoint() const;
    QString token() const;
    void setConfiguration(const QUrl &endpoint, const QString &token, bool persist = true);
    void connectToService();
    bool isReady() const;
    bool canGenerate() const;
    bool canUpscale() const;
    bool startGeneration(const ImageGenerationRequest &request, const QString &outputPath);
    bool startUpscale(const ImageUpscaleRequest &request, const QString &outputPath);

signals:
    void serviceStateChanged(const QString &state, const QString &message);
    void capabilitiesChanged(bool generationAvailable, bool upscaleAvailable);
    void generationOutput(const QString &stream, const QString &text);
    void generationProgressChanged(const GenerationProgress &progress);
    void generationFinished(const SdCliGenerationResult &result);
    void upscaleFinished(const SdCliUpscaleResult &result);

private:
    enum class JobOperation { None, Generate, Upscale };
    bool submit(JobOperation operation, const QJsonObject &options, const QString &inputPath, const QString &outputPath);
    void authenticate();
    void handleTextMessage(const QString &message);
    void handleBinaryMessage(const QByteArray &data);
    void finish(bool succeeded, const QString &message = {});

    QWebSocket *m_socket;
    QUrl m_endpoint;
    QString m_token;
    bool m_authenticated = false;
    bool m_generationAvailable = false;
    bool m_upscaleAvailable = false;
    QString m_jobId;
    JobOperation m_jobOperation = JobOperation::None;
    QString m_inputPath;
    QString m_outputPath;
    QByteArray m_inputBytes;
    bool m_waitingForResult = false;
    QString m_connectionFailureState;
    QString m_connectionFailureMessage;
};
