#include "CuratedResourceManager.h"
#include "GeneratePanel.h"
#include "ImageLibraryWidget.h"
#include "ImageViewer.h"
#include "MainWindow.h"
#include "ReferenceSlotWidget.h"
#include "SdCliRunner.h"
#include "SdCliProgressParser.h"
#include "ServiceClient.h"
#include "ServiceProtocol.h"
#include "StartupPreparationWidget.h"
#include "UpscaleDialog.h"

#include <QCryptographicHash>
#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QKeySequence>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QSettings>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QPushButton>
#include <QStatusBar>
#include <QToolButton>
#include <QTabWidget>
#include <QToolBar>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QtTest>

#include <algorithm>

namespace
{
class TestHttpServer final : public QObject
{
public:
    explicit TestHttpServer(const QByteArray &payload, QObject *parent = nullptr)
        : QObject(parent)
        , m_payload(payload)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *socket = m_server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                if (!socket->canReadLine()) {
                    return;
                }

                socket->readAll();
                ++m_requestCount;
                if (m_failFirstRequest && m_requestCount == 1) {
                    socket->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    return;
                }

                const QByteArray responseHeader = "HTTP/1.1 200 OK\r\nContent-Length: "
                                                  + QByteArray::number(m_payload.size())
                                                  + "\r\nConnection: close\r\n\r\n";
                socket->write(responseHeader);
                if (m_interruptFirstRequest && m_requestCount == 1) {
                    socket->write(m_payload.left(m_payload.size() / 2));
                } else {
                    socket->write(m_payload);
                }
                socket->disconnectFromHost();
            });
        });
    }

    bool start()
    {
        return m_server.listen(QHostAddress(QStringLiteral("127.0.0.1")));
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/resource").arg(m_server.serverPort()));
    }

    void interruptFirstRequest()
    {
        m_interruptFirstRequest = true;
    }

    void failFirstRequest()
    {
        m_failFirstRequest = true;
    }

    int requestCount() const
    {
        return m_requestCount;
    }

private:
    QTcpServer m_server;
    QByteArray m_payload;
    bool m_interruptFirstRequest = false;
    bool m_failFirstRequest = false;
    int m_requestCount = 0;
};

CuratedResourceDescriptor testDescriptor(const QUrl &url,
                                         const QByteArray &payload,
                                         const QString &hash = {})
{
    return {CuratedResourceId::ImageGenerationDefault,
            QStringLiteral("Image generation model"),
            CuratedResourceAcquisitionPolicy::RequiredAtStartup,
            QStringLiteral("test-v1"),
            QStringLiteral("generation"),
            QStringLiteral("test-resource.bin"),
            url,
            hash.isEmpty() ? QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())
                           : hash,
            payload.size(),
            std::nullopt,
            std::nullopt,
            std::nullopt};
}

bool includesState(const QSignalSpy &spy, CuratedResourceState state)
{
    for (const QList<QVariant> &arguments : spy) {
        if (arguments.at(0).value<CuratedResourceProgress>().state == state) {
            return true;
        }
    }
    return false;
}

int visibleLibraryItemCount(const QListWidget &view)
{
    int count = 0;
    for (int index = 0; index < view.count(); ++index) {
        if (!view.item(index)->isHidden()) {
            ++count;
        }
    }
    return count;
}

class SettingsValueRestorer final
{
public:
    SettingsValueRestorer(QSettings &settings, const QString &key)
        : m_settings(settings)
        , m_key(key)
        , m_hadValue(settings.contains(key))
        , m_value(settings.value(key))
    {
    }

    ~SettingsValueRestorer()
    {
        if (m_hadValue) {
            m_settings.setValue(m_key, m_value);
        } else {
            m_settings.remove(m_key);
        }
        m_settings.sync();
    }

private:
    QSettings &m_settings;
    QString m_key;
    bool m_hadValue;
    QVariant m_value;
};
}

class CuratedResourceManagerTest final : public QObject
{
    Q_OBJECT

private slots:
    void downloadsVerifiesAndUsesMetadata();
    void replacesInterruptedPartFile();
    void rejectsInvalidChecksum();
    void retriesAfterInterruptedDownload();
    void retriesAfterHttpFailure();
    void reacquiresAfterResourceOrMetadataChanges();
    void preparesMissingResourcesAtStartup();
    void skipsPreparationWhenResourcesAreReady();
    void retriesPreparationAfterFailure();
    void createsBuiltInGenerationRequestWithoutModelSelection();
    void wiresBaseImageAndStrength();
    void capturesGenerationProcessResult();
    void parsesGenerationProgressFromSdCliOutput();
    void serializesServiceProgress();
    void reportsServiceAuthenticationFailure();
    void displaysPreviewAtNativeSize();
    void capturesUpscaleProcessResult();
    void displaysUpscaleDialogResolution();
    void enablesUpscaleForActivePreview();
    void providesSharedMainWindowActions();
    void filtersLibraryAssets();
    void switchesLibraryViewModesAndRequestsTrash();
    void displaysPreviewOnlyForAssignedReference();
#if defined(Q_OS_MACOS)
    void usesDirectQtMacControls();
    void usesFlatMacSideRegions();
    void usesSubtleMacSplitters();
    void usesThreeRoundedMacWorkspacePanels();
    void usesTransientMacStatusMessages();
#endif
};

void CuratedResourceManagerTest::downloadsVerifiesAndUsesMetadata()
{
    const QByteArray payload(4096, 'a');
    TestHttpServer server(payload);
    QVERIFY(server.start());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    CuratedResourceManager manager({testDescriptor(server.url(), payload)}, directory.path());
    QSignalSpy progressSpy(&manager, &CuratedResourceManager::progressChanged);
    QSignalSpy finishedSpy(&manager, &CuratedResourceManager::downloadFinished);
    QSignalSpy failedSpy(&manager, &CuratedResourceManager::downloadFailed);

    manager.downloadMissing({CuratedResourceId::ImageGenerationDefault});
    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(manager.exists(CuratedResourceId::ImageGenerationDefault));
    QVERIFY(manager.isInstalled(CuratedResourceId::ImageGenerationDefault));
    QVERIFY(includesState(progressSpy, CuratedResourceState::Downloading));
    QVERIFY(includesState(progressSpy, CuratedResourceState::Verifying));
    QVERIFY(includesState(progressSpy, CuratedResourceState::Ready));

    QFile metadataFile(directory.filePath(QStringLiteral("metadata/image-generation-default.json")));
    QVERIFY(metadataFile.open(QIODevice::ReadOnly));
    const QJsonObject metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
    QVERIFY(metadata.value(QStringLiteral("verified")).toBool());
    QCOMPARE(metadata.value(QStringLiteral("fileSize")).toVariant().toLongLong(), qint64(payload.size()));

    QFile finalFile(manager.path(CuratedResourceId::ImageGenerationDefault));
    QVERIFY(finalFile.open(QIODevice::ReadWrite));
    QVERIFY(finalFile.seek(0));
    QCOMPARE(finalFile.write("b", 1), qint64(1));
    finalFile.close();
    QVERIFY(manager.isInstalled(CuratedResourceId::ImageGenerationDefault));
}

void CuratedResourceManagerTest::replacesInterruptedPartFile()
{
    const QByteArray payload(2048, 'p');
    TestHttpServer server(payload);
    QVERIFY(server.start());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    CuratedResourceManager manager({testDescriptor(server.url(), payload)}, directory.path());
    const QString partPath = manager.path(CuratedResourceId::ImageGenerationDefault) + QStringLiteral(".part");
    QVERIFY(QDir().mkpath(QFileInfo(partPath).dir().absolutePath()));
    QFile partFile(partPath);
    QVERIFY(partFile.open(QIODevice::WriteOnly));
    QCOMPARE(partFile.write("interrupted", 11), qint64(11));
    partFile.close();
    QVERIFY(!manager.isInstalled(CuratedResourceId::ImageGenerationDefault));

    QSignalSpy finishedSpy(&manager, &CuratedResourceManager::downloadFinished);
    manager.download(CuratedResourceId::ImageGenerationDefault);
    QTRY_COMPARE(finishedSpy.count(), 1);
    QVERIFY(manager.isInstalled(CuratedResourceId::ImageGenerationDefault));
    QVERIFY(!QFileInfo::exists(partPath));
}

void CuratedResourceManagerTest::rejectsInvalidChecksum()
{
    const QByteArray payload(1024, 'x');
    TestHttpServer server(payload);
    QVERIFY(server.start());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    CuratedResourceManager manager({testDescriptor(server.url(), payload, QString(64, QLatin1Char('0')))}, directory.path());
    QSignalSpy failedSpy(&manager, &CuratedResourceManager::downloadFailed);

    manager.download(CuratedResourceId::ImageGenerationDefault);
    QTRY_COMPARE(failedSpy.count(), 1);
    QVERIFY(!manager.exists(CuratedResourceId::ImageGenerationDefault));
    QVERIFY(!manager.isInstalled(CuratedResourceId::ImageGenerationDefault));
    QVERIFY(!QFileInfo::exists(manager.path(CuratedResourceId::ImageGenerationDefault) + QStringLiteral(".part")));
    QVERIFY(failedSpy.at(0).at(1).toString().contains(QStringLiteral("Image generation model")));
}

void CuratedResourceManagerTest::retriesAfterInterruptedDownload()
{
    const QByteArray payload(3072, 'r');
    TestHttpServer server(payload);
    server.interruptFirstRequest();
    QVERIFY(server.start());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    CuratedResourceManager manager({testDescriptor(server.url(), payload)}, directory.path());
    QSignalSpy finishedSpy(&manager, &CuratedResourceManager::downloadFinished);
    QSignalSpy failedSpy(&manager, &CuratedResourceManager::downloadFailed);

    manager.download(CuratedResourceId::ImageGenerationDefault);
    QTRY_COMPARE(failedSpy.count(), 1);
    QVERIFY(!manager.isInstalled(CuratedResourceId::ImageGenerationDefault));

    manager.download(CuratedResourceId::ImageGenerationDefault);
    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(server.requestCount(), 2);
    QVERIFY(manager.isInstalled(CuratedResourceId::ImageGenerationDefault));
}

void CuratedResourceManagerTest::retriesAfterHttpFailure()
{
    const QByteArray payload(1536, 'h');
    TestHttpServer server(payload);
    server.failFirstRequest();
    QVERIFY(server.start());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    CuratedResourceManager manager({testDescriptor(server.url(), payload)}, directory.path());
    QSignalSpy finishedSpy(&manager, &CuratedResourceManager::downloadFinished);
    QSignalSpy failedSpy(&manager, &CuratedResourceManager::downloadFailed);

    manager.download(CuratedResourceId::ImageGenerationDefault);
    QTRY_COMPARE(failedSpy.count(), 1);
    QVERIFY(!manager.isInstalled(CuratedResourceId::ImageGenerationDefault));

    manager.download(CuratedResourceId::ImageGenerationDefault);
    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(server.requestCount(), 2);
    QVERIFY(manager.isInstalled(CuratedResourceId::ImageGenerationDefault));
}

void CuratedResourceManagerTest::reacquiresAfterResourceOrMetadataChanges()
{
    const QByteArray payload(2048, 'm');
    TestHttpServer server(payload);
    QVERIFY(server.start());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    CuratedResourceDescriptor descriptor = testDescriptor(server.url(), payload);
    CuratedResourceManager manager({descriptor}, directory.path());
    QSignalSpy finishedSpy(&manager, &CuratedResourceManager::downloadFinished);

    manager.download(CuratedResourceId::ImageGenerationDefault);
    QTRY_COMPARE(finishedSpy.count(), 1);
    QVERIFY(manager.isInstalled(CuratedResourceId::ImageGenerationDefault));

    QVERIFY(QFile::remove(manager.path(CuratedResourceId::ImageGenerationDefault)));
    QVERIFY(!manager.isInstalled(CuratedResourceId::ImageGenerationDefault));
    manager.download(CuratedResourceId::ImageGenerationDefault);
    QTRY_COMPARE(finishedSpy.count(), 2);
    QVERIFY(manager.isInstalled(CuratedResourceId::ImageGenerationDefault));

    QFile metadataFile(directory.filePath(QStringLiteral("metadata/image-generation-default.json")));
    QVERIFY(metadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(metadataFile.write("not json"), qint64(8));
    metadataFile.close();
    QVERIFY(!manager.isInstalled(CuratedResourceId::ImageGenerationDefault));
    manager.download(CuratedResourceId::ImageGenerationDefault);
    QTRY_COMPARE(finishedSpy.count(), 3);
    QVERIFY(manager.isInstalled(CuratedResourceId::ImageGenerationDefault));

    *descriptor.version = QStringLiteral("test-v2");
    CuratedResourceManager updatedManager({descriptor}, directory.path());
    QSignalSpy updatedFinishedSpy(&updatedManager, &CuratedResourceManager::downloadFinished);
    QVERIFY(!updatedManager.isInstalled(CuratedResourceId::ImageGenerationDefault));
    updatedManager.download(CuratedResourceId::ImageGenerationDefault);
    QTRY_COMPARE(updatedFinishedSpy.count(), 1);
    QVERIFY(updatedManager.isInstalled(CuratedResourceId::ImageGenerationDefault));
    QCOMPARE(server.requestCount(), 4);
}

void CuratedResourceManagerTest::preparesMissingResourcesAtStartup()
{
    const QByteArray payload(1024, 's');
    TestHttpServer server(payload);
    QVERIFY(server.start());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    CuratedResourceManager manager({testDescriptor(server.url(), payload)}, directory.path());
    const QString partPath = manager.path(CuratedResourceId::ImageGenerationDefault) + QStringLiteral(".part");
    QVERIFY(QDir().mkpath(QFileInfo(partPath).dir().absolutePath()));
    QFile partFile(partPath);
    QVERIFY(partFile.open(QIODevice::WriteOnly));
    QCOMPARE(partFile.write("incomplete", 10), qint64(10));
    partFile.close();

    StartupPreparationWidget preparation(&manager);
    QSignalSpy preparedSpy(&preparation, &StartupPreparationWidget::preparationFinished);

    preparation.beginPreparation();
    QTRY_COMPARE(preparedSpy.count(), 1);
    QVERIFY(manager.isInstalled(CuratedResourceId::ImageGenerationDefault));
    QVERIFY(!QFileInfo::exists(partPath));
}

void CuratedResourceManagerTest::skipsPreparationWhenResourcesAreReady()
{
    const QByteArray payload(1024, 'z');
    TestHttpServer server(payload);
    QVERIFY(server.start());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    CuratedResourceManager manager({testDescriptor(server.url(), payload)}, directory.path());
    QSignalSpy downloadedSpy(&manager, &CuratedResourceManager::downloadFinished);
    manager.download(CuratedResourceId::ImageGenerationDefault);
    QTRY_COMPARE(downloadedSpy.count(), 1);
    QCOMPARE(server.requestCount(), 1);

    StartupPreparationWidget preparation(&manager);
    QSignalSpy preparedSpy(&preparation, &StartupPreparationWidget::preparationFinished);
    preparation.beginPreparation();
    QCOMPARE(preparedSpy.count(), 1);
    QCOMPARE(server.requestCount(), 1);
}

void CuratedResourceManagerTest::retriesPreparationAfterFailure()
{
    const QByteArray payload(1024, 'q');
    TestHttpServer server(payload);
    server.failFirstRequest();
    QVERIFY(server.start());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    CuratedResourceManager manager({testDescriptor(server.url(), payload)}, directory.path());
    StartupPreparationWidget preparation(&manager);
    QSignalSpy preparedSpy(&preparation, &StartupPreparationWidget::preparationFinished);
    QSignalSpy failedSpy(&manager, &CuratedResourceManager::downloadFailed);

    preparation.beginPreparation();
    QTRY_COMPARE(failedSpy.count(), 1);
    QPushButton *retryButton = preparation.findChild<QPushButton *>();
    QVERIFY(retryButton != nullptr);
    retryButton->click();

    QTRY_COMPARE(preparedSpy.count(), 1);
    QCOMPARE(server.requestCount(), 2);
    QVERIFY(manager.isInstalled(CuratedResourceId::ImageGenerationDefault));
}

void CuratedResourceManagerTest::createsBuiltInGenerationRequestWithoutModelSelection()
{
    QStatusBar statusBar;
    GeneratePanel panel(&statusBar);

    ImageGenerationRequest request;
    connect(&panel, &GeneratePanel::generationRequested, this, [&request](const ImageGenerationRequest &emittedRequest) {
        request = emittedRequest;
    });
    QPushButton *generateButton = panel.findChild<QPushButton *>();
    QVERIFY(generateButton != nullptr);
    const QList<QPlainTextEdit *> textEdits = panel.findChildren<QPlainTextEdit *>();
    QCOMPARE(textEdits.size(), 2);
    textEdits.at(0)->setPlainText(QStringLiteral("A sunset over a lake"));

    generateButton->click();
    QCOMPARE(request.prompt, QStringLiteral("A sunset over a lake"));
    QCOMPARE(request.sampler, QStringLiteral("dpm++2m"));
    QCOMPARE(request.scheduler, QStringLiteral("karras"));
    QCOMPARE(request.width, 1024);
    QCOMPARE(request.height, 1024);
    QCOMPARE(request.steps, 28);
    QCOMPARE(request.cfgScale, 7.0);
    QCOMPARE(request.seed, -1);
    QVERIFY(request.baseImagePath.isEmpty());
    QCOMPARE(panel.findChildren<QComboBox *>().size(), 2);
    QCOMPARE(panel.findChildren<ReferenceSlotWidget *>().size(), 1);
    const QList<QSlider *> sliders = panel.findChildren<QSlider *>();
    QCOMPARE(sliders.size(), 1);
    QWidget *strengthContainer = panel.findChild<QWidget *>(QStringLiteral("baseImageStrengthContainer"));
    QVERIFY(strengthContainer != nullptr);
    QVERIFY(strengthContainer->isHidden());
    QLabel *baseImageHelper = panel.findChild<QLabel *>(QStringLiteral("baseImageHelper"));
    QVERIFY(baseImageHelper != nullptr);
    QCOMPARE(baseImageHelper->text(), QStringLiteral("Start generation from an existing image."));
    QVERIFY(!baseImageHelper->isHidden());
    const QList<QLabel *> labels = panel.findChildren<QLabel *>();
    QVERIFY(std::none_of(labels.cbegin(), labels.cend(), [](const QLabel *label) {
        return label->text() == QStringLiteral("Model") || label->text() == QStringLiteral("Style Reference");
    }));
    QVERIFY(std::any_of(labels.cbegin(), labels.cend(), [](const QLabel *label) {
        return label->text() == QStringLiteral("Base image");
    }));
}

void CuratedResourceManagerTest::wiresBaseImageAndStrength()
{
    QStatusBar statusBar;
    GeneratePanel panel(&statusBar);
    const ImageAsset asset = {QStringLiteral("base"),
                              QStringLiteral("base.png"),
                              QStringLiteral("/tmp/base.png"),
                              QImage(512, 512, QImage::Format_ARGB32)};
    ImageGenerationRequest request;
    connect(&panel, &GeneratePanel::generationRequested, this, [&request](const ImageGenerationRequest &emittedRequest) {
        request = emittedRequest;
    });

    panel.setSelectedAsset(&asset);
    ReferenceSlotWidget *baseImageReference = panel.findChild<ReferenceSlotWidget *>();
    QVERIFY(baseImageReference != nullptr);
    QCOMPARE(baseImageReference->toolTip(), QStringLiteral("Uses this image as the starting point for generation."));
    const QList<QToolButton *> referenceButtons = baseImageReference->findChildren<QToolButton *>();
    const auto assignButton = std::find_if(referenceButtons.cbegin(), referenceButtons.cend(), [](const QToolButton *button) {
        return button->text() == QStringLiteral("Use Selected");
    });
    QVERIFY(assignButton != referenceButtons.cend());
    (*assignButton)->click();

    QSlider *strengthSlider = panel.findChild<QSlider *>();
    QVERIFY(strengthSlider != nullptr);
    QCOMPARE(strengthSlider->toolTip(),
             QStringLiteral("Controls how much the generated image may differ from the base image."));
    QWidget *strengthContainer = panel.findChild<QWidget *>(QStringLiteral("baseImageStrengthContainer"));
    QVERIFY(strengthContainer != nullptr);
    QVERIFY(!strengthContainer->isHidden());
    QLabel *baseImageHelper = panel.findChild<QLabel *>(QStringLiteral("baseImageHelper"));
    QVERIFY(baseImageHelper != nullptr);
    QVERIFY(baseImageHelper->isHidden());
    QLabel *strengthHelper = panel.findChild<QLabel *>(QStringLiteral("baseImageStrengthHelper"));
    QVERIFY(strengthHelper != nullptr);
    QCOMPARE(strengthHelper->text(), QStringLiteral("Higher values allow more change."));
    strengthSlider->setValue(42);

    const QList<QPlainTextEdit *> textEdits = panel.findChildren<QPlainTextEdit *>();
    textEdits.front()->setPlainText(QStringLiteral("A sunset over a lake"));
    QPushButton *generateButton = panel.findChild<QPushButton *>();
    QVERIFY(generateButton != nullptr);
    generateButton->click();

    QCOMPARE(request.baseImagePath, asset.filePath);
    QCOMPARE(request.baseImageStrength, 0.42);
}

void CuratedResourceManagerTest::capturesGenerationProcessResult()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString scriptPath = directory.filePath(QStringLiteral("sd-cli"));
    QFile script(scriptPath);
    QVERIFY(script.open(QIODevice::WriteOnly));
    QVERIFY(script.write("#!/bin/sh\n"
                         "output=\"\"\n"
                         "while [ \"$#\" -gt 0 ]; do\n"
                         "  if [ \"$1\" = \"--output\" ]; then output=\"$2\"; shift 2; else shift; fi\n"
                         "done\n"
                         "printf generated > \"$output\"\n"
                         "printf stdout-message\n"
                         "printf stderr-message >&2\n")
            > 0);
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    const QString outputPath = directory.filePath(QStringLiteral("generated.png"));
    SdCliRunner runner(scriptPath);
    std::optional<SdCliGenerationResult> result;
    connect(&runner, &SdCliRunner::generationFinished, this, [&result](const SdCliGenerationResult &finishedResult) {
        result = finishedResult;
    });

    GenerationRequest request;
    request.modelPath = QStringLiteral("/tmp/model");
    request.prompt = QStringLiteral("test prompt");
    request.negativePrompt = QStringLiteral("negative prompt");
    request.width = 1024;
    request.height = 768;
    request.steps = 28;
    request.cfgScale = 7.0;
    request.sampler = QStringLiteral("dpm++2m");
    request.scheduler = QStringLiteral("karras");
    request.seed = 12345;
    request.baseImagePath = QStringLiteral("/tmp/base.png");
    request.baseImageStrength = 0.55;
    request.outputPath = outputPath;
    const QStringList arguments = {QStringLiteral("--model"), request.modelPath,
                                   QStringLiteral("--prompt"), request.prompt,
                                   QStringLiteral("--negative-prompt"), request.negativePrompt,
                                   QStringLiteral("--width"), QStringLiteral("1024"),
                                   QStringLiteral("--height"), QStringLiteral("768"),
                                   QStringLiteral("--steps"), QStringLiteral("28"),
                                   QStringLiteral("--cfg-scale"), QStringLiteral("7.0"),
                                   QStringLiteral("--sampling-method"), request.sampler,
                                   QStringLiteral("--scheduler"), request.scheduler,
                                   QStringLiteral("--seed"), QStringLiteral("12345"),
                                   QStringLiteral("--output"), outputPath,
                                   QStringLiteral("--init-img"), request.baseImagePath,
                                   QStringLiteral("--strength"), QStringLiteral("0.55")};
    QSignalSpy outputSpy(&runner, &SdCliRunner::generationOutput);
    QSignalSpy progressSpy(&runner, &SdCliRunner::generationProgressChanged);
    QVERIFY(runner.runGeneration(request));
    QTRY_VERIFY(result.has_value());
    QVERIFY(result->succeeded);
    QCOMPARE(result->exitCode, 0);
    QCOMPARE(result->arguments, arguments);
    QCOMPARE(result->standardOutput, QStringLiteral("stdout-message"));
    QCOMPARE(result->standardError, QStringLiteral("stderr-message"));
    QCOMPARE(result->outputPath, outputPath);
    QVERIFY(QFileInfo(outputPath).isFile());
    QCOMPARE(runner.lastGenerationResult()->outputPath, outputPath);
    QCOMPARE(outputSpy.count(), 2);
    QCOMPARE(progressSpy.count(), 3);
    QCOMPARE(progressSpy.at(0).at(0).value<GenerationProgress>().percentage, 0);
    QCOMPARE(progressSpy.at(2).at(0).value<GenerationProgress>().percentage, 100);
}

void CuratedResourceManagerTest::parsesGenerationProgressFromSdCliOutput()
{
    SdCliProgressParser parser;
    const GenerationProgress preparing = parser.begin(28);
    QCOMPARE(preparing.percentage, 0);
    QCOMPARE(preparing.stage, GenerationProgressStage::Preparing);

    const auto starting = parser.starting();
    QVERIFY(starting.has_value());
    QCOMPARE(starting->percentage, 2);

    const auto loading = parser.processLine(QStringLiteral("loading model from '/tmp/model'"));
    QVERIFY(loading.has_value());
    QCOMPARE(loading->percentage, 5);
    QCOMPARE(loading->stage, GenerationProgressStage::LoadingModel);

    QVERIFY(!parser.processLine(QStringLiteral("|####| 13/178 - 4.39MB/s")).has_value());
    const auto initializing = parser.processLine(QStringLiteral("generate_image 1024x1024"));
    QVERIFY(initializing.has_value());
    QCOMPARE(initializing->percentage, 10);
    QVERIFY(!parser.processLine(QStringLiteral("|####| 374/374 - 3.12GB/s")).has_value());

    const auto sampling = parser.processLine(QStringLiteral("generating image: 1/1 - seed 424242"));
    QVERIFY(sampling.has_value());
    QCOMPARE(sampling->percentage, 15);
    QVERIFY(!parser.processLine(QStringLiteral("|######| 16/140 - 20.02MB/s")).has_value());

    const auto stepOne = parser.processLine(QStringLiteral("|=>| 1/28 - 1.68s/it"));
    QVERIFY(stepOne.has_value());
    QCOMPARE(stepOne->percentage, 18);
    QCOMPARE(*stepOne->currentStep, 1);
    QCOMPARE(*stepOne->totalSteps, 28);

    const auto stepFourteen = parser.processLine(QStringLiteral("|=================>| 14/28 - 1.55it/s"));
    QVERIFY(stepFourteen.has_value());
    QCOMPARE(stepFourteen->percentage, 53);
    const auto stepTwentyEight = parser.processLine(QStringLiteral("|==================| 28/28 - 1.55it/s"));
    QVERIFY(stepTwentyEight.has_value());
    QCOMPARE(stepTwentyEight->percentage, 90);

    const auto decoding = parser.processLine(QStringLiteral("decoding 1 latents"));
    QVERIFY(decoding.has_value());
    QCOMPARE(decoding->percentage, 94);
    QCOMPARE(decoding->stage, GenerationProgressStage::Decoding);
    QVERIFY(!parser.processLine(QStringLiteral("generate_image completed in 27.61s")).has_value());

    const auto saving = parser.processLine(QStringLiteral("save result image 0 to '/tmp/output.png' (success)"));
    QVERIFY(saving.has_value());
    QCOMPARE(saving->percentage, 99);
    const auto complete = parser.complete();
    QVERIFY(complete.has_value());
    QCOMPARE(complete->percentage, 100);
    QCOMPARE(complete->stage, GenerationProgressStage::Complete);

    SdCliProgressParser failedParser;
    failedParser.begin(28);
    const auto failed = failedParser.failed();
    QVERIFY(failed.has_value());
    QCOMPARE(failed->percentage, 0);
    QCOMPARE(failed->stage, GenerationProgressStage::Failed);
}

void CuratedResourceManagerTest::serializesServiceProgress()
{
    const GenerationProgress original = {GenerationProgressStage::Sampling,
                                         14,
                                         28,
                                         53,
                                         QStringLiteral("Sampling 14 / 28")};
    const QJsonObject encoded = ServiceProtocol::progressToJson(original);
    QCOMPARE(encoded.value(QStringLiteral("stage")).toString(), QStringLiteral("sampling"));
    QCOMPARE(encoded.value(QStringLiteral("currentStep")).toInt(), 14);
    const GenerationProgress decoded = ServiceProtocol::progressFromJson(encoded);
    QCOMPARE(decoded.stage, GenerationProgressStage::Sampling);
    QCOMPARE(decoded.currentStep, original.currentStep);
    QCOMPARE(decoded.totalSteps, original.totalSteps);
    QCOMPARE(decoded.percentage, original.percentage);
    QCOMPARE(decoded.statusText, original.statusText);
}

void CuratedResourceManagerTest::reportsServiceAuthenticationFailure()
{
    QWebSocketServer server(QStringLiteral("test-service"), QWebSocketServer::NonSecureMode);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    connect(&server, &QWebSocketServer::newConnection, this, [&server] {
        QWebSocket *socket = server.nextPendingConnection();
        connect(socket, &QWebSocket::textMessageReceived, socket, [socket](const QString &message) {
            const QJsonObject request = QJsonDocument::fromJson(message.toUtf8()).object();
            QCOMPARE(request.value(QStringLiteral("type")).toString(), QStringLiteral("authenticate"));
            QCOMPARE(request.value(QStringLiteral("token")).toString(), QStringLiteral("incorrect token"));
            socket->sendTextMessage(QStringLiteral("{\"type\":\"error\",\"code\":\"authentication-failed\",\"message\":\"Authentication failed\"}"));
            socket->close();
        });
    });

    ServiceClient client;
    client.setConfiguration(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())),
                            QStringLiteral("incorrect token"),
                            false);
    QSignalSpy stateSpy(&client, &ServiceClient::serviceStateChanged);
    client.connectToService();
    QTRY_VERIFY(std::any_of(stateSpy.cbegin(), stateSpy.cend(), [](const QList<QVariant> &arguments) {
        return arguments.at(0).toString() == QStringLiteral("authentication-failed")
               && arguments.at(1).toString().contains(QStringLiteral("shared token"));
    }));
}

void CuratedResourceManagerTest::displaysPreviewAtNativeSize()
{
    ImageViewer viewer;
    QCOMPARE(viewer.horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QCOMPARE(viewer.verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);

    viewer.resize(700, 600);
    viewer.setImage(QImage(1024, 1024, QImage::Format_ARGB32));
    QCOMPARE(viewer.zoomPercent(), 100);

    viewer.resize(1200, 900);
    QCOMPARE(viewer.zoomPercent(), 100);

    viewer.setImage(QImage(512, 512, QImage::Format_ARGB32));
    QCOMPARE(viewer.zoomPercent(), 100);

    viewer.fitToWindow();
    QCOMPARE(viewer.zoomPercent(), 100);
}

void CuratedResourceManagerTest::capturesUpscaleProcessResult()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString scriptPath = directory.filePath(QStringLiteral("sd-cli"));
    QFile script(scriptPath);
    QVERIFY(script.open(QIODevice::WriteOnly));
    QVERIFY(script.write("#!/bin/sh\n"
                         "output=\"\"\n"
                         "while [ \"$#\" -gt 0 ]; do\n"
                         "  if [ \"$1\" = \"--output\" ]; then output=\"$2\"; shift 2; else shift; fi\n"
                         "done\n"
                         "printf upscaled > \"$output\"\n")
            > 0);
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    const QString outputPath = directory.filePath(QStringLiteral("upscaled.png"));
    SdCliRunner runner(scriptPath);
    std::optional<SdCliUpscaleResult> result;
    connect(&runner, &SdCliRunner::upscaleFinished, this, [&result](const SdCliUpscaleResult &finishedResult) {
        result = finishedResult;
    });
    const QStringList arguments = {QStringLiteral("--mode"), QStringLiteral("upscale"),
                                   QStringLiteral("--output"), outputPath};
    QVERIFY(runner.runUpscale(arguments, outputPath));
    QTRY_VERIFY(result.has_value());
    QVERIFY(result->succeeded);
    QCOMPARE(result->exitCode, 0);
    QCOMPARE(result->outputPath, outputPath);
    QVERIFY(QFileInfo(outputPath).isFile());
}

void CuratedResourceManagerTest::displaysUpscaleDialogResolution()
{
    UpscaleDialog dialog({1024, 768});
    auto *scaleSelector = dialog.findChild<QComboBox *>(QStringLiteral("upscaleScaleSelector"));
    auto *baseResolution = dialog.findChild<QLabel *>(QStringLiteral("upscaleBaseResolutionLabel"));
    auto *finalResolution = dialog.findChild<QLabel *>(QStringLiteral("upscaleFinalResolutionLabel"));
    QVERIFY(scaleSelector != nullptr);
    QVERIFY(baseResolution != nullptr);
    QVERIFY(finalResolution != nullptr);
    QCOMPARE(dialog.scale(), 2);
    QCOMPARE(dialog.outputSize(), QSize(2048, 1536));
    QCOMPARE(baseResolution->text(), QStringLiteral("1024 × 768"));
    QCOMPARE(finalResolution->text(), QStringLiteral("2048 × 1536"));

    scaleSelector->setCurrentIndex(1);
    QCOMPARE(dialog.scale(), 4);
    QCOMPARE(dialog.outputSize(), QSize(4096, 3072));
    QCOMPARE(finalResolution->text(), QStringLiteral("4096 × 3072"));
}

#if defined(Q_OS_MACOS)
void CuratedResourceManagerTest::usesDirectQtMacControls()
{
    MainWindow window;
    window.show();
    QTest::qWait(1);
    QVERIFY(window.findChildren<QToolBar *>().isEmpty());
    QVERIFY(window.findChild<QWidget *>(QStringLiteral("macMaterialSurface")) == nullptr);

    auto *generateButton = window.findChild<QPushButton *>(QStringLiteral("generateButton"));
    QVERIFY(generateButton != nullptr);
    QVERIFY(generateButton->isVisible());
}

void CuratedResourceManagerTest::usesFlatMacSideRegions()
{
    const std::deque<ImageAsset> assets;
    ImageLibraryWidget library(assets);
    QWidget *libraryRoot = &library;
    QVERIFY(qobject_cast<QGroupBox *>(libraryRoot) == nullptr);

    GeneratePanel panel(nullptr);
    auto *scrollArea = panel.findChild<QScrollArea *>();
    QVERIFY(scrollArea != nullptr);
    QCOMPARE(scrollArea->frameShape(), QFrame::NoFrame);
    QVERIFY(panel.findChild<QLabel *>(QStringLiteral("generatePromptSection")) != nullptr);
    QVERIFY(panel.findChild<QLabel *>(QStringLiteral("generateModelSection")) != nullptr);
    QVERIFY(panel.findChild<QLabel *>(QStringLiteral("generateImageInputSection")) != nullptr);
}

void CuratedResourceManagerTest::usesSubtleMacSplitters()
{
    MainWindow window;
    auto *splitter = window.findChild<QSplitter *>();
    QVERIFY(splitter != nullptr);
    QCOMPARE(splitter->count(), 3);
    QCOMPARE(splitter->handleWidth(), 10);
}

void CuratedResourceManagerTest::usesThreeRoundedMacWorkspacePanels()
{
    MainWindow window;
    QCOMPARE(window.findChildren<QWidget *>(QStringLiteral("macWorkspacePanel")).size(), 3);
}

void CuratedResourceManagerTest::usesTransientMacStatusMessages()
{
    MainWindow window;
    window.show();
    QTest::qWait(1);
    QVERIFY(!window.statusBar()->isVisible());
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("transientStatusMessage")) != nullptr);
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("generateServiceStatus")) != nullptr);
}
#endif

void CuratedResourceManagerTest::enablesUpscaleForActivePreview()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = directory.filePath(QStringLiteral("source.png"));
    QImage image(320, 240, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(imagePath));

    QSettings settings;
    const QString assetPathsKey = QStringLiteral("Library/AssetPaths");
    const SettingsValueRestorer restoreAssetPaths(settings, assetPathsKey);
    settings.setValue(assetPathsKey, QStringList{imagePath});
    settings.sync();
    const QString canonicalImagePath = QFileInfo(imagePath).canonicalFilePath();

    MainWindow window;
    auto *upscaleAction = window.findChild<QAction *>(QStringLiteral("upscaleAction"));
    auto *libraryView = window.findChild<QListWidget *>(QStringLiteral("libraryAssetView"));
    QVERIFY(upscaleAction != nullptr);
    QVERIFY(libraryView != nullptr);
    QVERIFY(!upscaleAction->isEnabled());
    QListWidgetItem *sourceItem = nullptr;
    for (int index = 0; index < libraryView->count(); ++index) {
        QListWidgetItem *item = libraryView->item(index);
        if (item->data(Qt::UserRole).toString() == canonicalImagePath) {
            sourceItem = item;
            break;
        }
    }
    QVERIFY(sourceItem != nullptr);

    libraryView->itemDoubleClicked(sourceItem);
    QTRY_VERIFY(!upscaleAction->isEnabled());

    QTabWidget *previewTabs = nullptr;
    for (QTabWidget *tabs : window.findChildren<QTabWidget *>()) {
        if (tabs->tabsClosable()) {
            previewTabs = tabs;
            break;
        }
    }
    QVERIFY(previewTabs != nullptr);
    previewTabs->tabCloseRequested(0);
    QTRY_VERIFY(!upscaleAction->isEnabled());
}

void CuratedResourceManagerTest::providesSharedMainWindowActions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    MainWindow window;

#if defined(Q_OS_MACOS)
    QMenuBar *menuBar = window.menuBar();
    QVERIFY(menuBar != nullptr);
    QCOMPARE(menuBar->actions().size(), 3);
    QCOMPARE(menuBar->actions().at(0)->text(), QStringLiteral("&File"));
    QCOMPARE(menuBar->actions().at(1)->text(), QStringLiteral("&Image"));
    QCOMPARE(menuBar->actions().at(2)->text(), QStringLiteral("&Settings"));

    const QList<QAction *> fileActions = menuBar->actions().at(0)->menu()->actions();
    QCOMPARE(fileActions.size(), 2);
    QCOMPARE(fileActions.at(0)->text(), QStringLiteral("Import Photo"));
    QCOMPARE(fileActions.at(0)->shortcut(), QKeySequence(QKeySequence::Open));
    QCOMPARE(fileActions.at(1)->text(), QStringLiteral("Close Preview"));
    QCOMPARE(fileActions.at(1)->shortcut(), QKeySequence(QKeySequence::Close));
    QVERIFY(!fileActions.at(1)->isEnabled());

    const QList<QAction *> imageActions = menuBar->actions().at(1)->menu()->actions();
    QCOMPARE(imageActions.size(), 1);
    QCOMPARE(imageActions.at(0), window.findChild<QAction *>(QStringLiteral("upscaleAction")));
#else
    QVERIFY(window.menuWidget() == nullptr);

    auto *toolBar = window.findChild<QToolBar *>(QStringLiteral("mainToolBar"));
    QVERIFY(toolBar != nullptr);
    auto *mainMenuButton = window.findChild<QToolButton *>(QStringLiteral("mainMenuButton"));
    QVERIFY(mainMenuButton != nullptr);
    QCOMPARE(mainMenuButton->toolButtonStyle(), Qt::ToolButtonIconOnly);
    QCOMPARE(mainMenuButton->popupMode(), QToolButton::InstantPopup);
    QCOMPARE(mainMenuButton->toolTip(), QStringLiteral("Main Menu"));
    QCOMPARE(mainMenuButton->accessibleName(), QStringLiteral("Main Menu"));
    window.show();
    QTest::qWait(1);
    QVERIFY(mainMenuButton->geometry().center().x() > toolBar->contentsRect().center().x());

    QMenu *mainMenu = mainMenuButton->menu();
    QVERIFY(mainMenu != nullptr);
    const QList<QAction *> mainMenuActions = mainMenu->actions();
    QCOMPARE(mainMenuActions.size(), 6);
    QCOMPARE(mainMenuActions.at(0)->text(), QStringLiteral("Import Photo"));
    QCOMPARE(mainMenuActions.at(0)->shortcut(), QKeySequence(QKeySequence::Open));
    QCOMPARE(mainMenuActions.at(1)->text(), QStringLiteral("Close Preview"));
    QCOMPARE(mainMenuActions.at(1)->shortcut(), QKeySequence(QKeySequence::Close));
    QVERIFY(!mainMenuActions.at(1)->isEnabled());
    QVERIFY(mainMenuActions.at(2)->isSeparator());
    QCOMPARE(mainMenuActions.at(3), window.findChild<QAction *>(QStringLiteral("upscaleAction")));
    QVERIFY(mainMenuActions.at(4)->isSeparator());
    QCOMPARE(mainMenuActions.at(5)->text(), QStringLiteral("Service Preferences…"));
#endif
}

void CuratedResourceManagerTest::filtersLibraryAssets()
{
    const std::deque<ImageAsset> assets = {
        {QStringLiteral("generated"),
         QStringLiteral("generated-20260809-142502619.png"),
         QStringLiteral("/tmp/generated.png"),
         QImage(512, 512, QImage::Format_ARGB32),
         ImageAssetCategory::Generated},
        {QStringLiteral("imported"),
         QStringLiteral("portrait.png"),
         QStringLiteral("/tmp/portrait.png"),
         QImage(512, 768, QImage::Format_ARGB32),
         ImageAssetCategory::Imported},
        {QStringLiteral("upscaled"),
         QStringLiteral("upscaled-20260809-143039605.png"),
         QStringLiteral("/tmp/upscaled.png"),
         QImage(1344, 768, QImage::Format_ARGB32),
         ImageAssetCategory::Upscaled}};
    ImageLibraryWidget library(assets);

    auto *view = library.findChild<QListWidget *>(QStringLiteral("libraryAssetView"));
    auto *searchField = library.findChild<QLineEdit *>(QStringLiteral("librarySearchField"));
    auto *categorySelector = library.findChild<QComboBox *>(QStringLiteral("libraryCategorySelector"));
    QVERIFY(view != nullptr);
    QVERIFY(searchField != nullptr);
    QVERIFY(categorySelector != nullptr);
    QCOMPARE(visibleLibraryItemCount(*view), 3);

    const ImageAsset *selectedAsset = nullptr;
    connect(&library, &ImageLibraryWidget::imageSelected, this, [&selectedAsset](const ImageAsset *asset) {
        selectedAsset = asset;
    });
    library.selectAsset(&assets.front());
    QCOMPARE(selectedAsset, &assets.front());

    searchField->setText(QStringLiteral("portrait"));
    QCOMPARE(visibleLibraryItemCount(*view), 1);
    QCOMPARE(selectedAsset, nullptr);
    QCOMPARE(view->currentItem(), nullptr);

    searchField->clear();
    categorySelector->setCurrentIndex(categorySelector->findData(static_cast<int>(ImageAssetCategory::Generated)));
    QCOMPARE(visibleLibraryItemCount(*view), 1);
    QCOMPARE(view->item(0)->isHidden(), false);

    searchField->setText(QStringLiteral("generated"));
    categorySelector->setCurrentIndex(categorySelector->findData(static_cast<int>(ImageAssetCategory::Upscaled)));
    QCOMPARE(visibleLibraryItemCount(*view), 0);
    auto *emptyState = library.findChild<QLabel *>(QStringLiteral("libraryEmptyState"));
    QVERIFY(emptyState != nullptr);
    QCOMPARE(emptyState->text(), QStringLiteral("No matching images"));
}

void CuratedResourceManagerTest::switchesLibraryViewModesAndRequestsTrash()
{
    const std::deque<ImageAsset> assets = {
        {QStringLiteral("source"),
         QStringLiteral("source.png"),
         QStringLiteral("/tmp/source.png"),
         QImage(512, 512, QImage::Format_ARGB32),
         ImageAssetCategory::Imported}};
    ImageLibraryWidget library(assets);
    library.resize(320, 400);
    library.show();
    QTest::qWait(1);

    auto *view = library.findChild<QListWidget *>(QStringLiteral("libraryAssetView"));
    auto *gridButton = library.findChild<QToolButton *>(QStringLiteral("libraryGridButton"));
    auto *listButton = library.findChild<QToolButton *>(QStringLiteral("libraryListButton"));
    QVERIFY(view != nullptr);
    QVERIFY(gridButton != nullptr);
    QVERIFY(listButton != nullptr);
    QCOMPARE(view->viewMode(), QListView::IconMode);

    listButton->click();
    QCOMPARE(view->viewMode(), QListView::ListMode);
    QCOMPARE(view->flow(), QListView::TopToBottom);
    QCOMPARE(view->isWrapping(), false);

    gridButton->click();
    QCOMPARE(view->viewMode(), QListView::IconMode);
    QCOMPARE(view->flow(), QListView::LeftToRight);
    QCOMPARE(view->isWrapping(), true);

    QSignalSpy trashSpy(&library, &ImageLibraryWidget::moveToTrashRequested);
    library.selectAsset(&assets.front());
    const auto moveToTrashAction = std::find_if(view->actions().cbegin(), view->actions().cend(), [](const QAction *action) {
        return action->text() == QStringLiteral("Move to Trash");
    });
    QVERIFY(moveToTrashAction != view->actions().cend());
    (*moveToTrashAction)->trigger();
    QCOMPARE(trashSpy.count(), 1);
    QCOMPARE(trashSpy.at(0).at(0).toString(), QStringLiteral("/tmp/source.png"));
}

void CuratedResourceManagerTest::displaysPreviewOnlyForAssignedReference()
{
    ReferenceSlotWidget reference(QStringLiteral("Face Reference"));
    const ImageAsset asset = {QStringLiteral("reference"),
                              QStringLiteral("reference-photo.png"),
                              QStringLiteral("/tmp/reference-photo.png"),
                              QImage(512, 512, QImage::Format_ARGB32)};
    const ImageAsset *assignedAsset = nullptr;
    connect(&reference,
            &ReferenceSlotWidget::assignmentChanged,
            this,
            [&assignedAsset](const QString &, const ImageAsset *asset) { assignedAsset = asset; });

    reference.setSelectedAsset(&asset);
    const QList<QToolButton *> buttons = reference.findChildren<QToolButton *>();
    const auto assignButton = std::find_if(buttons.cbegin(), buttons.cend(), [](const QToolButton *button) {
        return button->text() == QStringLiteral("Use Selected");
    });
    QVERIFY(assignButton != buttons.cend());
    (*assignButton)->click();
    QCOMPARE(assignedAsset, &asset);

    const QList<QLabel *> labels = reference.findChildren<QLabel *>();
    QVERIFY(std::any_of(labels.cbegin(), labels.cend(), [](const QLabel *label) {
        return label->text() == QStringLiteral("Face Reference");
    }));
    QVERIFY(std::none_of(labels.cbegin(), labels.cend(), [&asset](const QLabel *label) {
        return label->text() == asset.name || label->text() == QStringLiteral("None");
    }));
    QVERIFY(std::any_of(labels.cbegin(), labels.cend(), [](const QLabel *label) {
        return !label->pixmap().isNull();
    }));

    const auto clearButton = std::find_if(buttons.cbegin(), buttons.cend(), [](const QToolButton *button) {
        return button->toolTip() == QStringLiteral("Clear Face Reference");
    });
    QVERIFY(clearButton != buttons.cend());
    (*clearButton)->click();
    QCOMPARE(assignedAsset, nullptr);
}

QTEST_MAIN(CuratedResourceManagerTest)

#include "CuratedResourceManagerTest.moc"
