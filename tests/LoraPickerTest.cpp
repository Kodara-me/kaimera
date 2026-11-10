#include "CanvasServiceController.h"
#include "GenerationInspectorWidget.h"
#include "GenerationParametersWidget.h"
#include "GenerationWorkspaceWidget.h"
#include "IdentityPanel.h"
#include "LoraPicker.h"
#include "MainWindow.h"
#include "PromptEditorWidget.h"
#include "ShotPicker.h"
#include <Ardoise/Theme.h>
#include <QAbstractButton>
#include <QAction>
#include <QBuffer>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTranslator>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QtTest>
#include <memory>
#include <nukleus/ModelRouting.h>

namespace
{
Nukleus::ModelInfo loraModel(const QString& file, const QString& name = {})
{
    Nukleus::ModelInfo model;
    model.id = QStringLiteral("companion:") + file;
    model.category = Nukleus::ModelCategory::Companion;
    model.fileName = file;
    model.displayName = name;
    model.recommendedOptions = QJsonObject{{"subtype", "lora"}};
    return model;
}

QList<Nukleus::ModelInfo> loraModels(int count)
{
    QList<Nukleus::ModelInfo> models;
    for (int i = 1; i <= count; ++i)
        models.append(loraModel(QStringLiteral("l%1.safetensors").arg(i), QStringLiteral("LoRA %1").arg(i)));
    return models;
}

QStringList menuLabels(LoraPicker* picker)
{
    QStringList labels;
    for (auto* action : picker->findChild<QMenu*>("loraMenu")->actions()) labels << action->text();
    return labels;
}

void choose(LoraPicker* picker, const QString& label)
{
    for (auto* action : picker->findChild<QMenu*>("loraMenu")->actions())
        if (action->text() == label) { action->trigger(); return; }
    QFAIL(qPrintable("Not in the menu: " + label));
}

QStringList chosenIds(LoraPicker* picker)
{
    QStringList ids;
    for (const auto& lora : picker->chosen()) ids << lora.modelId;
    return ids;
}

// Kaimera remembers a trained LoRA's trigger word under its model id, encoded as a settings key.
void remember(const QString& modelId, const QString& triggerWord)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Loras"));
    settings.beginGroup(QString::fromLatin1(modelId.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    settings.setValue(QStringLiteral("TriggerWord"), triggerWord);
    settings.setValue(QStringLiteral("BaseModelId"), QStringLiteral("image-generation:base"));
}

QByteArray pngBytes()
{
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}
} // namespace

// A service that generates with LoRAs, edits and upscales; it records every submission and answers
// the translator with an English rendering ("EN " and the original text).
struct FakeService
{
    QWebSocketServer server{"lora-picker-test", QWebSocketServer::NonSecureMode};
    QWebSocket* peer = nullptr;
    QStringList operations{"image.generate", "image.generate.lora", "image.edit", "image.upscale", "model-routing", "prompt.enhance"};
    QJsonArray loras;
    bool translator = false;
    QList<QJsonObject> submissions;
    Nukleus::ModelRouting routing = Nukleus::Protocol::starterModelRouting();

    FakeService()
    {
        for (auto& preset : routing.presets) preset.model.modelId = "image-model";
        for (auto& quality : routing.editingQualities) quality.model.modelId = "image-model";
    }
    void send(const QJsonObject& message) const { peer->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson())); }
    void sendCapabilities() const
    {
        send({{"type", "capabilities"}, {"state", "ready"}, {"operations", QJsonArray::fromStringList(operations)}});
    }
    void sendModels() const
    {
        QJsonArray categories{
            QJsonObject{{"category", "image-generation"}, {"models", QJsonArray{
                QJsonObject{{"id", "image-model"}, {"displayName", "Image"}, {"recommendedOptions", QJsonObject{{"promptFormat", "natural"}}}},
                QJsonObject{{"id", "tag-model"}, {"displayName", "Tags"}, {"recommendedOptions", QJsonObject{{"promptFormat", "tags"}}}}}}},
            QJsonObject{{"category", "upscaler"}, {"models", QJsonArray{QJsonObject{{"id", "upscaler:up"}, {"displayName", "Up"}}}}},
            QJsonObject{{"category", "companion"}, {"models", loras}}};
        if (translator)
            categories.append(QJsonObject{{"category", "llm-chat"}, {"models", QJsonArray{QJsonObject{
                {"id", "llm-chat:translator"}, {"displayName", "Translator"},
                {"recommendedOptions", QJsonObject{{"defaultPromptTranslator", true}}}}}}});
        send({{"type", "models.list.result"}, {"categories", categories}});
    }
    void sendRouting() const
    {
        send({{"type", "model-routing.get.completed"}, {"routing", Nukleus::Protocol::modelRoutingToJson(routing)}});
    }
    static QJsonObject lora(const QString& file, const QString& name)
    {
        return {{"id", "companion:" + file}, {"fileName", file}, {"displayName", name},
            {"recommendedOptions", QJsonObject{{"subtype", "lora"}}}};
    }
    bool listen(QObject* parent)
    {
        QObject::connect(&server, &QWebSocketServer::newConnection, parent, [this, parent] {
            peer = server.nextPendingConnection();
            QObject::connect(peer, &QWebSocket::textMessageReceived, parent, [this](const QString& text) {
                const auto request = QJsonDocument::fromJson(text.toUtf8()).object();
                if (request.value("type") == "authenticate") {
                    send({{"type", "authenticated"}});
                    sendCapabilities();
                    sendRouting();
                }
                if (request.value("type") == "models.list") sendModels();
                if (request.value("type") != "submit") return;
                if (request.value("operation") == "prompt.enhance") {
                    send({{"type", "result"}, {"jobId", request.value("jobId")}, {"operation", "prompt.enhance"},
                        {"prompt", "EN " + request.value("options").toObject().value("prompt").toString()}});
                    return;
                }
                submissions.append(request);
            });
        });
        return server.listen(QHostAddress::LocalHost);
    }
    QUrl url() const { return QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())); }
    QString prompt() const { return submissions.last().value("options").toObject().value("prompt").toString(); }
    QJsonArray sentLoras() const { return submissions.last().value("options").toObject().value("loras").toArray(); }
};

class LoraPickerTest final : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settingsDirectory;
    QTranslator m_englishTranslator;

    struct Session
    {
        Session(FakeService& service, QObject* test) : window(new MainWindow)
        {
            service.listen(test);
            window->show();
            client = window->findChild<CanvasServiceController*>();
            client->setConfiguration(service.url(), {}, false);
            client->connectToService();
            picker = window->findChild<GenerationInspectorWidget*>()->parameters()->loraPicker();
            positive = window->findChild<PromptEditorWidget*>("positivePrompt");
            generate = window->findChild<QPushButton*>("generateButton");
        }
        std::unique_ptr<MainWindow> window;
        CanvasServiceController* client = nullptr;
        LoraPicker* picker = nullptr;
        PromptEditorWidget* positive = nullptr;
        QPushButton* generate = nullptr;
    };

private slots:
    void initTestCase()
    {
        Ardoise::Theme::apply(*qApp);
        QVERIFY(m_englishTranslator.load(QStringLiteral(":/translations/kaimera_en.qm")));
        qApp->installTranslator(&m_englishTranslator);
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("KaimeraTests"));
        QCoreApplication::setApplicationName(QStringLiteral("LoraPicker"));
        QVERIFY(m_settingsDirectory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
    }
    void init()
    {
        QSettings().clear();
        // A closed port keeps the windows away from a Nukleus daemon running on this machine.
        QSettings().setValue(QStringLiteral("Service/Endpoint"), QStringLiteral("ws://127.0.0.1:9"));
    }

    void menuListsInstalledLorasNotChosen_R12()
    {
        LoraPicker picker;
        picker.setSupported(true);
        auto companions = loraModels(2);
        // A companion that is not a LoRA is never offered; the label is the display label.
        Nukleus::ModelInfo projector = loraModel("projector.gguf", "Projector");
        projector.recommendedOptions = QJsonObject{{"subtype", "mmproj"}};
        companions.append(projector);
        companions.append(loraModel("plain.safetensors"));
        picker.setModels(companions);
        QCOMPARE(menuLabels(&picker), (QStringList{"LoRA 1", "LoRA 2", "plain.safetensors"}));
        choose(&picker, "LoRA 2");
        QCOMPARE(menuLabels(&picker), (QStringList{"LoRA 1", "plain.safetensors"}));
        QCOMPARE(chosenIds(&picker), (QStringList{"companion:l2.safetensors"}));
        // A LoRA installed later appears without recreating the control.
        companions.append(loraModel("late.safetensors", "Late"));
        picker.setModels(companions);
        QCOMPARE(menuLabels(&picker), (QStringList{"LoRA 1", "plain.safetensors", "Late"}));
        QCOMPARE(chosenIds(&picker), (QStringList{"companion:l2.safetensors"}));
    }

    void menuFollowsRefreshInTheCard_R12()
    {
        FakeService service;
        service.loras = {FakeService::lora("a.safetensors", "Alpha")};
        Session session(service, this);
        QTRY_VERIFY(session.picker->isEnabled());
        QCOMPARE(menuLabels(session.picker), (QStringList{"Alpha"}));
        service.loras.append(FakeService::lora("b.safetensors", "Beta"));
        service.sendModels();
        QTRY_COMPARE(menuLabels(session.picker), (QStringList{"Alpha", "Beta"}));
        choose(session.picker, "Beta");
        QCOMPARE(menuLabels(session.picker), (QStringList{"Alpha"}));
    }

    void addIsDisabledAtEight_R19()
    {
        LoraPicker picker;
        picker.setSupported(true);
        picker.setModels(loraModels(9));
        auto* add = picker.findChild<QPushButton*>("loraAdd");
        for (int i = 1; i <= 7; ++i) choose(&picker, QString("LoRA %1").arg(i));
        QVERIFY(add->isEnabled());
        choose(&picker, "LoRA 8");
        QCOMPARE(picker.chosen().size(), 8);
        QVERIFY(!add->isEnabled());
        QCOMPARE(menuLabels(&picker), (QStringList{"LoRA 9"}));
    }

    void chosenLoraHasLabelAndWeight_R20()
    {
        LoraPicker picker;
        picker.setSupported(true);
        picker.setModels(loraModels(2));
        choose(&picker, "LoRA 2");
        const auto rows = picker.findChildren<QWidget*>("loraRow");
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first()->findChild<QLabel*>("loraLabel")->text(), QString("LoRA 2"));
        auto* weight = rows.first()->findChild<QDoubleSpinBox*>("loraWeight");
        QCOMPARE(weight->minimum(), 0.0);
        QCOMPARE(weight->maximum(), 2.0);
        QCOMPARE(weight->singleStep(), 0.05);
        QCOMPARE(weight->value(), 1.0);
        QCOMPARE(picker.chosen().first().weight, std::optional<double>(1.0));
        weight->setValue(5.0);
        QCOMPARE(weight->value(), 2.0);
        weight->setValue(0.65);
        QCOMPARE(picker.chosen().first().weight, std::optional<double>(0.65));
    }

    void removingALoraListsItAgain_R21()
    {
        LoraPicker picker;
        picker.setSupported(true);
        picker.setModels(loraModels(3));
        choose(&picker, "LoRA 1");
        choose(&picker, "LoRA 3");
        QCOMPARE(menuLabels(&picker), (QStringList{"LoRA 2"}));
        auto* remove = picker.findChildren<QWidget*>("loraRow").first()->findChild<QAbstractButton*>("loraRemove");
        remove->click();
        QCOMPARE(chosenIds(&picker), (QStringList{"companion:l3.safetensors"}));
        QCOMPARE(menuLabels(&picker), (QStringList{"LoRA 1", "LoRA 2"}));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(picker.findChildren<QWidget*>("loraRow").size(), 1);
    }

    void disabledWithoutServiceSupportOrLoras_R13()
    {
        FakeService service;
        service.operations = {"image.generate", "model-routing"};
        service.loras = {FakeService::lora("a.safetensors", "Alpha")};
        Session session(service, this);
        QTRY_VERIFY(session.client->isAuthenticated() && service.peer);
        QTRY_VERIFY(!session.picker->issue().isEmpty());
        QVERIFY(!session.picker->isEnabled());
        QVERIFY(session.picker->toolTip().contains("cannot generate with LoRAs"));
        QVERIFY(!session.picker->findChild<QPushButton*>("loraAdd")->isEnabled());
        // The service supports LoRAs but none is installed.
        service.operations << "image.generate.lora";
        service.loras = {};
        service.sendCapabilities();
        service.sendModels();
        QTRY_VERIFY(session.picker->toolTip().contains("No LoRA is installed"));
        QVERIFY(!session.picker->isEnabled());
        // One is installed: the control is enabled.
        service.loras = {FakeService::lora("a.safetensors", "Alpha")};
        service.sendModels();
        QTRY_VERIFY(session.picker->isEnabled());
        QVERIFY(session.picker->toolTip().isEmpty());
    }

    void generateSendsNoLoraWithoutServiceSupport_R13()
    {
        FakeService service;
        service.loras = {FakeService::lora("a.safetensors", "Alpha")};
        Session session(service, this);
        QTRY_VERIFY(session.picker->isEnabled());
        remember("companion:a.safetensors", "alphaword");
        choose(session.picker, "Alpha");
        // The capability drops after the choice: nothing is sent, trigger word included.
        service.operations = {"image.generate", "model-routing"};
        service.sendCapabilities();
        QTRY_VERIFY(!session.picker->isEnabled());
        QCOMPARE(session.picker->chosen().size(), 1);
        session.positive->setText("a castle");
        QTRY_VERIFY(session.generate->isEnabled());
        session.generate->click();
        QTRY_COMPARE(service.submissions.size(), 1);
        QVERIFY(service.sentLoras().isEmpty());
        QCOMPARE(service.prompt(), QString("a castle"));
    }

    void generateSendsChosenLoras_R14()
    {
        FakeService service;
        service.loras = {FakeService::lora("a.safetensors", "Alpha"), FakeService::lora("b.safetensors", "Beta")};
        Session session(service, this);
        QTRY_VERIFY(session.picker->isEnabled());
        session.positive->setText("a castle");
        QTRY_VERIFY(session.generate->isEnabled());
        // No choice: the request holds no LoRA.
        session.generate->click();
        QTRY_COMPARE(service.submissions.size(), 1);
        QVERIFY(!service.submissions.first().value("options").toObject().contains("loras"));
        // Two LoRAs, in the order they were added, with their weights.
        choose(session.picker, "Beta");
        choose(session.picker, "Alpha");
        session.picker->findChildren<QDoubleSpinBox*>("loraWeight").first()->setValue(0.75);
        session.positive->setText("a castle");
        QTRY_VERIFY(session.generate->isEnabled());
        session.generate->click();
        QTRY_COMPARE(service.submissions.size(), 2);
        QCOMPARE(service.sentLoras(), (QJsonArray{QJsonObject{{"modelId", "companion:b.safetensors"}, {"weight", 0.75}},
            QJsonObject{{"modelId", "companion:a.safetensors"}, {"weight", 1.0}}}));
    }

    void generateAddsTriggerWords_R15()
    {
        FakeService service;
        service.translator = true;
        service.loras = {FakeService::lora("a.safetensors", "Alpha"), FakeService::lora("b.safetensors", "Beta"),
            FakeService::lora("c.safetensors", "Gamma")};
        remember("companion:a.safetensors", QString::fromUtf8("Alpha \xC3\x9Cn\xC3\xAF"));
        remember("companion:b.safetensors", "betaword");
        // Gamma is not remembered: it adds nothing.
        Session session(service, this);
        QTRY_VERIFY(session.picker->isEnabled());
        QTRY_COMPARE(session.client->defaultPromptTranslatorModelId(), QString("llm-chat:translator"));
        choose(session.picker, "Beta");
        choose(session.picker, "Gamma");
        choose(session.picker, "Alpha");
        session.window->findChild<ShotPicker*>("shotPicker")->setCurrentShot(ShotPicker::Shot::Portrait);
        session.positive->setText("un chateau");
        QTRY_VERIFY(session.generate->isEnabled());
        session.generate->click();
        QTRY_COMPARE(service.submissions.size(), 1);
        // The shot clauses, then the trigger words in the order added (untranslated), then the translation.
        QCOMPARE(service.prompt(), QString::fromUtf8(
            "head and shoulders portrait, framed from the chest up, betaword, Alpha \xC3\x9Cn\xC3\xAF, EN un chateau"));
        // The editors never show them: the draft is emptied after the submission and holds only the user's text.
        QVERIFY(!session.positive->text().contains("betaword"));
        QVERIFY(!session.window->findChild<GenerationInspectorWidget*>()->positivePrompt().contains("betaword"));
    }

    void tagsModelKeepsTriggerWordsAsRemembered_R15()
    {
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QVERIFY(QDir().mkpath(folder));
        QFile csv(folder + "/danbooru.csv");
        const auto removeCsv = qScopeGuard([&] { csv.remove(); });
        QVERIFY(csv.open(QIODevice::WriteOnly));
        csv.write("1girl,0,6008644,\"1girls,sole_female\"\nsolo,0,5000954,\n");
        csv.close();
        FakeService service;
        for (auto& preset : service.routing.presets) preset.model.modelId = "tag-model";
        service.loras = {FakeService::lora("a.safetensors", "Alpha")};
        remember("companion:a.safetensors", "1girls");
        Session session(service, this);
        QTRY_VERIFY(session.picker->isEnabled());
        choose(session.picker, "Alpha");
        session.positive->setText("1girls, solo");
        QTRY_VERIFY(session.generate->isEnabled());
        session.generate->click();
        QTRY_COMPARE(service.submissions.size(), 1);
        // The user's tags take the Danbooru spelling; the trigger word is sent as remembered.
        QCOMPARE(service.prompt(), QString("1girls, 1girl, solo"));
    }

    void newImageClearsPresetKeepsRefreshDrops_R16()
    {
        FakeService service;
        service.loras = {FakeService::lora("a.safetensors", "Alpha"), FakeService::lora("b.safetensors", "Beta")};
        Session session(service, this);
        QTRY_VERIFY(session.picker->isEnabled());
        auto* panel = session.window->findChild<GenerationInspectorWidget*>();
        auto* preset = panel->parameters()->findChild<QComboBox*>("generationPreset");
        QTRY_VERIFY(preset->count() > 1);
        choose(session.picker, "Alpha");
        choose(session.picker, "Beta");
        // A change of preset keeps them.
        preset->setCurrentIndex(preset->currentIndex() == 0 ? 1 : 0);
        QCOMPARE(session.picker->chosen().size(), 2);
        // A refresh that removes one drops it from the choice.
        service.loras = {FakeService::lora("b.safetensors", "Beta")};
        service.sendModels();
        QTRY_COMPARE(chosenIds(session.picker), (QStringList{"companion:b.safetensors"}));
        // New Image clears the choice.
        session.window->findChild<QAction*>("newImageAction")->trigger();
        QVERIFY(session.picker->chosen().isEmpty());
        QCOMPARE(menuLabels(session.picker), (QStringList{"Beta"}));
    }

    void otherPathsSendNoLora_R17()
    {
        FakeService service;
        service.loras = {FakeService::lora("a.safetensors", "Alpha")};
        remember("companion:a.safetensors", "alphaword");
        Session session(service, this);
        QTRY_VERIFY(session.picker->isEnabled());
        choose(session.picker, "Alpha");
        const QImage image(16, 16, QImage::Format_RGB32);
        const auto finish = [&](const QJsonObject& submission) {
            service.send({{"type", "result"}, {"jobId", submission.value("jobId")},
                {"operation", submission.value("operation")}, {"mimeType", "image/png"}});
            service.peer->sendBinaryMessage(pngBytes());
        };
        const auto checkLast = [&](int count, const QString& operation) {
            QTRY_COMPARE(service.submissions.size(), count);
            const auto submission = service.submissions.last();
            QCOMPARE(submission.value("operation").toString(), operation);
            QVERIFY(!submission.value("options").toObject().contains("loras"));
            QVERIFY(!QJsonDocument(submission).toJson().contains("alphaword"));
            finish(submission);
            QTRY_VERIFY(!session.client->jobActive());
        };
        const QString quality = service.routing.editingQualities.first().id;
        // Edit, and Edit with an identity (no translation).
        QVERIFY(session.client->queueEdit(QUuid::createUuid(), image, "Add a hat", {}, quality, false));
        checkLast(1, "image.edit");
        QVERIFY(session.client->queueEdit(QUuid::createUuid(), image, "Use the references.", {}, quality, false));
        checkLast(2, "image.edit");
        // Improve sharpness and upscaling.
        QVERIFY(session.client->submitSharpen(image));
        checkLast(3, "image.edit");
        QVERIFY(session.client->submitUpscale(image));
        checkLast(4, "image.upscale");
        // A generation of Kira Sparkle (queued, not translated) and an identity portrait (submitted
        // directly): requests with no LoRA add no trigger word, even when one is remembered.
        Nukleus::ImageGenerationRequest request;
        request.modelId = "image-model";
        request.prompt = "a knight";
        QVERIFY(session.client->queueGeneration(QUuid::createUuid(), request, {}, {}, {}, false));
        QTRY_COMPARE(service.submissions.size(), 5);
        QVERIFY(!service.submissions.last().value("options").toObject().contains("loras"));
        QCOMPARE(service.prompt(), QString("a knight"));
        finish(service.submissions.last());
        QTRY_VERIFY(!session.client->jobActive());
        QVERIFY(session.client->submitGeneration(request));
        QTRY_COMPARE(service.submissions.size(), 6);
        QCOMPARE(service.prompt(), QString("a knight"));
        QVERIFY(!service.submissions.last().value("options").toObject().contains("loras"));
        // The identity panel, which reuses the card's parameters, offers no LoRA control.
        session.window->findChild<QAction*>("createPromptIdentityAction")->trigger();
        auto* identity = session.window->findChild<IdentityPanel*>();
        auto* identityPicker = identity->findChild<LoraPicker*>();
        QVERIFY(identityPicker);
        QVERIFY(!identityPicker->isVisibleTo(identity));
    }
};

QTEST_MAIN(LoraPickerTest)
#include "LoraPickerTest.moc"
