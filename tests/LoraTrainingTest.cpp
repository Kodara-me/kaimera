#include "AvailableModelsDialog.h"
#include "CanvasServiceController.h"
#include "GenerationInspectorWidget.h"
#include "GenerationParametersWidget.h"
#include "GenerationWorkspaceWidget.h"
#include "JobActivityIndicator.h"
#include "LoraDataset.h"
#include "MainWindow.h"
#include "TrainLoraDialog.h"
#include <Ardoise/Dialog.h>
#include <Ardoise/Theme.h>
#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTranslator>
#include <QTreeView>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QtTest>
#include <functional>

namespace
{
QByteArray imageBytes(const QColor& color, const char* format)
{
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format);
    return bytes;
}

void writeFile(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    file.write(content);
}

Nukleus::ModelInfo imageModel(const QString& id, const QString& fileName, const QString& type = QStringLiteral("checkpoint"),
    const QString& displayName = {})
{
    Nukleus::ModelInfo model;
    model.id = id;
    model.fileName = fileName;
    model.modelType = type;
    model.displayName = displayName;
    return model;
}

Nukleus::ModelInfo companion(const QString& fileName)
{
    Nukleus::ModelInfo model;
    model.id = QStringLiteral("companion:") + fileName;
    model.fileName = fileName;
    return model;
}
} // namespace

// A service that trains LoRAs: it asks for each image after the submission, and the test says
// how the job goes on.
struct FakeTrainer
{
    QWebSocketServer server{"lora-test", QWebSocketServer::NonSecureMode};
    QWebSocket* peer = nullptr;
    QStringList operations{"lora.train"};
    QJsonArray checkpoints;
    QJsonArray companions;
    QList<QJsonObject> submissions;
    QHash<QString, QList<QByteArray>> uploads;
    // Stops asking for images after this many, to cut the connection while uploading.
    int uploadLimit = -1;
    int modelRequests = 0;

    void send(const QJsonObject& message) const
    {
        peer->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));
    }
    void sendCapabilities() const
    {
        send({{"type", "capabilities"}, {"state", "ready"}, {"operations", QJsonArray::fromStringList(operations)}});
    }
    void sendModels() const
    {
        send({{"type", "models.list.result"}, {"categories", QJsonArray{
            QJsonObject{{"category", "image-generation"}, {"models", checkpoints}},
            QJsonObject{{"category", "companion"}, {"models", companions}}}}});
    }
    static QJsonObject checkpoint(const QString& file, const QString& name = {}, const QString& type = {})
    {
        QJsonObject model{{"id", "image-generation:" + file}, {"fileName", file}};
        if (!name.isEmpty()) model.insert("displayName", name);
        if (!type.isEmpty()) model.insert("modelType", type);
        return model;
    }
    static QJsonObject lora(const QString& file)
    {
        return {{"id", "companion:" + file}, {"fileName", file}, {"displayName", QFileInfo(file).completeBaseName()},
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
                }
                if (request.value("type") == "models.list") {
                    ++modelRequests;
                    sendModels();
                }
                if (request.value("type") != "submit") return;
                submissions.append(request);
                if (uploadLimit != 0)
                    send({{"type", "upload-required"}, {"jobId", request.value("jobId")}, {"inputIndex", 0}});
            });
            QObject::connect(peer, &QWebSocket::binaryMessageReceived, parent, [this](const QByteArray& bytes) {
                const auto& job = submissions.last();
                auto& images = uploads[job.value("jobId").toString()];
                images.append(bytes);
                const int count = job.value("options").toObject().value("captions").toArray().size();
                if (images.size() < count && (uploadLimit < 0 || images.size() < uploadLimit))
                    send({{"type", "upload-required"}, {"jobId", job.value("jobId")}, {"inputIndex", int(images.size())}});
            });
        });
        return server.listen(QHostAddress::LocalHost);
    }
};

class LoraTrainingTest final : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settingsDirectory;
    QTranslator m_englishTranslator;

    // Lets the next modal dialog of type T be driven by action once it is open.
    template <typename T>
    static void driveNext(QWidget* owner, std::function<void(T*)> action)
    {
        auto* poll = new QTimer(owner);
        poll->setInterval(10);
        connect(poll, &QTimer::timeout, owner, [owner, poll, action] {
            auto* dialog = owner->findChild<T*>();
            if (!dialog || !dialog->isVisible()) return;
            poll->stop();
            poll->deleteLater();
            action(dialog);
        });
        poll->start();
    }

    // A folder with three images named so that the sort order differs from the creation order.
    static QStringList makeFolder(const QTemporaryDir& root, const QString& name)
    {
        const QString folder = root.filePath(name);
        writeFile(folder + "/b.png", imageBytes(Qt::red, "PNG"));
        writeFile(folder + "/a.jpg", imageBytes(Qt::green, "JPG"));
        writeFile(folder + "/c.JPEG", imageBytes(Qt::blue, "JPG"));
        writeFile(folder + "/a.txt", "  a red dress\n");
        return {folder + "/a.jpg", folder + "/b.png", folder + "/c.JPEG"};
    }

    static QList<Nukleus::ModelInfo> twoCheckpoints()
    {
        return {imageModel("image-generation:one.safetensors", "one.safetensors", "checkpoint", "One"),
            imageModel("image-generation:two.safetensors", "two.safetensors", "checkpoint", "Two")};
    }

    // Opens the form from the toolbar button, fills it as the user does and presses Train.
    void trainFrom(MainWindow& window, const QString& folder, const QString& name, const QString& trigger, int steps)
    {
        driveNext<TrainLoraDialog>(&window, [=](TrainLoraDialog* dialog) {
            dialog->setFolder(folder);
            dialog->findChild<QLineEdit*>("loraName")->setText(name);
            auto* triggerWord = dialog->findChild<QLineEdit*>("loraTriggerWord");
            triggerWord->setText(trigger);
            emit triggerWord->textEdited(trigger);
            dialog->findChild<QSpinBox*>("loraSteps")->setValue(steps);
            auto* train = dialog->findChild<QPushButton*>("loraTrain");
            QVERIFY2(train->isEnabled(), qPrintable(dialog->status()));
            train->click();
        });
        window.findChild<QToolButton*>("imageTrainLora")->click();
    }

private slots:
    void initTestCase()
    {
        Ardoise::Theme::apply(*qApp);
        QVERIFY(m_englishTranslator.load(QStringLiteral(":/translations/kaimera_en.qm")));
        qApp->installTranslator(&m_englishTranslator);
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("KaimeraTests"));
        QCoreApplication::setApplicationName(QStringLiteral("LoraTraining"));
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

    void scansImagesInFolder_R3()
    {
        QTemporaryDir root;
        const auto images = makeFolder(root, "set");
        writeFile(root.filePath("set/notes.txt"), "x");
        writeFile(root.filePath("set/anim.gif"), "x");
        writeFile(root.filePath("set/photo.PnG"), imageBytes(Qt::white, "PNG"));
        writeFile(root.filePath("set/inner/deep.png"), imageBytes(Qt::white, "PNG"));
        QDir().mkpath(root.filePath("set/folder.png"));
        const QStringList found = LoraDataset::scanImages(root.filePath("set"));
        QStringList names;
        for (const QString& path : found) names << QFileInfo(path).fileName();
        // Sorted by file name; only PNG and JPEG files placed directly in the folder.
        QCOMPARE(names, (QStringList{"a.jpg", "b.png", "c.JPEG", "photo.PnG"}));
        QVERIFY(QFileInfo(found.first()).isAbsolute());
        QCOMPARE(LoraDataset::captionedCount(found), 1);
        QVERIFY(LoraDataset::scanImages(root.filePath("missing")).isEmpty());
        Q_UNUSED(images);
    }

    void derivesNameFromFolder_R4()
    {
        QCOMPARE(LoraDataset::deriveName("C:/pictures/My Hero (v2)"), QString("My_Hero__v2_"));
        QCOMPARE(LoraDataset::deriveName("/data/kept_name-1"), QString("kept_name-1"));
        QCOMPARE(LoraDataset::deriveName("/data/élan"), QString("_lan"));
        QCOMPARE(LoraDataset::deriveName("/data/" + QString(80, 'x')), QString(64, 'x'));
        QVERIFY(LoraDataset::isValidName(QString(64, 'a')));
        QVERIFY(!LoraDataset::isValidName(QString(65, 'a')));
        QVERIFY(!LoraDataset::isValidName("a b"));
        QVERIFY(!LoraDataset::isValidName(""));
    }

    void buildsCaptions_R5()
    {
        QTemporaryDir root;
        const auto images = makeFolder(root, "set");
        // A caption file is trimmed; one that is blank counts as none.
        writeFile(root.filePath("set/b.txt"), "   \n\t ");
        writeFile(root.filePath("set/c.txt"), QByteArray("\xEF\xBB\xBF") + "caf\xC3\xA9 au lait \r\n");
        QCOMPARE(LoraDataset::captionFileText(images.at(0)), QString("a red dress"));
        QCOMPARE(LoraDataset::captionFileText(images.at(1)), QString());
        QCOMPARE(LoraDataset::captionFileText(images.at(2)), QString("caf\u00e9 au lait"));
        QCOMPARE(LoraDataset::caption("hero", "a red dress"), QString("hero, a red dress"));
        QCOMPARE(LoraDataset::caption("hero", ""), QString("hero"));
        QCOMPARE(LoraDataset::captionedCount(images), 2);
    }

    void listsBaseModels_R6()
    {
        const auto models = LoraDataset::baseModels({
            imageModel("a", "a.safetensors"), imageModel("b", "b.SafeTensors"), imageModel("c", "c.gguf"),
            imageModel("d", "d.safetensors", "diffusion-model"), imageModel("e", "e.ckpt")});
        QStringList ids;
        for (const auto& model : models) ids << model.id;
        QCOMPARE(ids, (QStringList{"a", "b"}));
    }

    void dialogFieldsAndDefaults_R2_R6()
    {
        QTemporaryDir root;
        makeFolder(root, "My Hero");
        TrainLoraDialog dialog(twoCheckpoints(), "image-generation:two.safetensors", {}, false);
        // The five values, and nothing else to type.
        int typed = 0;
        for (auto* edit : dialog.findChildren<QLineEdit*>()) typed += qobject_cast<QAbstractSpinBox*>(edit->parentWidget()) ? 0 : 1;
        QCOMPARE(typed, 2);
        QCOMPARE(dialog.findChildren<QComboBox*>().size(), 1);
        QCOMPARE(dialog.findChildren<QSpinBox*>().size(), 1);
        QVERIFY(dialog.findChild<QPushButton*>("loraFolderButton"));
        auto* steps = dialog.findChild<QSpinBox*>("loraSteps");
        QCOMPARE(steps->minimum(), 1);
        QCOMPARE(steps->maximum(), 20000);
        QCOMPARE(steps->value(), 1500);
        // R6: the model selected in the card is selected; the labels are the display names.
        auto* base = dialog.findChild<QComboBox*>("loraBaseModel");
        QCOMPARE(base->count(), 2);
        QCOMPARE(base->itemText(0), QString("One"));
        QCOMPARE(dialog.baseModelId(), QString("image-generation:two.safetensors"));
        QVERIFY(dialog.findChild<QLabel*>("loraBaseModelNote")->text().contains("SDXL"));
        TrainLoraDialog other(twoCheckpoints(), "image-generation:unlisted.gguf", {}, false);
        QCOMPARE(other.baseModelId(), QString("image-generation:one.safetensors"));
        // R3, R4: the summary, and the name and trigger word that follow the folder.
        dialog.setFolder(root.filePath("My Hero"));
        QCOMPARE(dialog.findChild<QLabel*>("loraFolderSummary")->text(), QString("3 images, 1 caption"));
        QCOMPARE(dialog.name(), QString("My_Hero"));
        QCOMPARE(dialog.triggerWord(), QString("My_Hero"));
        // A typed name stays when another folder is chosen; the trigger word still follows it.
        auto* name = dialog.findChild<QLineEdit*>("loraName");
        name->setText("typed");
        emit name->textEdited("typed");
        makeFolder(root, "Other");
        dialog.setFolder(root.filePath("Other"));
        QCOMPARE(dialog.name(), QString("typed"));
        QCOMPARE(dialog.triggerWord(), QString("typed"));
        // Once the trigger word is edited, it stays.
        auto* trigger = dialog.findChild<QLineEdit*>("loraTriggerWord");
        trigger->setText("zed");
        emit trigger->textEdited("zed");
        name->setText("renamed");
        QCOMPARE(dialog.triggerWord(), QString("zed"));
    }

    void dialogChecksConditions_R7()
    {
        QTemporaryDir root;
        makeFolder(root, "set");
        QDir().mkpath(root.filePath("empty"));
        for (int i = 0; i < 65; ++i) writeFile(root.filePath(QString("many/%1.png").arg(i, 3, 10, QChar('0'))), "x");
        TrainLoraDialog dialog(twoCheckpoints(), {}, {"Existing", "Other.v2"}, false);
        auto* train = dialog.findChild<QPushButton*>("loraTrain");
        const auto fill = [&](const QString& name, const QString& trigger) {
            dialog.findChild<QLineEdit*>("loraName")->setText(name);
            dialog.findChild<QLineEdit*>("loraTriggerWord")->setText(trigger);
        };
        QVERIFY(!train->isEnabled());
        QCOMPARE(dialog.status(), QString("Choose a folder of PNG or JPEG images."));
        dialog.setFolder(root.filePath("empty"));
        QCOMPARE(dialog.status(), QString("This folder holds no PNG or JPEG image."));
        dialog.setFolder(root.filePath("many"));
        QVERIFY(dialog.status().contains("64") && dialog.status().contains("65"));
        QVERIFY(!train->isEnabled());
        dialog.setFolder(root.filePath("set"));
        QVERIFY2(train->isEnabled(), qPrintable(dialog.status()));
        QCOMPARE(dialog.status(), QString());
        // The name: 1 to 64 characters of the set, and not an installed companion, in any letter case.
        fill("", "hero");
        QCOMPARE(dialog.status(), QString("Enter a name for the LoRA."));
        fill("bad name", "hero");
        QVERIFY(dialog.status().startsWith("The name can hold only"));
        QVERIFY(!train->isEnabled());
        fill("EXISTING", "hero");
        QVERIFY(dialog.status().contains("already installed"));
        QVERIFY(!train->isEnabled());
        fill("Existing2", "hero");
        QVERIFY(train->isEnabled());
        // The trigger word: not blank, no <, > or comma, 100 characters at most.
        fill("fine", "   ");
        QCOMPARE(dialog.status(), QString("Enter a trigger word."));
        for (const QString& bad : {QString("a,b"), QString("<lora>"), QString("a>b")}) {
            fill("fine", bad);
            QCOMPARE(dialog.status(), QString("The trigger word cannot hold <, > or a comma."));
            QVERIFY(!train->isEnabled());
        }
        fill("fine", QString(101, 'k'));
        QCOMPARE(dialog.triggerWord().size(), 100);
        QVERIFY(train->isEnabled());
        // A caption over 1000 characters names its image; the trigger word counts in it.
        writeFile(root.filePath("set/b.txt"), QByteArray(994, 'x'));
        dialog.setFolder(root.filePath("set"));
        fill("fine", "hero");
        QVERIFY(train->isEnabled());
        fill("fine", "heroes");
        QCOMPARE(dialog.status(), QString("The caption of b.png holds more than 1000 characters. Shorten its .txt file."));
        QVERIFY(!train->isEnabled());
        fill("fine", "hero");
        // A single job running keeps Train disabled, with its message.
        dialog.setJobActive(true);
        QCOMPARE(dialog.status(), QString("Wait for the current job to finish."));
        QVERIFY(!train->isEnabled());
        dialog.setJobActive(false);
        QVERIFY(train->isEnabled());
        // The request: the images in order, their captions, the name, the base model and the steps.
        dialog.findChild<QSpinBox*>("loraSteps")->setValue(300);
        const auto request = dialog.request();
        QVERIFY(request);
        QCOMPARE(request->outputName, QString("fine"));
        QCOMPARE(request->baseModelId, QString("image-generation:one.safetensors"));
        QCOMPARE(request->maxTrainSteps, std::optional<int>(300));
        QVERIFY(!request->networkDim && !request->networkAlpha && !request->learningRate && !request->resolution
            && !request->seed && !request->trainTextEncoder);
        QCOMPARE(request->images.size(), 3);
        QCOMPARE(request->images.at(0).caption, QString("hero, a red dress"));
        QCOMPARE(request->images.at(1).caption, QString("hero, ") + QString(994, 'x'));
        QCOMPARE(request->images.at(2).caption, QString("hero"));
        QCOMPARE(request->images.at(0).data, imageBytes(Qt::green, "JPG"));
        QCOMPARE(request->images.at(1).data, imageBytes(Qt::red, "PNG"));
    }

    void trainButtonFollowsService_R1()
    {
        FakeTrainer service;
        QVERIFY(service.listen(this));
        MainWindow window;
        window.show();
        auto* client = window.findChild<CanvasServiceController*>();
        auto* button = window.findChild<QToolButton*>("imageTrainLora");
        auto* workspace = window.findChild<GenerationWorkspaceWidget*>();
        QVERIFY(client && button && workspace);
        QVERIFY(!button->isEnabled());
        QVERIFY(button->toolTip().contains("Connect to the service"));
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(service.server.serverPort())), {}, false);
        client->connectToService();
        // The service cannot train.
        service.operations = {"image.generate"};
        service.checkpoints = {FakeTrainer::checkpoint("sdxl.safetensors")};
        QTRY_VERIFY(client->isAuthenticated() && service.peer);
        service.sendCapabilities();
        service.sendModels();
        QTRY_COMPARE(button->toolTip(), QString("The service cannot train LoRAs — update Nukleus."));
        QVERIFY(!button->isEnabled());
        // It can, but no SDXL checkpoint file is installed.
        service.operations << "lora.train";
        service.checkpoints = {FakeTrainer::checkpoint("flux.gguf"), FakeTrainer::checkpoint("other.safetensors", {}, "diffusion-model")};
        service.sendCapabilities();
        service.sendModels();
        QTRY_VERIFY(button->toolTip().startsWith("No SDXL checkpoint"));
        QVERIFY(!button->isEnabled());
        // A checkpoint appears: the button is enabled with no image selected.
        service.checkpoints.append(FakeTrainer::checkpoint("SDXL.SAFETENSORS"));
        service.sendModels();
        QTRY_VERIFY(button->isEnabled());
        QCOMPARE(button->toolTip(), QString("Train a LoRA…"));
        QVERIFY(workspace->currentImage().isNull());
        // It is a plain tool of the identity section, not one of the selected image's tools.
        service.operations = {"image.generate"};
        service.sendCapabilities();
        QTRY_VERIFY(!button->isEnabled());
    }

    void trainsThroughService_R8_R9_R18()
    {
        QTemporaryDir root;
        const auto images = makeFolder(root, "hero");
        FakeTrainer service;
        QVERIFY(service.listen(this));
        service.checkpoints = {FakeTrainer::checkpoint("sdxl.safetensors", "SDXL base"), FakeTrainer::checkpoint("flux.gguf")};
        service.companions = {FakeTrainer::lora("Existing.safetensors")};
        MainWindow window;
        window.show();
        auto* client = window.findChild<CanvasServiceController*>();
        auto* workspace = window.findChild<GenerationWorkspaceWidget*>();
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(service.server.serverPort())), {}, false);
        client->connectToService();
        auto* button = window.findChild<QToolButton*>("imageTrainLora");
        QTRY_VERIFY(button->isEnabled());
        // R7: the installed LoRA's name is refused, in the real form.
        driveNext<TrainLoraDialog>(&window, [&](TrainLoraDialog* dialog) {
            dialog->setFolder(root.filePath("hero"));
            dialog->findChild<QLineEdit*>("loraName")->setText("existing");
            QVERIFY(!dialog->findChild<QPushButton*>("loraTrain")->isEnabled());
            QVERIFY(dialog->status().contains("already installed"));
            dialog->findChild<QPushButton*>("loraCancel")->click();
        });
        button->click();
        QVERIFY(service.submissions.isEmpty() && !client->jobActive());
        // R8: Train closes the form and submits one training.
        trainFrom(window, root.filePath("hero"), "hero", "hero", 300);
        QTRY_COMPARE(service.submissions.size(), 1);
        QVERIFY(!window.findChild<TrainLoraDialog*>());
        QVERIFY(client->jobActive() && client->loraTrainingActive());
        const auto submission = service.submissions.first();
        const auto options = submission.value("options").toObject();
        QCOMPARE(submission.value("operation").toString(), QString("lora.train"));
        QCOMPARE(options.value("baseModelId").toString(), QString("image-generation:sdxl.safetensors"));
        QCOMPARE(options.value("outputName").toString(), QString("hero"));
        QCOMPARE(options.value("captions").toArray(), (QJsonArray{"hero, a red dress", "hero", "hero"}));
        QCOMPARE(options.value("settings").toObject(), (QJsonObject{{"maxTrainSteps", 300}}));
        QCOMPARE(options.keys(), (QStringList{"baseModelId", "captions", "outputName", "settings"}));
        const QString job = submission.value("jobId").toString();
        QTRY_COMPARE(service.uploads.value(job).size(), 3);
        QCOMPARE(service.uploads.value(job).at(0), imageBytes(Qt::green, "JPG"));
        QCOMPARE(service.uploads.value(job).at(1), imageBytes(Qt::red, "PNG"));
        QCOMPARE(service.uploads.value(job).at(2), imageBytes(Qt::blue, "JPG"));
        // The pill holds the job: title, stage and percentage.
        auto* pill = workspace->activity();
        QCOMPARE(pill->state(), JobActivityIndicator::State::Running);
        QCOMPARE(pill->title(), QString("Training the LoRA hero"));
        // A second job cannot start meanwhile.
        Nukleus::LoraTrainingRequest another;
        another.images.append(Nukleus::LoraTrainingImage{"x", "c"});
        QVERIFY(!client->trainLora(another, "x"));
        service.send({{"type", "queued"}, {"jobId", job}});
        QTRY_COMPARE(pill->status(), QString("Queued…"));
        service.send({{"type", "running"}, {"jobId", job}});
        QTRY_COMPARE(pill->status(), QString("Training…"));
        service.send({{"type", "progress"}, {"jobId", job}, {"progress", QJsonObject{{"stage", "training"},
            {"percentage", 40}, {"currentStep", 120}, {"totalSteps", 300}}}});
        QTRY_COMPARE(pill->status(), QString("Training: 120 / 300"));
        QCOMPARE(pill->percentage(), 40);
        QVERIFY(client->jobActive());
        // R9: success ends the pill and refreshes the models, which list the new LoRA.
        const int requests = service.modelRequests;
        service.companions.append(FakeTrainer::lora("hero.safetensors"));
        service.send({{"type", "result"}, {"jobId", job}, {"operation", "lora.train"},
            {"modelId", "companion:hero.safetensors"}, {"steps", 300}, {"seed", 7}, {"durationMs", 1000}});
        QTRY_COMPARE(pill->state(), JobActivityIndicator::State::Succeeded);
        QVERIFY(!client->jobActive() && !client->loraTrainingActive());
        QTRY_VERIFY(service.modelRequests > requests);
        QTRY_VERIFY(std::any_of(client->models(Nukleus::ModelCategory::Companion).cbegin(),
            client->models(Nukleus::ModelCategory::Companion).cend(),
            [](const auto& model) { return model.id == "companion:hero.safetensors" && model.recommendedOptions.value("subtype") == "lora"; }));
        // R9: Available Models lists it among the installed companions.
        AvailableModelsDialog models(client, &window);
        auto* tree = models.findChild<QTreeView*>("availableModelsTree");
        const std::function<bool(const QModelIndex&)> lists = [&](const QModelIndex& parent) {
            for (int row = 0; row < tree->model()->rowCount(parent); ++row) {
                const auto index = tree->model()->index(row, 0, parent);
                if (index.data().toString().contains("hero")) return true;
                if (lists(index)) return true;
            }
            return false;
        };
        QVERIFY(lists({}));
        // R18: the trigger word and the base model are stored under the model id of the LoRA, and a
        // new settings object on the same file reads them.
        const auto remembered = CanvasServiceController::rememberedLora("companion:hero.safetensors");
        QVERIFY(remembered);
        QCOMPARE(remembered->triggerWord, QString("hero"));
        QCOMPARE(remembered->baseModelId, QString("image-generation:sdxl.safetensors"));
        QSettings reopened(QSettings::IniFormat, QSettings::UserScope, "KaimeraTests", "LoraTraining");
        reopened.beginGroup("Loras");
        QCOMPARE(reopened.childGroups().size(), 1);
        reopened.beginGroup(reopened.childGroups().first());
        QCOMPARE(reopened.value("TriggerWord").toString(), QString("hero"));
        QCOMPARE(reopened.value("BaseModelId").toString(), QString("image-generation:sdxl.safetensors"));
        QVERIFY(!CanvasServiceController::rememberedLora("companion:other.safetensors"));
        Q_UNUSED(images);
    }

    void failsWithServiceMessage_R10()
    {
        QTemporaryDir root;
        makeFolder(root, "alpha");
        makeFolder(root, "beta");
        makeFolder(root, "gamma");
        makeFolder(root, "delta");
        FakeTrainer service;
        QVERIFY(service.listen(this));
        service.checkpoints = {FakeTrainer::checkpoint("sdxl.safetensors")};
        MainWindow window;
        window.show();
        auto* client = window.findChild<CanvasServiceController*>();
        auto* pill = window.findChild<GenerationWorkspaceWidget*>()->activity();
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(service.server.serverPort())), {}, false);
        client->connectToService();
        QTRY_VERIFY(window.findChild<QToolButton*>("imageTrainLora")->isEnabled());
        const auto reconnect = [&] {
            client->connectToService();
            QTRY_VERIFY(client->isAuthenticated());
            QTRY_VERIFY(window.findChild<QToolButton*>("imageTrainLora")->isEnabled());
        };
        const auto nothingRemembered = [&](const QString& name) {
            QVERIFY(!CanvasServiceController::rememberedLora("companion:" + name + ".safetensors"));
            QSettings settings;
            settings.beginGroup("Loras");
            QVERIFY(settings.childGroups().isEmpty());
        };
        // The trainer fails: the message and its diagnostics reach the pill.
        trainFrom(window, root.filePath("alpha"), "alpha", "alpha", 100);
        QTRY_COMPARE(service.submissions.size(), 1);
        const QString first = service.submissions.last().value("jobId").toString();
        QTRY_COMPARE(service.uploads.value(first).size(), 3);
        service.send({{"type", "output"}, {"jobId", first}, {"stream", "stderr"}, {"text", "CUDA out of memory"}});
        service.send({{"type", "error"}, {"jobId", first}, {"code", "training-failed"}, {"message", "The trainer stopped"}});
        QTRY_COMPARE(pill->state(), JobActivityIndicator::State::Failed);
        QCOMPARE(pill->title(), QString("LoRA training failed"));
        QCOMPARE(pill->status(), QString("The trainer stopped\nCUDA out of memory"));
        QVERIFY(!client->jobActive());
        nothingRemembered("alpha");
        // The connection is lost while the images upload.
        service.uploadLimit = 1;
        trainFrom(window, root.filePath("beta"), "beta", "beta", 100);
        QTRY_COMPARE(service.submissions.size(), 2);
        const QString second = service.submissions.last().value("jobId").toString();
        QTRY_COMPARE(service.uploads.value(second).size(), 1);
        QVERIFY(client->jobActive());
        service.peer->close();
        QTRY_COMPARE(pill->state(), JobActivityIndicator::State::Failed);
        QVERIFY(!pill->status().isEmpty());
        QVERIFY(!client->jobActive() && !client->loraTrainingActive());
        nothingRemembered("beta");
        // While it is queued.
        service.uploadLimit = -1;
        reconnect();
        trainFrom(window, root.filePath("gamma"), "gamma", "gamma", 100);
        QTRY_COMPARE(service.submissions.size(), 3);
        const QString third = service.submissions.last().value("jobId").toString();
        QTRY_COMPARE(service.uploads.value(third).size(), 3);
        service.send({{"type", "queued"}, {"jobId", third}});
        QTRY_COMPARE(pill->status(), QString("Queued…"));
        service.peer->close();
        QTRY_COMPARE(pill->state(), JobActivityIndicator::State::Failed);
        QVERIFY(!client->jobActive());
        nothingRemembered("gamma");
        // While it runs.
        reconnect();
        trainFrom(window, root.filePath("delta"), "delta", "delta", 100);
        QTRY_COMPARE(service.submissions.size(), 4);
        const QString fourth = service.submissions.last().value("jobId").toString();
        QTRY_COMPARE(service.uploads.value(fourth).size(), 3);
        service.send({{"type", "running"}, {"jobId", fourth}});
        QTRY_COMPARE(pill->status(), QString("Training…"));
        service.peer->close();
        QTRY_COMPARE(pill->state(), JobActivityIndicator::State::Failed);
        QVERIFY(!client->jobActive());
        nothingRemembered("delta");
    }

    void asksBeforeClosingDuringTraining_R11()
    {
        QTemporaryDir root;
        makeFolder(root, "hero");
        FakeTrainer service;
        QVERIFY(service.listen(this));
        service.checkpoints = {FakeTrainer::checkpoint("sdxl.safetensors")};
        MainWindow window;
        window.show();
        auto* client = window.findChild<CanvasServiceController*>();
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(service.server.serverPort())), {}, false);
        client->connectToService();
        QTRY_VERIFY(window.findChild<QToolButton*>("imageTrainLora")->isEnabled());
        trainFrom(window, root.filePath("hero"), "hero", "hero", 100);
        QTRY_COMPARE(service.submissions.size(), 1);
        const QString job = service.submissions.last().value("jobId").toString();
        service.send({{"type", "running"}, {"jobId", job}});
        QTRY_VERIFY(client->loraTrainingActive());
        // Declining keeps Kaimera open and the training running.
        QString asked;
        driveNext<Ardoise::Dialog>(&window, [&](Ardoise::Dialog* dialog) {
            asked = dialog->findChild<QLabel*>("closeDuringTrainingText")->text();
            dialog->findChild<QPushButton*>("keepTraining")->click();
        });
        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        QVERIFY(asked.contains("stops the training"));
        QVERIFY(client->jobActive() && client->loraTrainingActive());
        // Confirming closes it.
        driveNext<Ardoise::Dialog>(&window, [&](Ardoise::Dialog* dialog) {
            dialog->findChild<QPushButton*>("stopTraining")->click();
        });
        QVERIFY(window.close());
        QVERIFY(!window.isVisible());
    }

    void closesWithoutQuestionWhenIdle_R11()
    {
        MainWindow window;
        window.show();
        bool asked = false;
        auto* poll = new QTimer(&window);
        connect(poll, &QTimer::timeout, &window, [&] { asked = asked || QApplication::activeModalWidget(); });
        poll->start(5);
        QVERIFY(window.close());
        QVERIFY(!asked);
    }

    void translatesToFrench_R2()
    {
        QTranslator french;
        QVERIFY(french.load(QStringLiteral(":/translations/kaimera_fr.qm")));
        qApp->installTranslator(&french);
        const auto restore = qScopeGuard([&] { qApp->removeTranslator(&french); });
        TrainLoraDialog dialog(twoCheckpoints(), {}, {}, false);
        QCOMPARE(dialog.windowTitle(), QString("Entraîner un LoRA"));
        QCOMPARE(dialog.status(), QString("Choisissez un dossier d’images PNG ou JPEG."));
        QVERIFY(dialog.findChild<QLabel*>("loraBaseModelNote")->text().contains("SDXL"));
        QTemporaryDir root;
        makeFolder(root, "set");
        dialog.setFolder(root.filePath("set"));
        QCOMPARE(dialog.findChild<QLabel*>("loraFolderSummary")->text(), QString("3 images, 1 légende"));
    }

    void cleanupTestCase() { QSettings().clear(); }
};
QTEST_MAIN(LoraTrainingTest)
#include "LoraTrainingTest.moc"
