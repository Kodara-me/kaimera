#include "AvailableModelsDialog.h"
#include "CanvasServiceController.h"
#include <Ardoise/Theme.h>
#include <Ardoise/TreeView.h>
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QtTest>

class AvailableModelsDialogTest final : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settings;
  private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("KaimeraTests");
        QCoreApplication::setApplicationName("AvailableModelsDialogTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        Ardoise::Theme::apply(*qApp);
    }
    void readsServerOptionsAndRefreshesWithoutMutations()
    {
        QWebSocketServer server("models-test", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QWebSocket* peer = nullptr;
        QList<QJsonObject> requests;
        int steps = 37;
        bool emptyUpscaler = false;
        const auto list = [&]()
        {
            QJsonArray categories;
            for (const QString& category : {QString("image-generation"),
                                            QString("upscaler"), QString("llm-chat"), QString("companion")})
            {
                QJsonObject options;
                if (category == "image-generation")
                    options = {{"width", 768},  {"height", 768},         {"steps", steps}, {"cfgScale", 5.5},
                               {"sampler", ""}, {"scheduler", "karras"}, {"clipSkip", -1}};
                if (category == "companion")
                    options = {{"subtype", "mmproj"}};
                if (category == "llm-chat")
                    options = {
                        {"temperature", 0.8}, {"topP", 0.95}, {"contextLength", 8192}, {"maxTokens", 2048},
                        {"defaultPromptEnhancer", true}, {"defaultPromptTranslator", true}};
                QJsonObject model{{"id", category + ":test.gguf"},
                                  {"name", "Server model"},
                                  {"fileName", "test.gguf"},
                                  {"bytes", 1024},
                                  {"recommendedOptions", options}};
                model.insert("explicitContent", category == "image-generation");
                if (category == "image-generation")
                    model.insert("companionFiles", QJsonObject{
                        {"clip-l", "companions/test.gguf/clip-l/clip_l.safetensors"},
                        {"clip-g", "companions/test.gguf/clip-g/clip_g.safetensors"}});
                categories.append(QJsonObject{
                    {"category", category},
                    {"models", category == "upscaler" && emptyUpscaler ? QJsonArray{} : QJsonArray{model}}});
            }
            peer->sendTextMessage(QString::fromUtf8(
                QJsonDocument(QJsonObject{{"type", "models.list.result"}, {"categories", categories}})
                    .toJson(QJsonDocument::Compact)));
        };
        connect(&server, &QWebSocketServer::newConnection, this,
                [&]
                {
                    peer = server.nextPendingConnection();
                    connect(peer, &QWebSocket::textMessageReceived, this,
                            [&](const QString& text)
                            {
                                const auto request = QJsonDocument::fromJson(text.toUtf8()).object();
                                requests.append(request);
                                if (request.value("type") == "authenticate")
                                    peer->sendTextMessage("{\"type\":\"authenticated\"}");
                                if (request.value("type") == "models.list")
                                    list();
                            });
                });
        CanvasServiceController client;
        client.setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        client.connectToService();
        QTRY_VERIFY(client.modelListAvailable());
        QVERIFY(!client.allowsExplicitModels());
        QCOMPARE(client.models(Nukleus::ModelCategory::ImageGeneration).size(), 1);
        QCOMPARE(client.generationModels().size(), 1); // Explicit models are not filtered.
        QVERIFY(client.generationModels().first().explicitContent);
        AvailableModelsDialog dialog(&client);
        dialog.show();
        auto* tree = dialog.findChild<Ardoise::TreeView*>("availableModelsTree");
        auto* table = dialog.findChild<QTableWidget*>("modelRecommendedOptions");
        auto* placeholder = dialog.findChild<QLabel*>("modelConfigurationPlaceholder");
        QVERIFY(tree);
        QVERIFY(table);
        QCOMPARE(tree->model()->rowCount(), 4);
        const auto choose = [&](int category)
        { tree->setCurrentIndex(tree->model()->index(0, 0, tree->model()->index(category, 0))); };
        choose(0);
        auto* details = dialog.findChild<QLabel*>("availableModelDetails");
        QVERIFY(details->text().contains("Explicit content: Yes"));
        QVERIFY(!details->text().contains("clip_l.safetensors"));
        QVERIFY(!details->text().contains("clip_g.safetensors"));
        QCOMPARE(table->rowCount(), 5);
        QCOMPARE(table->item(0, 1)->text(), QString("768 × 768"));
        QCOMPARE(table->item(1, 1)->text(), QString("37"));
        QCOMPARE(table->item(3, 1)->text(), QString("Automatic"));
        QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
        for (int row = 0; row < table->rowCount(); ++row)
            for (int column = 0; column < 2; ++column)
                QVERIFY(!(table->item(row, column)->flags() & Qt::ItemIsEditable));
        steps = 49;
        dialog.findChild<QPushButton*>("refreshModels")->click();
        QTRY_COMPARE(table->item(1, 1)->text(), QString("49"));
        QCOMPARE(tree->currentIndex().data(Qt::UserRole + 1).toString(),
                 QString("image-generation:test.gguf"));
        choose(1);
        QVERIFY(!table->isVisible());
        choose(2);
        QCOMPARE(table->rowCount(), 6);
        QCOMPARE(table->item(0, 0)->text(), QString("Default prompt enhancer"));
        QCOMPARE(table->item(1, 0)->text(), QString("Default prompt translator"));
        QCOMPARE(table->item(1, 1)->text(), QString("Yes"));
        QCOMPARE(client.defaultPromptEnhancerModelId(), QString("llm-chat:test.gguf"));
        QCOMPARE(client.defaultPromptTranslatorModelId(), QString("llm-chat:test.gguf"));
        choose(3);
        QCOMPARE(table->rowCount(), 1);
        QTranslator french;
        QVERIFY(french.load(":/translations/kaimera_fr.qm"));
        qApp->installTranslator(&french);
        {
            AvailableModelsDialog translated(&client);
            QCOMPARE(translated.windowTitle(), QString("Modèles disponibles"));
        }
        qApp->removeTranslator(&french);
        choose(0);
        if (qEnvironmentVariableIsSet("KIRA_MODELS_SCREENSHOT"))
            QVERIFY(dialog.grab().save(qEnvironmentVariable("KIRA_MODELS_SCREENSHOT")));
        emptyUpscaler = true;
        dialog.findChild<QPushButton*>("refreshModels")->click();
        QTRY_COMPARE(tree->model()->rowCount(tree->model()->index(1, 0)), 0);
        tree->setCurrentIndex(tree->model()->index(1, 0));
        QVERIFY(placeholder->text().contains("No models available"));
        QVERIFY(!table->isVisible());
        peer->close();
        QTRY_VERIFY(!client.isAuthenticated());
        QTRY_COMPARE(tree->model()->rowCount(tree->model()->index(0, 0)), 0);
        QVERIFY(!table->isVisible());
        QVERIFY(!dialog.findChild<QPushButton*>("refreshModels")->isEnabled());
        for (const auto& request : requests)
            QVERIFY(request.value("type") == "authenticate" || request.value("type") == "models.list");
    }
    void showsOfflineState()
    {
        CanvasServiceController client;
        AvailableModelsDialog dialog(&client);
        dialog.show();
        QVERIFY(dialog.findChild<QLabel*>("modelConfigurationPlaceholder")->text().contains("disconnected"));
        QVERIFY(!dialog.findChild<QPushButton*>("refreshModels")->isEnabled());
    }
};
QTEST_MAIN(AvailableModelsDialogTest)
#include "AvailableModelsDialogTest.moc"
