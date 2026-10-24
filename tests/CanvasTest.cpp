#include "AnimateForm.h"
#include "CanvasServiceController.h"
#include "LocalStyles.h"
#include <QListWidget>
#include <nukleus/Identity.h>
#include <Ardoise/CollapsibleSection.h>
#include <Ardoise/Dialog.h>
using Ardoise::CollapsibleSection;
#include "EditBubble.h"
#include "JobActivityIndicator.h"
#include "KiraSparkle.h"
#include "LegacySettings.h"
#include "SparklePlanner.h"
#include "GenerationHistoryModel.h"
#include "ReferenceImageStrip.h"
#include "ResolutionPicker.h"
#include "ShotPicker.h"
#include "ShotPrompts.h"
#include "GenerationHistoryWidget.h"
#include <QDesktopServices>
#include <QFileDialog>
#include <QContextMenuEvent>
#include <QTimer>
#include "GenerationHistoryView.h"
#include "GenerationInspectorWidget.h"
#include "GenerationParametersWidget.h"
#include "GenerationPreviewWidget.h"
#include "GenerationWorkspaceWidget.h"
#include "MainWindow.h"
#include "GenerationCard.h"
#include "IdentityPanel.h"
#include "IdentityPrompts.h"
#include "OutputSettingsWidget.h"
#include "PromptEditorWidget.h"
#include "QualityStepSlider.h"
#include "ServicePreferencesDialog.h"

#include <Ardoise/PlainTextEdit.h>
#include <Ardoise/Theme.h>
#include <Ardoise/TitleBar.h>
#include <QAbstractItemModelTester>
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProgressBar>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QAction>
#include <QClipboard>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QScopeGuard>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyle>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QTranslator>
#include <QVBoxLayout>
#include <QtTest>

class CanvasTest final : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settingsDirectory;
    QTranslator m_englishTranslator;
private:
    QUrl m_externalImage;
    // Writes a two-view identity package and returns its path inside directory.
    static QString writeIdentity(const QTemporaryDir& directory, const QList<QImage>& references,
        const QString& generationPrompt, const QString& editPrompt)
    {
        Nukleus::IdentityPackage identity;
        for (const auto& image : references) {
            QByteArray png; QBuffer buffer(&png);
            if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
            identity.references.append(png);
        }
        identity.generationPrompt = generationPrompt;
        identity.editPrompt = editPrompt;
        const QString path = directory.filePath("character.kiraid");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(Nukleus::packIdentity(identity)) <= 0) return {};
        return path;
    }
    // Accepts path in the next Qt (non-native) file dialog opened under parent.
    static void selectInNextFileDialog(QWidget* parent, const QString& path)
    {
        QTimer::singleShot(0, parent, [parent, path] {
            auto* chooser = parent->findChild<QFileDialog*>();
            QVERIFY(chooser);
            chooser->selectFile(path);
            QMetaObject::invokeMethod(chooser, "accept", Qt::QueuedConnection);
        });
    }
private slots:
    void identityPromptClauses()
    {
        using IdentityPrompts::appendClause;
        QCOMPARE(appendClause("", "Use the references."), QString("Use the references."));
        QCOMPARE(appendClause("  make it brighter , ,  ", "Use the references."),
            QString("make it brighter, Use the references."));
        QCOMPARE(appendClause("make it brighter, use the references.", "Use the references."),
            QString("make it brighter, use the references."));
        QCOMPARE(appendClause("make it brighter", "  "), QString("make it brighter"));
    }

    void llmPreferenceFollowsCatalogue()
    {
        QWebSocketServer server("enhancer", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QWebSocket* peer = nullptr;
        bool explicitContent = false;
        QString displayName = "Identity writer";
        const auto sendModels = [&] {
            peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                {"type", "models.list.result"}, {"categories", QJsonArray{QJsonObject{
                    {"category", "llm-chat"}, {"models", QJsonArray{QJsonObject{
                        {"id", "enhancer"}, {"name", "Enhancer"}, {"explicitContent", explicitContent},
                        {"fileName", "enhancer.gguf"}, {"displayName", displayName},
                        {"recommendedOptions", QJsonObject{{"temperature", 0.2}, {"topP", 0.9}, {"contextLength", 4096}, {"maxTokens", 256}}}}}}}}}}).toJson()));
        };
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this, [&](const QString& text) {
                const auto message = QJsonDocument::fromJson(text.toUtf8()).object();
                if (message.value("type") == "authenticate") {
                    peer->sendTextMessage("{\"type\":\"authenticated\"}");
                    peer->sendTextMessage("{\"type\":\"capabilities\",\"state\":\"ready\",\"operations\":[\"prompt.enhance\"]}");
                    sendModels();
                }
            });
        });
        CanvasServiceController service;
        service.setDefaultLlmModel({});
        service.setAllowsExplicitModels(false);
        service.setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        service.connectToService();
        QTRY_COMPARE(service.models(Nukleus::ModelCategory::LlmChat).size(), 1);
        {
            ServicePreferencesDialog preferences(&service);
            auto* combo = preferences.findChild<QComboBox*>("defaultLlmModel");
            QCOMPARE(combo->count(), 2);
            QCOMPARE(combo->itemText(1), QString("Identity writer"));
            combo->setCurrentIndex(1);
            preferences.reject();
            QVERIFY(service.defaultLlmModel().isEmpty());
        }
        {
            ServicePreferencesDialog preferences(&service);
            preferences.findChild<QComboBox*>("defaultLlmModel")->setCurrentIndex(1);
            preferences.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
            QCOMPARE(service.defaultLlmModel(), QString("enhancer"));
        }
        service.setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        service.connectToService();
        QTRY_COMPARE(service.models(Nukleus::ModelCategory::LlmChat).size(), 1);
        displayName.clear(); sendModels();
        QTRY_VERIFY(service.models(Nukleus::ModelCategory::LlmChat).first().displayName.isEmpty());
        {
            ServicePreferencesDialog preferences(&service);
            QCOMPARE(preferences.findChild<QComboBox*>("defaultLlmModel")->itemText(1), QString("enhancer.gguf"));
        }
        explicitContent = true; sendModels();
        QTRY_VERIFY(service.models(Nukleus::ModelCategory::LlmChat).first().explicitContent);
        {
            // Explicit-content classification no longer filters models.
            ServicePreferencesDialog preferences(&service);
            QCOMPARE(preferences.findChild<QComboBox*>("defaultLlmModel")->count(), 2);
        }
        QCOMPARE(service.defaultLlmModel(), QString("enhancer"));
        service.setDefaultLlmModel({});
    }

    void promptIdentityGeneratesStandalonePortraitThroughSdk_data()
    {
        QTest::addColumn<int>("viewCount");
        QTest::newRow("frontal") << 1;
        QTest::newRow("frontal-profile") << 2;
        QTest::newRow("four-views") << 4;
    }

    void promptIdentityGeneratesStandalonePortraitThroughSdk()
    {
        QFETCH(int, viewCount);
        QWebSocketServer server("prompt-identity", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QWebSocket* peer = nullptr;
        QJsonObject submission;
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this, [&](const QString& text) {
                const auto message = QJsonDocument::fromJson(text.toUtf8()).object();
                if (message.value("type") == "authenticate") {
                    peer->sendTextMessage("{\"type\":\"authenticated\"}");
                    peer->sendTextMessage("{\"type\":\"capabilities\",\"state\":\"ready\",\"operations\":[\"image.generate\",\"model-routing\"]}");
                    peer->sendTextMessage("{\"type\":\"models.list.result\",\"categories\":[{\"category\":\"image-generation\",\"models\":[{\"id\":\"portrait-model\",\"name\":\"Portrait\",\"recommendedOptions\":{\"width\":1024,\"height\":768,\"steps\":34,\"cfgScale\":4.5}}]}]}");
                    auto routing = Nukleus::Protocol::starterModelRouting();
                    routing.presets[0].positivePrefix = "preset positive";
                    routing.presets[0].negativePrefix = "preset negative";
                    routing.presets[0].model = {"portrait-model", 25, 3.5};
                    routing.presets[0].identityViews = viewCount; // The preset sets the view count.
                    routing.presets[1].identityViews = viewCount == 4 ? 1 : 4;
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                        {"type", "model-routing.get.completed"},
                        {"routing", Nukleus::Protocol::modelRoutingToJson(routing)}}).toJson()));
                } else if (message.value("type") == "submit") submission = message;
            });
        });
        MainWindow window;
        auto* service = window.findChild<CanvasServiceController*>();
        auto* panel = window.findChild<GenerationInspectorWidget*>();
        auto* workspace = window.findChild<GenerationWorkspaceWidget*>();
        panel->findChild<PromptEditorWidget*>("positivePrompt")->setText("curly hair");
        QImage source(32, 32, QImage::Format_RGB32); source.fill(Qt::blue);
        workspace->appendImage(source, "source");
        service->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        service->connectToService();
        QTRY_VERIFY(service->canGenerate("portrait-model"));
        QTRY_VERIFY(service->modelRouting().has_value());
        window.findChild<QAction*>("createPromptIdentityAction")->trigger();
        auto* identity = window.findChild<IdentityPanel*>();
        QVERIFY(identity->isOpen());
        auto* generate = identity->findChild<QPushButton*>("generateIdentityPortrait");
        QVERIFY(!identity->findChild<QSlider*>()); // No local view selector.
        QVERIFY(!generate->isEnabled()); // The face description is required.
        identity->findChild<QPlainTextEdit*>("identityPrompt")->setPlainText("red lipstick, black jacket, gold earrings, adult with green eyes");
        identity->findChild<QPlainTextEdit*>("identityHairPrompt")->setPlainText("curly hair");
        QVERIFY(generate->isEnabled());
        generate->click();
        QTRY_VERIFY(!submission.isEmpty());
        const auto options = submission.value("options").toObject();
        QVERIFY(options.value("prompt").toString().startsWith("one person, adult, nude, bare shoulders, front-facing"));
        QVERIFY(options.value("prompt").toString().endsWith(", curly hair"));
        QVERIFY(!options.value("prompt").toString().contains("lipstick"));
        QVERIFY(!options.value("prompt").toString().contains("jacket"));
        QVERIFY(!options.value("prompt").toString().contains("earrings"));
        QVERIFY(options.value("seed").toInt() >= 0);
        QVERIFY(options.value("prompt").toString().contains("eye-level viewpoint directly in front of the face"));
        QVERIFY(options.value("prompt").toString().contains("head upright and facing forward"));
        QVERIFY(options.value("prompt").toString().contains("entire head including hair and chin visible"));
        QVERIFY(options.value("prompt").toString().contains("top of the head and all hair fully inside the frame"));
        QVERIFY(!options.value("prompt").toString().contains("crown", Qt::CaseInsensitive));
        QVERIFY(!options.value("prompt").toString().contains("camera", Qt::CaseInsensitive));
        QVERIFY(options.value("prompt").toString().contains("clear empty background space above the highest hair"));
        QVERIFY(options.value("prompt").toString().contains("both shoulders and upper chest visible"));
        QVERIFY(options.value("prompt").toString().contains("generous plain background margin above the head and on both sides"));
        QVERIFY(options.value("negativePrompt").toString().contains("hands, fingers, finger to mouth"));
        QCOMPARE(options.value("negativePrompt").toString().section(", hands, fingers", 0, 0), QStringLiteral(
            "head tilt, tilted viewpoint, side view, profile, three-quarter view, looking away, extreme close-up, tight framing, cropped head, cropped hair, cropped crown, hair touching top edge, cropped shoulders, cropped chin, face out of frame, makeup, eyeshadow, eyeliner, lipstick, mascara, blush, foundation, clothing, shirts, jackets, dresses, accessories, jewelry, earrings, necklaces, chokers, piercings, headwear, hats, crowns, tiaras, headdresses, hair ornaments, hair accessories, glasses, sunglasses, masks, face coverings, face straps, tattoos, facial tattoos, body tattoos, face markings, decorative skin markings, face paint, body paint, forehead symbols, forehead gems, facial gems, stickers, horns, antlers, camera, handheld camera, photography equipment, handheld props, camera overlay, viewfinder overlay, recording indicator, REC, recording frame, focus brackets, battery indicator, timestamp, HUD, user interface, border, picture frame, text, watermark, signature, logo"));
        QCOMPARE(options.value("width").toInt(), 768);
        QCOMPARE(options.value("height").toInt(), 1024);
        QCOMPARE(options.value("steps").toInt(), 25);
        QCOMPARE(options.value("cfgScale").toDouble(), 3.5);
        QCOMPARE(options.value("presetId").toString(), QString("photorealism"));
        QVERIFY(!options.contains("qualityId"));
        QVERIFY(!options.contains("skipPromptFlow")); // The service rejects retired options.
        QVERIFY(!identity->findChild<QComboBox*>("generationPreset")->isEnabled());
        QVERIFY(!options.value("hasInput").toBool());
        QVERIFY(identity->awaitingPortrait());
        QVERIFY(!service->canGenerate("portrait-model"));
        QCOMPARE(identity->activity()->title(), viewCount == 1 ? QString("Generating portrait")
                                                               : QString("Generating portrait 1 of %1").arg(viewCount));
        identity->setOpen(false); // Closing never cancels the job.
        QImage portrait(32, 32, QImage::Format_RGB32); portrait.fill(Qt::magenta);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(portrait.save(&buffer, "PNG"));
        peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
            {"type", "result"}, {"jobId", submission.value("jobId")}, {"mimeType", "image/png"}}).toJson()));
        peer->sendBinaryMessage(png);
        QStringList orientations;
        if (viewCount == 4)
            orientations << "three-quarter view facing left" << "three-quarter view facing right";
        if (viewCount >= 2)
            orientations << "profile, from side";
        for (const auto& orientation : orientations) {
            QTRY_VERIFY(submission.value("options").toObject().value("prompt").toString().startsWith(orientation));
            const auto viewOptions = submission.value("options").toObject();
            const auto positive = viewOptions.value("prompt").toString();
            const auto negative = viewOptions.value("negativePrompt").toString();
            QCOMPARE(viewOptions.value("seed"), options.value("seed"));
            QCOMPARE(viewOptions.value("presetId"), options.value("presetId"));
            QCOMPARE(viewOptions.value("steps"), options.value("steps"));
            QCOMPARE(viewOptions.value("width"), options.value("width"));
            QCOMPARE(viewOptions.value("height"), options.value("height"));
            QVERIFY(positive.endsWith(", curly hair"));
            QVERIFY(!positive.contains("camera", Qt::CaseInsensitive));
            QVERIFY(!positive.contains("looking straight ahead"));
            QVERIFY(!positive.contains("symmetrical"));
            QVERIFY(negative.contains("front view"));
            QVERIFY(negative.contains("hands, fingers"));
            QVERIFY(negative.contains("finger to mouth"));
            QVERIFY(identity->awaitingPortrait());
            if (orientation.startsWith("three-quarter")) {
                const auto direction = orientation.endsWith("left") ? QString("left") : QString("right");
                const auto opposite = direction == "left" ? QString("right") : QString("left");
                QVERIFY(positive.contains("45 degrees"));
                QVERIFY(positive.contains("eyes looking diagonally toward image-" + direction));
                QVERIFY(positive.contains("nose tip to the " + direction + " of the face center"));
                QVERIFY(!positive.contains(opposite + " edge of the image"));
                QVERIFY(negative.contains("facing " + opposite));
                QVERIFY(negative.contains("eyes turned toward the viewer"));
                QVERIFY(!negative.contains("three-quarter view"));
            } else {
                QVERIFY(positive.contains("viewpoint exactly 90 degrees from the front"));
                QVERIFY(positive.contains("one visible eye"));
                QVERIFY(positive.contains("strict left-facing side profile of both head and torso"));
                QVERIFY(!positive.contains("three-quarter"));
                QVERIFY(!positive.contains("both shoulders"));
                QVERIFY(!negative.contains("looking away"));
                QVERIFY(negative.contains("both eyes visible"));
            }
            peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                {"type", "result"}, {"jobId", submission.value("jobId")}, {"mimeType", "image/png"}}).toJson()));
            peer->sendBinaryMessage(png);
        }
        QTRY_VERIFY(!identity->awaitingPortrait());
        QVERIFY(identity->isOpen()); // A finished set comes back into view.
        QVERIFY(identity->findChild<QComboBox*>("generationPreset")->isEnabled());
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(identity->findChild<QWidget*>(QString("identityPortrait%1").arg(i))->isHidden(), i >= viewCount);
            QCOMPARE(identity->portrait(i), i < viewCount ? portrait : QImage());
        }
        const auto identityPackage = identity->portraitIdentity();
        QCOMPARE(identityPackage.references.size(), viewCount);
        QCOMPARE(identityPackage.seed, options.value("seed").toInt());
        QCOMPARE(identityPackage.identityPrompt, QString("adult with green eyes"));
        QCOMPARE(identityPackage.hairPrompt, QString("curly hair"));
        QCOMPARE(workspace->currentImage(), source);
        QVERIFY(generate->isEnabled());
        QCOMPARE(generate->text(), QString("Regenerate portraits"));
        QTRY_VERIFY(identity->findChild<QPushButton*>("savePromptIdentity")->isEnabled());
        // A delivered set keeps its views when a preset with another count is selected.
        identity->findChild<QComboBox*>("generationPreset")->setCurrentIndex(1);
        QCOMPARE(identity->portraitIdentity().references.size(), viewCount);
        QVERIFY(identity->findChild<QPushButton*>("savePromptIdentity")->isEnabled());
        QTemporaryDir directory;
        const QString path = directory.filePath("identity.kiraid");
        const bool nativeDialogsDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restoreDialogs = qScopeGuard([&] {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogsDisabled);
        });
        QTimer::singleShot(0, &window, [&] {
            auto* chooser = window.findChild<QFileDialog*>();
            QVERIFY(chooser);
            chooser->selectFile(path);
            QMetaObject::invokeMethod(chooser, "accept", Qt::QueuedConnection);
        });
        const auto lastSubmission = submission;
        identity->findChild<QPushButton*>("savePromptIdentity")->click();
        QFile packageFile(path);
        QVERIFY(packageFile.open(QIODevice::ReadOnly));
        Nukleus::IdentityPackage saved;
        QVERIFY(Nukleus::unpackIdentity(packageFile.readAll(), saved));
        QCOMPARE(saved.references.size(), viewCount);
        QCOMPARE(saved.identityPrompt, identityPackage.identityPrompt);
        QCOMPARE(saved.hairPrompt, identityPackage.hairPrompt);
        QCOMPARE(saved.seed, identityPackage.seed);
        // The manifest tells generation and editing how to use the references.
        QVERIFY(saved.generationPrompt.contains("reference images"));
        QVERIFY(saved.editPrompt.contains("source image"));
        QCOMPARE(submission, lastSubmission); // Export never submits a service job.
    }

    void identityPanelSlidesOutBesideToolbarAndRetainsDraft()
    {
        MainWindow window;
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto* panel = window.findChild<GenerationInspectorWidget*>();
        auto* positive = panel->findChild<PromptEditorWidget*>("positivePrompt");
        positive->setText("a person with curly hair");
        auto* action = window.findChild<QAction*>("createPromptIdentityAction");
        QVERIFY(action);
        auto* identityButton = window.findChild<QToolButton*>("imageCreateIdentity");
        auto* toolbar = window.findChild<QWidget*>("imageEditingToolbar");
        auto* identity = window.findChild<IdentityPanel*>();
        auto* card = window.findChild<GenerationCard*>();
        QVERIFY(identityButton && identityButton->isEnabled() && identityButton->isCheckable());
        QVERIFY(identity && !identity->isOpen() && identity->isHidden());
        // The identity tool sits in its own section under Improve sharpness, before Edit with an identity.
        const auto toolIndex = [&](const char* name) {
            return toolbar->layout()->indexOf(toolbar->findChild<QWidget*>(name));
        };
        QVERIFY(toolIndex("imageSharpen") < toolIndex("imageCreateIdentity") - 1);
        QCOMPARE(toolIndex("imageEditWithIdentity"), toolIndex("imageCreateIdentity") + 1);
        QVERIFY(!card->isActive());
        window.findChild<QToolButton*>("imageNew")->click();
        QVERIFY(card->isActive());
        identityButton->click();
        QVERIFY(identity->isOpen());
        QVERIFY(identityButton->isChecked());
        QVERIFY(!card->isActive()); // The New image card steps aside.
        // It slides out from the toolbar's edge and settles beside it, matching its height.
        const int margin = Ardoise::Theme::tokens().metrics.spacing8;
        QTRY_COMPARE(identity->geometry().left() + margin, toolbar->geometry().right() + 1 + margin);
        QCOMPARE(identity->geometry().top() + margin, toolbar->geometry().top());
        QCOMPARE(identity->height() - 2 * margin, toolbar->height());
        QVERIFY(!identity->findChild<QToolButton*>("identityPromptAi")); // Sparkles improves instead.
        QVERIFY(identity->findChild<QComboBox*>("generationModel")->isHidden());
        QVERIFY(identity->findChild<ResolutionPicker*>()->isHidden());
        QVERIFY(identity->findChild<QSpinBox*>("steps")->isHidden());
        QVERIFY(identity->findChild<QDoubleSpinBox*>("cfgScale")->isHidden());
        QVERIFY(identity->findChild<QSpinBox*>("seed")->parentWidget()->isHidden());
        // Three numbered steps: the style, the face, then the hair.
        const auto top = [&](const char* name) {
            return identity->findChild<QWidget*>(name)->mapTo(identity, QPoint()).y();
        };
        QVERIFY(top("identityStep1") < top("generationPreset"));
        QVERIFY(top("generationPreset") < top("identityStep2"));
        QVERIFY(top("identityStep2") < top("identityPrompt"));
        QVERIFY(top("identityPrompt") < top("identityStep3"));
        QVERIFY(top("identityStep3") < top("identityHairPrompt"));
        QVERIFY(!identity->findChild<QSlider*>()); // The preset sets the view count.
        auto* prompt = identity->findChild<QPlainTextEdit*>("identityPrompt");
        auto* hair = identity->findChild<QPlainTextEdit*>("identityHairPrompt");
        QCOMPARE(QApplication::focusWidget(), prompt);
        QVERIFY(prompt->toPlainText().isEmpty()); // Independent of the workspace draft.
        QVERIFY(!identity->findChild<QPushButton*>("generateIdentityPortrait")->isEnabled()); // Requires a face.
        QVERIFY(!identity->findChild<QPushButton*>("savePromptIdentity")->isEnabled());
        prompt->setPlainText("a different person");
        QCOMPARE(positive->text(), QString("a person with curly hair"));
        QImage portrait(64, 64, QImage::Format_RGB32);
        portrait.fill(Qt::blue);
        identity->setPortrait(portrait);
        identity->setAwaitingPortrait(true);
        QVERIFY(!prompt->isEnabled() && !hair->isEnabled());
        identity->findChild<QToolButton*>("identityPanelClose")->click();
        QVERIFY(!identity->isOpen());
        QVERIFY(!identityButton->isChecked());
        QVERIFY(card->isActive());
        QTRY_VERIFY(identity->isHidden());
        action->trigger();
        QVERIFY(identity->isOpen());
        QVERIFY(identity->awaitingPortrait()); // Closing never cancels the job.
        QCOMPARE(prompt->toPlainText(), QString("a different person"));
        identity->setAwaitingPortrait(false);
        QVERIFY(prompt->isEnabled());
        QCOMPARE(identity->portrait(0), portrait);
        prompt->setFocus();
        QTest::keyClick(prompt, Qt::Key_Escape);
        QVERIFY(!identity->isOpen());
        identityButton->click();
        QVERIFY(identity->isOpen());
        identityButton->click(); // The toolbar button toggles the panel.
        QVERIFY(!identity->isOpen());
        QVERIFY(window.findChild<GenerationWorkspaceWidget*>()->currentImage().isNull());
    }

    void identityPortraitSlotsFollowViewSelection()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.findChild<QAction*>("createPromptIdentityAction")->trigger();
        auto* identity = window.findChild<IdentityPanel*>();
        auto* parameters = identity->findChild<GenerationParametersWidget*>();
        auto* preset = identity->findChild<QComboBox*>("generationPreset");
        auto* area = identity->findChild<QWidget*>("identityPortraits");
        const auto slot = [&](int i) { return identity->findChild<QWidget*>(QString("identityPortrait%1").arg(i)); };
        // Slots glide to their places; let them settle before measuring.
        const auto verifyGrid = [&](int count) {
            QTest::qWait(450);
            QRect bounds;
            for (int i = 0; i < 4; ++i) {
                QCOMPARE(slot(i)->isVisible(), i < count);
                if (i >= count) continue;
                QCOMPARE(slot(i)->size(), slot(0)->size());
                QVERIFY(slot(i)->height() >= slot(i)->width()); // Portrait slots, never landscape.
                for (int j = 0; j < i; ++j) QVERIFY(!slot(i)->geometry().intersects(slot(j)->geometry()));
                bounds |= slot(i)->geometry();
            }
            // The panel is sized so the grid fills the area: no empty band above, below or beside it.
            const QRect rect = area->rect();
            QVERIFY(qAbs(bounds.top() - rect.top()) <= 1 && qAbs(bounds.bottom() - rect.bottom()) <= 1);
            QVERIFY(qAbs(bounds.left() - rect.left()) <= 1 && qAbs(bounds.right() - rect.right()) <= 1);
        };
        // Without routing there is a single view.
        verifyGrid(1);
        Nukleus::ModelInfo model;
        model.id = "portrait"; model.name = "Portrait";
        model.category = Nukleus::ModelCategory::ImageGeneration;
        parameters->setModels({model});
        auto routing = Nukleus::Protocol::starterModelRouting();
        routing.presets[0].model.modelId = model.id;
        routing.presets[0].identityViews = 4;
        routing.presets[1].identityViews = 2;
        parameters->setRouting(routing, true);
        // Before a set exists, the slots preview the selected preset's views.
        QCOMPARE(preset->currentIndex(), 0);
        verifyGrid(4);
        QCOMPARE(slot(1)->accessibleName(), QString("Three-quarter left"));
        QCOMPARE(slot(3)->accessibleName(), QString("Profile"));
        preset->setCurrentIndex(1);
        verifyGrid(2);
        QCOMPARE(slot(0)->accessibleName(), QString("Front"));
        QCOMPARE(slot(1)->accessibleName(), QString("Profile"));
        routing.presets[1].identityViews = 1; // Routing changes apply to the selected preset.
        parameters->setRouting(routing, true);
        verifyGrid(1);
        routing.presets[1].identityViews = 2;
        parameters->setRouting(routing, true);
        verifyGrid(2);
        // A delivered set keeps its views when the selection changes.
        QImage portrait(512, 768, QImage::Format_RGB32);
        portrait.fill(Qt::blue);
        identity->setPortrait(portrait);
        preset->setCurrentIndex(0);
        verifyGrid(2);
        m_externalImage = QUrl();
        QDesktopServices::setUrlHandler("file", this, "captureExternalImage");
        QTest::mouseDClick(slot(0), Qt::LeftButton);
        QDesktopServices::unsetUrlHandler("file");
        QVERIFY(m_externalImage.isLocalFile());
        QCOMPARE(QImage(m_externalImage.toLocalFile()), portrait);
        // Presets map directly to a model; there is no quality control.
        QVERIFY(!parameters->findChild<QComboBox*>("generationQuality"));
        QVERIFY(parameters->routingAvailable());
        QVERIFY(parameters->routingIssue().isEmpty());
        routing.presets[0].model.modelId = "missing";
        parameters->setRouting(routing, true);
        QVERIFY(!parameters->routingAvailable());
        QVERIFY(parameters->routingIssue().contains("unavailable"));
        QCOMPARE(parameters->findChild<QComboBox*>("generationPreset")->toolTip(), parameters->routingIssue());
    }

    void identityPortraitsTranslateDescriptionOnce()
    {
        const auto originalLocale = QLocale();
        const auto restoreLocale = qScopeGuard([&] { QLocale::setDefault(originalLocale); });
        QLocale::setDefault(QLocale("en_US"));
        QWebSocketServer server("identity-translation", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QWebSocket* peer = nullptr;
        QList<QJsonObject> submissions;
        auto routing = Nukleus::Protocol::starterModelRouting();
        for (auto& preset : routing.presets) preset.model.modelId = "image-model";
        routing.presets[0].identityViews = 2; // Front and profile.
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this, [&](const QString& text) {
                const auto request = QJsonDocument::fromJson(text.toUtf8()).object();
                if (request.value("type") == "authenticate") {
                    peer->sendTextMessage("{\"type\":\"authenticated\"}");
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "capabilities"}, {"state", "ready"},
                        {"operations", QJsonArray{"image.generate", "model-routing", "prompt.enhance"}}}).toJson()));
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                        {"type", "model-routing.get.completed"}, {"routing", Nukleus::Protocol::modelRoutingToJson(routing)}}).toJson()));
                }
                if (request.value("type") == "models.list")
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "models.list.result"},
                        {"categories", QJsonArray{
                            QJsonObject{{"category", "image-generation"},
                                {"models", QJsonArray{QJsonObject{{"id", "image-model"}, {"displayName", "Image"}}}}},
                            QJsonObject{{"category", "llm-chat"},
                                {"models", QJsonArray{QJsonObject{{"id", "llm-chat:chat"}, {"displayName", "Chat"}},
                                    QJsonObject{{"id", "llm-chat:translator"}, {"displayName", "Translator"},
                                        {"recommendedOptions", QJsonObject{{"defaultPromptTranslator", true}}}}}}}}}}).toJson()));
                if (request.value("type") == "submit") submissions.append(request);
            });
        });
        MainWindow window;
        auto* client = window.findChild<CanvasServiceController*>();
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        client->connectToService();
        window.findChild<QAction*>("createPromptIdentityAction")->trigger();
        auto* identity = window.findChild<IdentityPanel*>();
        auto* generate = identity->findChild<QPushButton*>("generateIdentityPortrait");
        auto* hair = identity->findChild<QPlainTextEdit*>("identityHairPrompt");
        identity->findChild<QPlainTextEdit*>("identityPrompt")->setPlainText("une femme avec des taches de rousseur");
        hair->setPlainText("cheveux roux");
        QTRY_VERIFY(generate->isEnabled());
        QTRY_COMPARE(client->defaultPromptTranslatorModelId(), QString("llm-chat:translator"));
        const auto reply = [&](const QJsonObject& message) {
            peer->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));
        };
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG"));
        const auto deliver = [&](const QJsonObject& request) {
            reply({{"type", "result"}, {"jobId", request.value("jobId")}, {"operation", "image.generate"},
                   {"mimeType", "image/png"}});
            peer->sendBinaryMessage(png);
        };
        // The translator turns the face and hair description into English once, before the first view.
        generate->click();
        QTRY_COMPARE(submissions.size(), 1);
        auto submission = submissions.takeFirst();
        auto options = submission.value("options").toObject();
        QCOMPARE(submission.value("operation").toString(), QString("prompt.enhance"));
        QCOMPARE(options.value("modelId").toString(), QString("llm-chat:translator"));
        QCOMPARE(options.value("prompt").toString(), QString("une femme avec des taches de rousseur, cheveux roux"));
        QVERIFY(options.value("parameters").toObject().value("systemPrompt").toString().contains("into English"));
        QVERIFY(identity->awaitingTranslation());
        QVERIFY(!generate->isEnabled());
        QCOMPARE(identity->activity()->title(), QString("Generating portraits"));
        reply({{"type", "result"}, {"jobId", submission.value("jobId")}, {"operation", "prompt.enhance"},
               {"prompt", "a woman with freckles, red lipstick, red hair"}});
        // Every view reuses the translation and keeps makeup excluded.
        for (int view = 0; view < 2; ++view) {
            QTRY_COMPARE(submissions.size(), 1);
            submission = submissions.takeFirst();
            options = submission.value("options").toObject();
            QCOMPARE(submission.value("operation").toString(), QString("image.generate"));
            QVERIFY(!options.contains("skipPromptFlow"));
            QVERIFY(options.value("prompt").toString().endsWith(", a woman with freckles, red hair"));
            QVERIFY(!options.value("prompt").toString().contains("lipstick"));
            QCOMPARE(identity->activity()->title(), QString("Generating portrait %1 of 2").arg(view + 1));
            deliver(submission);
        }
        QTRY_VERIFY(generate->isEnabled());
        QCOMPARE(identity->portraitIdentity().references.size(), 2);
        QCOMPARE(identity->portraitIdentity().identityPrompt, QString("une femme avec des taches de rousseur"));
        // Regenerating the same description reuses its translation.
        generate->click();
        QTRY_COMPARE(submissions.size(), 1);
        submission = submissions.takeFirst();
        QCOMPARE(submission.value("operation").toString(), QString("image.generate"));
        QVERIFY(submission.value("options").toObject().value("prompt").toString().endsWith("red hair"));
        deliver(submission);
        QTRY_COMPARE(submissions.size(), 1);
        deliver(submissions.takeFirst());
        QTRY_VERIFY(generate->isEnabled());
        // A failed translation reports in the panel's pill and keeps the draft and portraits.
        hair->setPlainText("cheveux noirs courts");
        generate->click();
        QTRY_COMPARE(submissions.size(), 1);
        submission = submissions.takeFirst();
        QCOMPARE(submission.value("operation").toString(), QString("prompt.enhance"));
        reply({{"type", "error"}, {"jobId", submission.value("jobId")}, {"code", "prompt-enhancement-failed"},
               {"message", "LLM failed"}});
        QTRY_COMPARE(identity->activity()->state(), JobActivityIndicator::State::Failed);
        QVERIFY(identity->activity()->status().contains("LLM failed"));
        QVERIFY(!identity->awaitingTranslation());
        QVERIFY(generate->isEnabled());
        QCOMPARE(hair->toPlainText(), QString("cheveux noirs courts"));
        QCOMPARE(identity->portraitIdentity().references.size(), 2);
        QVERIFY(submissions.isEmpty());
    }

    void selectsServicePresetModels()
    {
        GenerationParametersWidget parameters;
        Nukleus::ModelInfo first;
        first.id = "first"; first.displayName = "First";
        first.category = Nukleus::ModelCategory::ImageGeneration;
        first.recommendedOptions = {{"steps", 31}, {"cfgScale", 5.5}};
        auto second = first; second.id = "second"; second.recommendedOptions.insert("steps", 42);
        auto routing = Nukleus::Protocol::starterModelRouting();
        routing.presets[0].positivePrefix = "realism";
        routing.presets[0].model = {first.id, 24, 4.0};
        routing.presets[1].model = {second.id, 30, 2.2575};
        parameters.setModels({first, second});
        parameters.setRouting(routing, true);
        QVERIFY(!parameters.findChild<QComboBox *>("generationQuality"));
        QVERIFY(!parameters.findChild<QSlider *>());
        auto *preset = parameters.findChild<QComboBox *>("generationPreset");
        QCOMPARE(preset->currentData().toString(), QString("photorealism"));
        QCOMPARE(parameters.modelId(), first.id);
        QCOMPARE(parameters.steps(), 24); // The preset's snapshot wins over model recommendations.
        QCOMPARE(parameters.cfgScale(), 4.0);
        QVERIFY(preset->toolTip().contains("realism"));
        parameters.findChild<QSpinBox *>("steps")->setValue(50);
        parameters.setRouting(routing, true); // Same preset retains workspace overrides.
        QCOMPARE(parameters.steps(), 50);
        preset->setCurrentIndex(1);
        QCOMPARE(parameters.modelId(), second.id);
        QCOMPARE(parameters.steps(), 30);
        QCOMPARE(parameters.generationRequest().presetId, QString("illustration"));
        QCOMPARE(parameters.generationRequest().cfgScale, 2.2575);
        QVERIFY(parameters.generationRequest().styleId.isEmpty());
        preset->setCurrentIndex(0); // Overrides are discarded on preset changes.
        QCOMPARE(parameters.steps(), 24);
        routing.presets[1].model.modelId = first.id;
        parameters.setRouting(routing, true);
        parameters.findChild<QSpinBox *>("steps")->setValue(50);
        preset->setCurrentIndex(1); // Even when both presets share a model.
        QCOMPARE(parameters.steps(), 30);
        routing.presets[1].model.steps = 45;
        parameters.setRouting(routing, true); // Unedited fields follow the refreshed snapshot.
        QCOMPARE(parameters.steps(), 45);
        const auto originalLocale = QLocale();
        const auto restoreLocale = qScopeGuard([&] { QLocale::setDefault(originalLocale); });
        QLocale::setDefault(QLocale("fr_CA"));
        preset->setCurrentIndex(0);
        parameters.setRouting(routing, true);
        QCOMPARE(preset->currentText(), QString("Photoréalisme"));
        parameters.resetDefaults();
        QCOMPARE(preset->currentData().toString(), QString("photorealism"));
        parameters.setModels({second}); // A missing model cannot silently choose another one.
        QVERIFY(!parameters.routingAvailable());
        QVERIFY(parameters.modelId().isEmpty());
        QCOMPARE(parameters.routingIssue(), QString("Assigned model is unavailable"));
        QCOMPARE(preset->toolTip(), parameters.routingIssue());
        parameters.setModels({first, second});
        QVERIFY(parameters.routingAvailable());
        QVERIFY(parameters.routingIssue().isEmpty());
        routing.presets[0].model = {};
        parameters.setRouting(routing, true);
        QCOMPARE(preset->currentData().toString(), QString("photorealism"));
        QVERIFY(!parameters.routingAvailable());
        QVERIFY(parameters.routingIssue().startsWith("Unassigned"));
        parameters.setRoutingLocked(true);
        QVERIFY(!preset->isEnabled());
        parameters.setRouting(routing, true); // A refresh keeps the lock.
        QVERIFY(!preset->isEnabled());
        parameters.setRoutingLocked(false);
        QVERIFY(preset->isEnabled());
        preset->setCurrentIndex(1);
        parameters.setRouting({}, true);
        parameters.setRouting({}, false);
        parameters.setRouting(routing, true);
        QCOMPARE(preset->currentData().toString(), QString("illustration"));
        parameters.setRouting({}, false);
        QVERIFY(!parameters.routingAvailable());
        QVERIFY(preset->currentText().contains("Upgrade"));
    }
    void captureExternalImage(const QUrl& url) { m_externalImage = url; }
    void editRoutingFollowsEditingQualities()
    {
        const auto originalLocale = QLocale();
        const auto restoreLocale = qScopeGuard([&] { QLocale::setDefault(originalLocale); });
        QLocale::setDefault(QLocale("en_US"));
        GenerationWorkspaceWidget workspace;
        auto routing = Nukleus::Protocol::starterModelRouting();
        Nukleus::ModelInfo model;
        model.id = "editing-model"; model.category = Nukleus::ModelCategory::ImageGeneration;
        model.explicitContent = true;
        auto *row = workspace.findChild<QWidget *>("imageEditRow");
        auto *quality = workspace.findChild<QualityStepSlider *>("editQuality");
        QVERIFY(quality);
        workspace.setEditRouting(routing, {model});
        QVERIFY(row->isHidden()); // The bubble only appears over a selected image.
        QCOMPARE(quality->maximum(), 2);
        QCOMPARE(quality->currentLabel(), QString("Balanced")); // The service default starts selected.
        QCOMPARE(workspace.editQualityId(), QString("edit-medium"));
        QImage image(128, 96, QImage::Format_RGB32); image.fill(Qt::blue);
        workspace.appendImage(image, "Source");
        QVERIFY(!row->isHidden()); // Unassigned editing still offers the bubble and explains why.
        workspace.findChild<Ardoise::PlainTextEdit *>("imageEditPrompt")->setPlainText("change sky");
        workspace.setEditEnabled(true);
        auto *button = workspace.findChild<QPushButton *>("imageEditButton");
        QVERIFY(!button->isEnabled());
        QVERIFY(button->toolTip().startsWith("Unassigned"));
        routing.editingQualities[1].model.modelId = model.id;
        workspace.setEditRouting(routing, {});
        QVERIFY(!button->isEnabled());
        QCOMPARE(button->toolTip(), QString("Assigned model is unavailable"));
        workspace.setEditRouting(routing, {model}); // Explicit models are not filtered.
        QVERIFY(button->isEnabled());
        QVERIFY(button->toolTip().isEmpty());
        // Each quality has its own model; an unassigned one disables Edit and explains why.
        quality->setValue(2);
        QCOMPARE(workspace.editQualityId(), QString("edit-high"));
        QVERIFY(!button->isEnabled());
        QVERIFY(button->toolTip().startsWith("Unassigned"));
        routing.editingQualities[2].model.modelId = model.id;
        workspace.setEditRouting(routing, {model}); // Refreshes keep the selected quality.
        QCOMPARE(workspace.editQualityId(), QString("edit-high"));
        QVERIFY(button->isEnabled());
        workspace.setRoutingLocked(true);
        QVERIFY(!quality->isEnabled());
        workspace.setRoutingLocked(false);
        QVERIFY(quality->isEnabled());
        model.recommendedOptions.insert("canEdit", false);
        workspace.setEditRouting(routing, {model});
        QVERIFY(!button->isEnabled());
        workspace.clearSelection();
        QTRY_VERIFY(row->isHidden());
    }
    void imageContextActions()
    {
        GenerationWorkspaceWidget workspace;
        workspace.resize(600, 500);
        QImage first(80, 120, QImage::Format_RGB32);
        first.fill(Qt::red);
        QImage second(120, 80, QImage::Format_RGB32);
        second.fill(Qt::blue);
        workspace.appendImage(first, "First");
        workspace.appendImage(second, "Second");
        workspace.show();
        QCoreApplication::processEvents();
        auto* history = workspace.findChild<GenerationHistoryWidget*>();
        auto* view = history->view();
        QDesktopServices::setUrlHandler("file", this, "captureExternalImage");
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
            view->visualRect(history->model()->index(0)).center());
        QDesktopServices::unsetUrlHandler("file");
        QVERIFY(m_externalImage.isLocalFile());
        QCOMPARE(QImage(m_externalImage.toLocalFile()), first);
        view->setCurrentIndex(history->model()->index(1));
        QTemporaryDir exportDirectory;
        QVERIFY(exportDirectory.isValid());
        const QString exportPath = exportDirectory.filePath("thumbnail.png");
        const bool nativeDialogsDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QTimer::singleShot(0, &workspace, [&]
        {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            QVERIFY(menu);
            QTimer::singleShot(0, &workspace, [&]
            {
                auto* dialog = workspace.findChild<QFileDialog*>();
                QVERIFY(dialog);
                QCOMPARE(dialog->defaultSuffix(), QString("png"));
                dialog->selectFile(exportPath);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            });
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                menu->actionGeometry(menu->actions().at(1)).center());
        });
        const QPoint thumbnailPoint = view->visualRect(history->model()->index(0)).center();
        QContextMenuEvent saveEvent(QContextMenuEvent::Mouse, thumbnailPoint,
            view->viewport()->mapToGlobal(thumbnailPoint));
        QApplication::sendEvent(view->viewport(), &saveEvent);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogsDisabled);
        QCOMPARE(QImage(exportPath), first);
        QCOMPARE(workspace.currentImage(), second);
        const auto deleteFrom = [&](QWidget* viewport, const QPoint& point)
        {
            QTimer::singleShot(0, &workspace, [&]
            {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                QVERIFY(menu);
                QCOMPARE(menu->actions().size(), 3);
                QCOMPARE(menu->actions().at(0)->text(), QString("Open"));
                QCOMPARE(menu->actions().at(1)->text(), QString("Save"));
                QCOMPARE(menu->actions().at(2)->text(), QString("Delete"));
                QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                    menu->actionGeometry(menu->actions().at(2)).center());
            });
            QContextMenuEvent event(QContextMenuEvent::Mouse, point, viewport->mapToGlobal(point));
            QApplication::sendEvent(viewport, &event);
        };
        deleteFrom(view->viewport(), view->visualRect(history->model()->index(0)).center());
        QCOMPARE(history->model()->rowCount(), 1);
        QCOMPARE(workspace.currentImage(), second);
        QDesktopServices::setUrlHandler("file", this, "captureExternalImage");
        QTest::mouseDClick(workspace.preview()->viewport(), Qt::LeftButton);
        QDesktopServices::unsetUrlHandler("file");
        QCOMPARE(QImage(m_externalImage.toLocalFile()), second);
        deleteFrom(workspace.preview()->viewport(), QPoint(20, 20));
        QCOMPARE(history->model()->rowCount(), 0);
        QVERIFY(workspace.currentImage().isNull());
        QVERIFY(workspace.preview()->currentImage().isNull());
    }
    void hoveredThumbnailRemoveButton()
    {
        GenerationWorkspaceWidget workspace;
        workspace.resize(600, 500);
        QImage first(80, 120, QImage::Format_RGB32);
        first.fill(Qt::red);
        QImage second(120, 80, QImage::Format_RGB32);
        second.fill(Qt::blue);
        workspace.appendImage(first, "First");
        workspace.appendImage(second, "Second");
        workspace.show();
        QCoreApplication::processEvents();
        auto* history = workspace.findChild<GenerationHistoryWidget*>();
        auto* view = history->view();
        auto* remove = view->findChild<QToolButton*>(QStringLiteral("historyRemove"));
        QVERIFY(remove);
        QVERIFY(remove->isHidden());
        emit view->entered(history->model()->index(0));
        QVERIFY(remove->isVisible());
        const QRect thumbnail = view->visualRect(history->model()->index(0));
        QVERIFY(thumbnail.contains(remove->geometry()));
        QVERIFY(remove->geometry().center().x() > thumbnail.center().x());
        QVERIFY(remove->geometry().center().y() < thumbnail.center().y());
        remove->click();
        QCOMPARE(history->model()->rowCount(), 1);
        QCOMPARE(workspace.currentImage(), second);
        QCoreApplication::processEvents();
        emit view->entered(history->model()->index(0));
        remove->click();
        QCOMPARE(history->model()->rowCount(), 0);
        QVERIFY(workspace.currentImage().isNull());
        QVERIFY(workspace.preview()->currentImage().isNull());
    }
    void generatedImagesKeepFinalPrompt()
    {
        GenerationWorkspaceWidget workspace;
        workspace.resize(600, 500);
        QImage image(64, 64, QImage::Format_RGB32);
        image.fill(Qt::green);
        workspace.appendImage(image, "Opened");
        workspace.appendImage(image, "Generated image", "a red fox <in snow>", "blurry");
        workspace.show();
        QCoreApplication::processEvents();
        auto* history = workspace.findChild<GenerationHistoryWidget*>();
        auto* model = history->model();
        QCOMPARE(model->index(0).data(Qt::ToolTipRole).toString(), QString("Opened"));
        QVERIFY(model->index(0).data(GenerationHistoryModel::PromptRole).toString().isEmpty());
        const QString tooltip = model->index(1).data(Qt::ToolTipRole).toString();
        QVERIFY(tooltip.contains("a red fox &lt;in snow&gt;"));
        QVERIFY(tooltip.contains("Negative: blurry"));
        QCOMPARE(model->index(1).data(Qt::AccessibleTextRole).toString(), QString("Generated image"));
        const auto menuActions = [&](int row, bool copy) {
            QStringList texts;
            QTimer::singleShot(0, &workspace, [&] {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                QVERIFY(menu);
                for (const auto* action : menu->actions()) texts.append(action->text());
                if (copy) {
                    auto* action = menu->findChild<QAction*>("copyPromptAction");
                    QVERIFY(action);
                    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center());
                } else menu->close();
            });
            auto* view = history->view();
            const QPoint point = view->visualRect(model->index(row)).center();
            QContextMenuEvent event(QContextMenuEvent::Mouse, point, view->viewport()->mapToGlobal(point));
            QApplication::sendEvent(view->viewport(), &event);
            return texts;
        };
        QCOMPARE(menuActions(0, false), (QStringList{"Open", "Save", "Delete"}));
        QGuiApplication::clipboard()->clear();
        QCOMPARE(menuActions(1, true), (QStringList{"Open", "Save", "Copy prompt", "Delete"}));
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("a red fox <in snow>"));
        QCOMPARE(model->rowCount(), 2);
    }
    void initTestCase()
    {
        Ardoise::Theme::apply(*qApp);
        QVERIFY(m_englishTranslator.load(QStringLiteral(":/translations/kaimera_en.qm")));
        qApp->installTranslator(&m_englishTranslator);
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("KaimeraTests"));
        QCoreApplication::setApplicationName(QStringLiteral("GenerationPrototype"));
        QVERIFY(m_settingsDirectory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
    }
    void init()
    {
        QSettings().clear();
        // Windows connect at startup; a closed port keeps them away from a Nukleus daemon running on this
        // machine, whose routing would otherwise leak into the tests.
        QSettings().setValue(QStringLiteral("Service/Endpoint"), QStringLiteral("ws://127.0.0.1:9"));
    }

    void legacySettingsAndDataFileAreCopiedOnce()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings legacy(directory.filePath("legacy.ini"), QSettings::IniFormat);
        legacy.setValue("Output/Format", "jpg");
        legacy.setValue("Service/Endpoint", "ws://10.0.0.2:9384");
        QSettings current(directory.filePath("current.ini"), QSettings::IniFormat);
        migrateLegacySettings(legacy, current);
        QCOMPARE(current.value("Output/Format").toString(), QStringLiteral("jpg"));
        QCOMPARE(current.value("Service/Endpoint").toString(), QStringLiteral("ws://10.0.0.2:9384"));
        legacy.setValue("Output/Format", "png");
        migrateLegacySettings(legacy, current);
        QCOMPARE(current.value("Output/Format").toString(), QStringLiteral("jpg"));
        QCOMPARE(legacy.value("Output/Format").toString(), QStringLiteral("png"));

        const QString legacyData = directory.filePath("Kira Canvas");
        const QString currentData = directory.filePath("Kaimera");
        QVERIFY(QDir().mkpath(legacyData));
        QFile legacyFile(QDir(legacyData).filePath("danbooru.csv"));
        QVERIFY(legacyFile.open(QIODevice::WriteOnly));
        legacyFile.write("1girl,0,100,\n");
        legacyFile.close();
        migrateLegacyDataFile(legacyData, currentData, "danbooru.csv");
        QFile copied(QDir(currentData).filePath("danbooru.csv"));
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(copied.readAll(), QByteArray("1girl,0,100,\n"));
        QVERIFY(legacyFile.exists());
        QVERIFY(copied.remove());
        QFile kept(QDir(currentData).filePath("danbooru.csv"));
        QVERIFY(kept.open(QIODevice::WriteOnly));
        kept.write("kept");
        kept.close();
        migrateLegacyDataFile(legacyData, currentData, "danbooru.csv");
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), QByteArray("kept"));
    }

    void compactEditRowFiltersModelsAndRequiresSelectedImage()
    {
        GenerationWorkspaceWidget workspace;
        workspace.resize(720, 600);
        workspace.show();
        auto* row = workspace.findChild<QWidget*>("imageEditRow");
        auto* prompt = workspace.findChild<Ardoise::PlainTextEdit*>("imageEditPrompt");
        QVERIFY(!workspace.findChild<QComboBox*>("imageEditModel"));
        auto* button = workspace.findChild<QPushButton*>("imageEditButton");
        QVERIFY(row && prompt && button);
        QVERIFY(row->isHidden());
        QVERIFY(!button->isEnabled());
        Nukleus::ModelInfo safe;
        safe.id = "image-generation:safe"; safe.name = "Safe edit"; safe.category = Nukleus::ModelCategory::ImageGeneration;
        auto explicitModel = safe;
        explicitModel.id = "image-generation:explicit"; explicitModel.explicitContent = true;
        workspace.setDefaultModels({safe, explicitModel}, {}, {});
        QVERIFY(row->isHidden());
        safe.recommendedOptions.insert("canEdit", false);
        workspace.setDefaultModels({safe}, safe.id, {});
        QVERIFY(row->isHidden());
        safe.recommendedOptions.insert("canEdit", true);
        workspace.setDefaultModels({safe, explicitModel}, safe.id, {});
        QVERIFY(row->isHidden()); // Editing is offered, but there is no image yet.
        prompt->setPlainText("  ");
        QVERIFY(!button->isEnabled());
        QImage source(40, 40, QImage::Format_RGB32); source.fill(Qt::red);
        workspace.appendImage(source, "Source");
        QVERIFY(!row->isHidden());
        workspace.setEditEnabled(true);
        prompt->setPlainText("Change the sky\nMake it violet");
        QVERIFY(button->isEnabled());
        QCoreApplication::processEvents();
        const int fieldHeight = prompt->height();
        QVERIFY(fieldHeight >= prompt->heightForLines(3)); // Three lines are visible.
        prompt->appendPlainText("Keep the foreground");
        QCOMPARE(prompt->height(), fieldHeight);
        // No gaps: the paperclip/Sparkles column and the Edit button share the field's edges.
        {
            auto* panelBubble = workspace.findChild<EditBubble*>();
            panelBubble->setExpanded(true);
            QTRY_VERIFY(panelBubble->findChild<QToolButton*>("imageEditAttach")->isVisible());
            auto* attach = panelBubble->findChild<QToolButton*>("imageEditAttach");
            auto* improve = panelBubble->findChild<QToolButton*>("imageEditImprove");
            QVERIFY(!panelBubble->findChild<QToolButton*>("imageEditAttachIdentity"));
            const auto edges = [&](QWidget* widget) {
                const QPoint top = widget->mapTo(panelBubble, QPoint());
                return std::pair{top.y(), top.y() + widget->height()};
            };
            QTRY_COMPARE(edges(attach).first, edges(prompt).first);
            QCOMPARE(edges(improve).second, edges(prompt).second);
            QCOMPARE(edges(button), edges(prompt));
            QCOMPARE(attach->width(), attach->height());
            QVERIFY(improve->mapTo(panelBubble, QPoint()).x() == attach->mapTo(panelBubble, QPoint()).x());
            if (qEnvironmentVariableIsSet("KIRA_EDIT_SCREENSHOT")) {
                QTest::qWait(500);
                QVERIFY(panelBubble->grab().save(qEnvironmentVariable("KIRA_EDIT_SCREENSHOT")));
            }
            panelBubble->setExpanded(false);
            QTest::qWait(400); // The bubble starts collapsed; let it settle back into its pill.
        }
        workspace.setDefaultModels({explicitModel, safe}, explicitModel.id, {}); // Explicit models are not filtered.
        QVERIFY(!row->isHidden());
        QImage image(40, 40, QImage::Format_RGB32); image.fill(Qt::red);
        auto* history = workspace.findChild<GenerationHistoryWidget*>();
        QSignalSpy selected(&workspace, &GenerationWorkspaceWidget::imageSelectionChanged);
        QSignalSpy edits(&workspace, &GenerationWorkspaceWidget::editRequested);
        auto* bubble = workspace.findChild<EditBubble*>();
        auto* strip = row->findChild<QWidget*>("imageEditReferences");
        QVERIFY(bubble && strip && strip->isHidden());
        QList<QImage> references;
        for (const auto color : {Qt::green, Qt::blue, Qt::yellow, Qt::cyan, Qt::magenta}) {
            QImage reference(16, 24, QImage::Format_RGB32); reference.fill(color);
            references.append(reference);
        }
        QVERIFY(!bubble->addReference({}));
        for (int i = 0; i < EditBubble::MaximumReferences; ++i)
            QVERIFY(bubble->addReference(references.at(i)));
        QVERIFY(!bubble->addReference(references.at(4)));
        QVERIFY(!strip->isHidden());
        QVERIFY(!bubble->attachButton()->isEnabled());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(strip->findChildren<QToolButton*>("imageEditRemoveReference").size(), 4);
        strip->findChildren<QToolButton*>("imageEditRemoveReference").at(1)->click();
        QCOMPARE(bubble->references(), (QList<QImage>{references.at(0), references.at(2), references.at(3)}));
        QVERIFY(bubble->attachButton()->isEnabled());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(strip->findChildren<QToolButton*>("imageEditRemoveReference").size(), 3);        const QString instructions = prompt->toPlainText();
        button->click();
        QVERIFY(prompt->toPlainText().isEmpty());
        QVERIFY(bubble->references().isEmpty());
        QVERIFY(strip->isHidden());
        QVERIFY(!button->isEnabled());
        QCOMPARE(edits.count(), 1);
        QCOMPARE(edits.first().at(1).toString(), instructions);
        QCOMPARE(edits.first().at(2).value<QList<QImage>>(),
            (QList<QImage>{references.at(0), references.at(2), references.at(3)}));
        QCOMPARE(workspace.currentImage(), image);
        QCOMPARE(history->model()->rowCount(), 1);
        QCOMPARE(selected.count(), 0);
        prompt->setFocus();
        prompt->setPlainText("Change the sky");
        prompt->moveCursor(QTextCursor::End);
        QTest::keyClick(prompt, Qt::Key_Return, Qt::ShiftModifier);
        QTest::keyClicks(prompt, "Make it violet");
        QCOMPARE(prompt->toPlainText(), QString("Change the sky\nMake it violet"));
        QCOMPARE(edits.count(), 1);
        QCOMPARE(prompt->height(), fieldHeight);
        workspace.setEditEnabled(false);
        QTest::keyClick(prompt, Qt::Key_Return);
        QCOMPARE(edits.count(), 1);
        QCOMPARE(prompt->toPlainText(), QString("Change the sky\nMake it violet"));
        workspace.setEditEnabled(true);
        QTest::keyClick(prompt, Qt::Key_Return);
        QCOMPARE(edits.count(), 2);
        QCOMPARE(edits.last().at(1).toString(), QString("Change the sky\nMake it violet"));
        QVERIFY(prompt->toPlainText().isEmpty());
        QTest::keyClick(prompt, Qt::Key_Enter, Qt::KeypadModifier);
        QCOMPARE(edits.count(), 2);
        prompt->setPlainText("Preserve lighting");
        QTest::keyClick(prompt, Qt::Key_Enter, Qt::KeypadModifier);
        QCOMPARE(edits.count(), 3);
        QVERIFY(prompt->toPlainText().isEmpty());
        // The bubble floats over the bottom of the preview, inside its bounds.
        QCOMPARE(row->parentWidget(), workspace.preview());
        QTRY_VERIFY(workspace.preview()->rect().contains(row->geometry()));
        QVERIFY(row->geometry().bottom() > workspace.preview()->height() / 2);
        auto* expand = row->findChild<QToolButton*>("imageEditExpand");
        QVERIFY(expand && !expand->isHidden()); // Collapsed to a pill by default.
        QVERIFY(!prompt->isVisibleTo(row));
        // The centred pill leaves the dimensions badge in the bottom-left corner, away from
        // Kira Sparkle's bubble on the right.
        auto* sparkleBubble = workspace.findChild<QToolButton*>("kiraSparkleBubble");
        QTRY_VERIFY(sparkleBubble->isVisible());
        QTRY_COMPARE(workspace.preview()->overlayRect().bottom(), workspace.preview()->viewport()->height() - 9);
        QCOMPARE(workspace.preview()->overlayRect().left(), 8);
        QVERIFY(workspace.preview()->overlayRect().right()
            < sparkleBubble->mapTo(workspace.preview()->viewport(), QPoint()).x());
        expand->click();
        QTRY_VERIFY(expand->isHidden() && prompt->isVisibleTo(row));
        // The full-width editor overlaps the corner, so the badge rises above it.
        QTRY_VERIFY(workspace.preview()->overlayRect().bottom() < row->geometry().top());
        QTRY_VERIFY(prompt->hasFocus());
        QVERIFY(prompt->height() >= prompt->heightForLines(3)); // Three visible lines.
        QVERIFY(prompt->height() < prompt->heightForLines(4));
        QCOMPARE(button->height(), prompt->height());
        prompt->setPlainText("Warmer light");
        QTest::keyClick(prompt, Qt::Key_Return);
        QCOMPARE(edits.count(), 4);
        QVERIFY(!bubble->isExpanded()); // Submitting returns to the pill.
        QTRY_VERIFY(!expand->isHidden() && !prompt->isVisibleTo(row));
        expand->click();
        QTRY_VERIFY(expand->isHidden() && prompt->isVisibleTo(row));
        row->findChild<QToolButton*>("imageEditCollapse")->click();
        QTRY_VERIFY(!expand->isHidden() && !prompt->isVisibleTo(row));
        workspace.setDefaultModels({}, safe.id, {});
        QTRY_VERIFY(row->isHidden());
        QVERIFY(!button->isEnabled());
    }

    void kiraSparkleBubbleSendsRequestsWithAttachments()
    {
        GenerationWorkspaceWidget workspace;
        workspace.resize(720, 600);
        workspace.show();
        auto* sparkle = workspace.findChild<KiraSparkle*>();
        QVERIFY(sparkle);
        auto* bubble = sparkle->findChild<QToolButton*>("kiraSparkleBubble");
        auto* prompt = sparkle->prompt();
        auto* send = sparkle->sendButton();
        // Collapsed to a bubble in the preview's bottom-right corner.
        QTRY_VERIFY(sparkle->isVisible() && bubble->isVisible());
        QCOMPARE(sparkle->parentWidget(), workspace.preview());
        const auto bubbleRect = [&] {
            return QRect(bubble->mapTo(workspace.preview(), QPoint()), bubble->size());
        };
        QTRY_VERIFY(bubbleRect().left() > workspace.preview()->width() * 3 / 4);
        QVERIFY(bubbleRect().bottom() > workspace.preview()->height() / 2);
        QVERIFY(!prompt->isVisibleTo(sparkle));
        QVERIFY(!sparkle->findChild<QToolButton*>("kiraSparkleCollapse"));
        bubble->click();
        // The panel opens to the left of the bubble, which stays visible.
        QTRY_VERIFY(prompt->isVisibleTo(sparkle));
        QTRY_VERIFY(prompt->hasFocus());
        QVERIFY(bubble->isVisible());
        auto* panel = prompt->parentWidget();
        QTRY_VERIFY(QRect(panel->mapTo(workspace.preview(), QPoint()), panel->size()).right() < bubbleRect().left());
        QVERIFY(workspace.preview()->rect().contains(sparkle->geometry()));
        // The chat takes the viewport's full height and follows its resizes.
        const auto panelTop = [&] {
            return panel->mapTo(workspace.preview(), QPoint()).y() - workspace.preview()->viewport()->geometry().top();
        };
        QTRY_COMPARE(panelTop(), 16);
        workspace.resize(720, 800);
        QTRY_COMPARE(panelTop(), 16);
        QTRY_VERIFY(panel->height() > 500);
        workspace.resize(720, 600);
        QTRY_COMPARE(panelTop(), 16);
        // A chat: the greeting, a two-line field and the paperclip left of Send under it.
        const auto messages = [&] { return sparkle->findChildren<QLabel*>("kiraSparkleMessage"); };
        QCOMPARE(messages().size(), 1);
        QVERIFY(!messages().first()->property("fromUser").toBool());
        QCOMPARE(prompt->height(), prompt->heightForLines(2));
        QTRY_COMPARE(prompt->width(), sparkle->findChild<QScrollArea*>("kiraSparkleTranscript")->width());
        QVERIFY(send->text().isEmpty());
        QVERIFY(send->geometry().top() > prompt->geometry().bottom());
        QVERIFY(sparkle->attachButton()->geometry().right() < send->geometry().left());
        QCOMPARE(sparkle->attachButton()->geometry().left(), prompt->geometry().left());
        QCOMPARE(send->geometry().right(), prompt->geometry().right());
        // The bubble toggles the panel closed and open again.
        bubble->click();
        QVERIFY(!sparkle->isExpanded());
        bubble->click();
        QVERIFY(sparkle->isExpanded());
        // A click outside Kira Sparkle collapses it.
        QTest::mouseClick(workspace.preview()->viewport(), Qt::LeftButton, {}, QPoint(10, 10));
        QVERIFY(!sparkle->isExpanded());
        bubble->click();
        QTRY_VERIFY(prompt->hasFocus());
        QVERIFY(!send->isEnabled());

        QSignalSpy requests(&workspace, &GenerationWorkspaceWidget::sparkleRequested);
        QImage attachment(16, 16, QImage::Format_RGB32); attachment.fill(Qt::green);
        // The buttons are as tall as a thumbnail, so attaching never moves the toolbar.
        QCOMPARE(send->size(), QSize(ReferenceImageStrip::ThumbnailSize, ReferenceImageStrip::ThumbnailSize));
        QCOMPARE(sparkle->attachButton()->size(), send->size());
        const QRect sendBefore = send->geometry();
        const QRect promptBefore = prompt->geometry();
        // A base image (unnumbered, before the references) and up to four references.
        auto* engine = static_cast<QWidget*>(sparkle->engineButton());
        const QImage engineCreating = engine->grab().toImage();
        QImage base(24, 16, QImage::Format_RGB32); base.fill(Qt::blue);
        sparkle->setBaseImage(base);
        QCOMPARE(sparkle->baseImage(), base);
        QVERIFY(engine->grab().toImage() != engineCreating); // The quality, not the preset, now applies.
        for (int i = 0; i < ReferenceImageStrip::MaximumReferences; ++i)
            QVERIFY(sparkle->addAttachment(attachment));
        QVERIFY(!sparkle->addAttachment(attachment));
        QTest::qWait(50);
        QCOMPARE(send->geometry(), sendBefore);
        QCOMPARE(prompt->geometry(), promptBefore);
        // The base and the four references fit beside the engine button.
        workspace.resize(900, 600);
        QTest::qWait(50);
        auto* baseStrip = sparkle->baseStrip();
        auto* attachmentStrip = sparkle->referenceStrip();
        QVERIFY(baseStrip->geometry().right() < attachmentStrip->geometry().left());
        QVERIFY(attachmentStrip->geometry().left() + attachmentStrip->sizeHint().width()
            <= engine->geometry().left());
        workspace.resize(720, 600);
        // Another base image replaces the current one.
        QImage otherBase(16, 24, QImage::Format_RGB32); otherBase.fill(Qt::darkBlue);
        sparkle->setBaseImage(otherBase);
        QCOMPARE(baseStrip->references(), QList<QImage>{otherBase});
        sparkle->setBaseImage(base);
        QTest::keyClick(prompt, Qt::Key_Return); // Blank requests are not sent.
        QCOMPARE(requests.count(), 0);
        QTest::keyClicks(prompt, "A cat");
        QTest::keyClick(prompt, Qt::Key_Return, Qt::ShiftModifier);
        QTest::keyClicks(prompt, "on a roof");
        QCOMPARE(prompt->toPlainText(), QString("A cat\non a roof"));
        sparkle->setSendEnabled(false);
        QTest::keyClick(prompt, Qt::Key_Return);
        QCOMPARE(requests.count(), 0);
        QCOMPARE(prompt->toPlainText(), QString("A cat\non a roof"));
        sparkle->setSendEnabled(true);
        QTest::keyClick(prompt, Qt::Key_Enter, Qt::KeypadModifier);
        QCOMPARE(requests.count(), 1);
        QCOMPARE(requests.first().at(0).toString(), QString("A cat\non a roof"));
        QCOMPARE(requests.first().at(1).value<QImage>(), base);
        QCOMPARE(requests.first().at(2).value<QList<QImage>>().size(), ReferenceImageStrip::MaximumReferences);
        QVERIFY(prompt->toPlainText().isEmpty());
        // The attachments belong to the conversation: they stay beside the attach button
        // and the message shows only its text.
        QCOMPARE(sparkle->baseImage(), base);
        QCOMPARE(sparkle->attachments().size(), ReferenceImageStrip::MaximumReferences);
        QCOMPARE(sparkle->findChildren<ReferenceImageStrip*>().size(), 2);
        // A new chat empties the conversation and its attachments.
        sparkle->findChild<QToolButton*>("kiraSparkleNewChat")->click();
        QVERIFY(sparkle->attachments().isEmpty());
        QVERIFY(sparkle->baseImage().isNull());
        QCOMPARE(messages().size(), 1);
        prompt->setPlainText("A cat\non a roof");
        QTest::keyClick(prompt, Qt::Key_Return);
        QCOMPARE(requests.count(), 2);
        QVERIFY(requests.last().at(1).value<QImage>().isNull());
        // The attach button's menu: the base image, then the references.
        const auto attachMenu = [&](const QString& text) {
            QStringList offered;
            QTimer::singleShot(0, this, [&offered, text] {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if (!menu) return;
                for (auto* action : menu->actions()) {
                    offered.append(action->text() + (action->isEnabled() ? QString() : QStringLiteral(" (off)")));
                    if (action->text() == text && action->isEnabled()) action->trigger();
                }
                menu->close();
            });
            sparkle->attachButton()->click();
            return offered;
        };
        QCOMPARE(attachMenu({}), (QStringList{"Add a base image…", "Use the displayed image as base (off)", "",
                                      "Add reference images…", "Add the displayed image as reference (off)",
                                      "Add an identity as reference…"}));
        QImage displayed(30, 20, QImage::Format_RGB32); displayed.fill(Qt::red);
        workspace.appendImage(displayed, "Displayed", {});
        attachMenu("Use the displayed image as base");
        QCOMPARE(sparkle->baseImage(), displayed);
        QVERIFY(sparkle->attachments().isEmpty());
        attachMenu("Add the displayed image as reference");
        QCOMPARE(sparkle->attachments(), QList<QImage>{displayed});
        // An addition that would go beyond four references (an identity's four views with
        // one attached) is rejected as a whole; the base image does not count.
        auto* strip = sparkle->referenceStrip();
        QVERIFY(!strip->addReferences({attachment, attachment, attachment, attachment}));
        QCOMPARE(strip->references().size(), 1);
        QVERIFY(strip->addReferences({attachment, attachment, attachment}));
        // Full: the reference choices are off, the base ones stay available.
        QCOMPARE(attachMenu({}), (QStringList{"Add a base image…", "Use the displayed image as base", "",
                                      "Add reference images… (off)", "Add the displayed image as reference (off)",
                                      "Add an identity as reference… (off)"}));
        strip->clearReferences();
        sparkle->setBaseImage({});
        QVERIFY(sparkle->baseImage().isNull());
        // The request joins the conversation and the panel stays open for the answer.
        QVERIFY(sparkle->isExpanded());
        QCOMPARE(messages().size(), 2);
        QCOMPARE(messages().last()->text(), QString("A cat\non a roof"));
        QVERIFY(messages().last()->property("fromUser").toBool());
        sparkle->addReply("I'm creating a new image.");
        QCOMPARE(messages().size(), 3);
        QVERIFY(!messages().last()->property("fromUser").toBool());

        // Opening the edit bubble collapses Kira Sparkle, and vice versa.
        auto* editBubble = workspace.findChild<EditBubble*>();
        sparkle->setExpanded(true);
        editBubble->setExpanded(true);
        QVERIFY(!sparkle->isExpanded());
        sparkle->setExpanded(true);
        QVERIFY(!editBubble->isExpanded());
        QTest::qWait(400);
        if (qEnvironmentVariableIsSet("KIRA_SPARKLE_SCREENSHOT")) {
            prompt->setPlainText(QStringLiteral("Make the sky violet"));
            sparkle->addAttachment(attachment);
            const QUuid pending = QUuid::createUuid(), done = QUuid::createUuid();
            for (const auto& id : {pending, done}) {
                workspace.addPendingImage(id, "Generated image", "Submitting…");
                workspace.showSparkleResult(id, QSize(1216, 832));
            }
            workspace.setPendingProgress(pending, "Sampling", 40);
            QImage result(1216, 832, QImage::Format_RGB32); result.fill(Qt::darkCyan);
            workspace.completePendingImage(done, result);
            sparkle->setThinking("Understanding your request…");
            workspace.resize(900, 900);
            QTest::qWait(300);
            QVERIFY(workspace.grab().save(qEnvironmentVariable("KIRA_SPARKLE_SCREENSHOT")));
            // The pending result, scrolled into view.
            auto* transcript = sparkle->findChild<QScrollArea*>("kiraSparkleTranscript");
            transcript->ensureWidgetVisible(sparkle->findChildren<QWidget*>("kiraSparkleResult").first());
            QTest::qWait(100);
            QVERIFY(workspace.grab().save(qEnvironmentVariable("KIRA_SPARKLE_SCREENSHOT").replace(".png", "-pending.png")));
            // The sent request with its attachments.
            transcript->verticalScrollBar()->setValue(0);
            QTest::qWait(100);
            QVERIFY(workspace.grab().save(qEnvironmentVariable("KIRA_SPARKLE_SCREENSHOT").replace(".png", "-message.png")));
            // The engine button and its menu, with an unavailable preset.
            sparkle->setPresets({{"photo", "Photorealism", {}}, {"illustration", "Illustration", {}},
                                    {"anime", "Anime", "Assigned model is unavailable"}}, "photo");
            sparkle->setQualities({{"fast", "Fast", {}}, {"balanced", "Balanced", {}}, {"quality", "Quality", {}}},
                "balanced");
            for (int i = 0; i < 4; ++i) sparkle->addAttachment(attachment);
            QImage screenshotBase(60, 40, QImage::Format_RGB32); screenshotBase.fill(Qt::darkCyan);
            sparkle->setBaseImage(screenshotBase);
            QTest::qWait(100);
            QVERIFY(workspace.grab().save(qEnvironmentVariable("KIRA_SPARKLE_SCREENSHOT").replace(".png", "-engine.png")));
            QTimer::singleShot(200, this, [] {
                auto* menu = QApplication::activePopupWidget();
                if (!menu) return;
                menu->grab().save(qEnvironmentVariable("KIRA_SPARKLE_SCREENSHOT").replace(".png", "-menu.png"));
                menu->close();
            });
            sparkle->engineButton()->click();
            sparkle->referenceStrip()->clearReferences();
            QTimer::singleShot(200, this, [] {
                auto* menu = QApplication::activePopupWidget();
                if (!menu) return;
                menu->grab().save(qEnvironmentVariable("KIRA_SPARKLE_SCREENSHOT").replace(".png", "-attach.png"));
                menu->close();
            });
            sparkle->attachButton()->click();
        }
        prompt->setFocus();
        QTest::keyClick(prompt, Qt::Key_Escape);
        QVERIFY(!sparkle->isExpanded());
        QTRY_VERIFY(bubble->isVisible());
        if (qEnvironmentVariableIsSet("KIRA_SPARKLE_SCREENSHOT")) {
            // The bubble at rest, then hovered.
            const auto grabBubble = [&](const QString& suffix) {
                const QRect area = sparkle->geometry().intersected(workspace.preview()->rect());
                QVERIFY(workspace.preview()->grab(area).save(
                    qEnvironmentVariable("KIRA_SPARKLE_SCREENSHOT").replace(".png", suffix + ".png")));
            };
            workspace.setFocus();
            QTest::qWait(500);
            grabBubble("-bubble");
            QEnterEvent enter(QPointF(4, 4), QPointF(4, 4), QPointF(4, 4));
            QApplication::sendEvent(bubble, &enter);
            QTest::qWait(700);
            grabBubble("-bubble-hover");
        }
    }

    void sparklePlannerOffersOnlyPossibleActions()
    {
        using SparklePlanner::Plan;
        // One schema branch per allowed action, each with only its own fields, action first.
        const auto branches = [](const SparklePlanner::Context& context) {
            QMap<QString, QStringList> fields;
            for (const auto& branch : SparklePlanner::schema(context).value("anyOf").toArray()) {
                const auto object = branch.toObject();
                QStringList required;
                for (const auto& key : object.value("required").toArray()) required.append(key.toString());
                fields.insert(object.value("properties").toObject().value("action").toObject().value("const").toString(),
                    required);
            }
            return fields;
        };
        SparklePlanner::Context context;
        // The user chooses the preset and the editing quality; the plan never does.
        context.preset = SparklePlanner::Preset{"anime", "Anime", "anime style", PromptInstructions::Format::Tags};
        context.qualityId = "balanced";
        context.canLook = true;
        context.language = PromptInstructions::Language::French;
        // Without any image, nothing can be looked at; editing is still offered so that a request
        // to change an image is recognized, and Canvas then asks for a base image.
        auto fields = branches(context);
        QCOMPARE(fields.keys(), (QStringList{"ask", "edit", "generate", "prompt", "reply"}));
        QVERIFY(!context.hasEditTarget());
        QCOMPARE(fields.value("reply"), (QStringList{"action", "message"}));
        QCOMPARE(fields.value("ask"), (QStringList{"action", "question"}));
        QCOMPARE(fields.value("prompt"), (QStringList{"action", "message", "prompt", "negativePrompt"}));
        QCOMPARE(fields.value("generate"), (QStringList{"action", "shot", "ratio", "description", "avoid", "prompt",
                                                "negativePrompt"}));
        auto instructions = SparklePlanner::instructions(context);
        QVERIFY(instructions.contains("The answer is a proposal"));
        QVERIFY(instructions.contains("description: what the image will show, for the user, in two or three "
                                      "natural sentences in French"));
        QVERIFY(!instructions.contains("Your last answer is a proposal"));
        // While a creation proposal waits, a change to it is a new proposal, never an edit.
        auto proposed = context;
        proposed.proposal = true;
        QCOMPARE(branches(proposed).keys(), (QStringList{"ask", "generate", "prompt", "reply"}));
        QVERIFY(SparklePlanner::instructions(proposed).contains("Your last answer is a proposal the user has not "
                                                                "accepted"));
        // Acceptances in words carry it out like its button, without the LLM; changes do not.
        for (const auto& accepted : {"oui", "Oui, vas-y !", "ok go", "Parfait merci", "c’est bon", "yes please",
                 "ça me va, crée-la", "do it"})
            QVERIFY2(SparklePlanner::isAcceptance(QString::fromUtf8(accepted)), accepted);
        for (const auto& changed : {"oui mais de nuit", "merci", "non", "non, plutôt un chien", ""})
            QVERIFY2(!SparklePlanner::isAcceptance(QString::fromUtf8(changed)), changed);
        QCOMPARE(SparklePlanner::uniqueTerms(" helmet, Helmet ,,crowd "), QString("helmet, crowd"));
        QVERIFY(instructions.contains("No image is displayed."));
        QVERIFY(instructions.contains("No image is attached."));
        QVERIFY(instructions.contains("the preset the user chose, \"Anime\" (style: anime style)"));
        QVERIFY(instructions.contains("the complete prompt written as Danbooru tags, always in English"));
        QVERIFY(instructions.contains("it is never an edit"));
        QVERIFY(instructions.contains("short follow-up question in French"));
        QVERIFY(instructions.contains("No image can be changed yet"));
        QVERIFY(!instructions.contains("\"look\""));
        QVERIFY(!SparklePlanner::parse({{"action", "look"}, {"images", QJsonArray{"displayed"}},
            {"describerQuestion", "?"}}, context));
        // Texts for the user are kept as written; blank ones fail.
        auto plan = SparklePlanner::parse({{"action", "reply"}, {"message", " Bonjour ! "}}, context);
        QVERIFY(plan); QCOMPARE(plan->action, Plan::Action::Reply); QCOMPARE(plan->message, QString("Bonjour !"));
        plan = SparklePlanner::parse({{"action", "ask"}, {"question", "Quel sujet ?"}}, context);
        QVERIFY(plan); QCOMPARE(plan->action, Plan::Action::Ask); QCOMPARE(plan->question, QString("Quel sujet ?"));
        QString error;
        QVERIFY(!SparklePlanner::parse({{"action", "ask"}, {"question", " "}}, context, &error));
        QVERIFY(error.contains("empty answer"));
        plan = SparklePlanner::parse({{"action", "prompt"}, {"message", "Voici :"}, {"prompt", "1girl, dragon"},
            {"negativePrompt", ""}}, context);
        QVERIFY(plan); QCOMPARE(plan->action, Plan::Action::Prompt); QCOMPARE(plan->prompt, QString("1girl, dragon"));
        // Unknown choices fall back to Free and the current ratio.
        plan = SparklePlanner::parse({{"action", "generate"}, {"shot", "sideways"},
            {"ratio", "7:5"}, {"prompt", " a fox "}, {"negativePrompt", ""}}, context);
        QVERIFY(plan);
        QCOMPARE(plan->action, Plan::Action::Generate);
        QCOMPARE(plan->shot, ShotPicker::Shot::Free);
        QVERIFY(plan->ratio.isEmpty());
        QCOMPARE(plan->prompt, QString("a fox"));
        QVERIFY(!SparklePlanner::parse({{"action", "generate"}, {"prompt", " "}}, context, &error));
        QVERIFY(error.contains("no image prompt"));
        // A displayed image can be edited with the user's quality and shown to the describer.
        context.imageSize = QSize(640, 480);
        context.imagePrompt = "a red barn";
        fields = branches(context);
        QCOMPARE(fields.keys(), (QStringList{"ask", "edit", "generate", "look", "prompt", "reply"}));
        QCOMPARE(fields.value("edit"), (QStringList{"action", "instruction"}));
        QCOMPARE(fields.value("look"), (QStringList{"action", "images", "describerQuestion"}));
        instructions = SparklePlanner::instructions(context);
        QVERIFY(instructions.contains("640 × 480"));
        QVERIFY(instructions.contains("generated from this prompt: a red barn"));
        QVERIFY(instructions.contains("The edit changes the displayed image."));
        QVERIFY(!instructions.contains("balanced"));
        plan = SparklePlanner::parse({{"action", "edit"}, {"editQuality", "best"}, {"instruction", "Make it red"}}, context);
        QVERIFY(plan);
        QCOMPARE(plan->action, Plan::Action::Edit);
        QCOMPARE(plan->qualityId, QString("balanced"));
        plan = SparklePlanner::parse({{"action", "generate"}, {"shot", "full-body"},
            {"ratio", "2:3"}, {"prompt", "a knight"}}, context);
        QVERIFY(plan);
        QCOMPARE(plan->shot, ShotPicker::Shot::FullBody);
        QCOMPARE(plan->ratio, QString("2:3"));
        // The base image is what an edit changes; the describer is offered every image by name.
        context.base = true;
        context.references = 2;
        QCOMPARE(context.images(), (QStringList{"base", "reference 1", "reference 2", "displayed"}));
        const auto look = SparklePlanner::schema(context).value("anyOf").toArray().last().toObject();
        QCOMPARE(look.value("properties").toObject().value("images").toObject().value("items").toObject()
                     .value("enum").toArray(), QJsonArray({"base", "reference 1", "reference 2", "displayed"}));
        instructions = SparklePlanner::instructions(context);
        QVERIFY(instructions.contains("The edit changes the base image."));
        QVERIFY(instructions.contains("2 reference image(s)"));
        plan = SparklePlanner::parse({{"action", "look"}, {"images", QJsonArray{"reference 2", "nope", "reference 2", "base"}},
            {"describerQuestion", "Porte-t-elle des lunettes ?"}}, context);
        QVERIFY(plan); QCOMPARE(plan->action, Plan::Action::Look);
        QCOMPARE(plan->images, (QStringList{"reference 2", "base"}));
        QVERIFY(!SparklePlanner::parse({{"action", "look"}, {"images", QJsonArray{"nope"}}, {"describerQuestion", "?"}}, context));
        // Without an image describer, nothing can look; without a quality or a preset, nothing
        // can be edited or created.
        context.canLook = false;
        context.qualityId.clear();
        context.preset.reset();
        QCOMPARE(branches(context).keys(), (QStringList{"ask", "prompt", "reply"}));
        instructions = SparklePlanner::instructions(context);
        QVERIFY(instructions.contains("Nothing can look at them for you now."));
        QVERIFY(instructions.contains("written as phrases"));
    }

    void kiraSparkleEngineMonograms()
    {
        QCOMPARE(KiraSparkle::monograms({"Photorealism", "Illustration"}), (QStringList{"Ph", "Il"}));
        // Clashes fall back to the first words' initials, then to a number.
        QCOMPARE(KiraSparkle::monograms({"Photo portrait", "Photo landscape", "Photo"}),
            (QStringList{"Ph", "PL", "P2"}));
        QCOMPARE(KiraSparkle::monograms({"3D render", " "}), (QStringList{"3D", "?"}));
    }

    void kiraSparklePlansAndRunsRequests()
    {
        const auto originalLocale = QLocale();
        const auto restoreLocale = qScopeGuard([&] { QLocale::setDefault(originalLocale); });
        QLocale::setDefault(QLocale("en_US"));
        QWebSocketServer server("kira-sparkle-test", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QWebSocket* peer = nullptr;
        QList<QJsonObject> submissions;
        auto routing = Nukleus::Protocol::starterModelRouting();
        // Illustration's model cannot edit, so it cannot use reference images.
        for (auto& preset : routing.presets)
            preset.model.modelId = preset.id == "illustration" ? "sketch-model" : "image-model";
        for (auto& quality : routing.editingQualities) quality.model.modelId = "image-model";
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this, [&](const QString& text) {
                const auto request = QJsonDocument::fromJson(text.toUtf8()).object();
                if (request.value("type") == "authenticate") {
                    peer->sendTextMessage("{\"type\":\"authenticated\"}");
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "capabilities"},
                        {"state", "ready"}, {"operations", QJsonArray{"image.generate", "image.edit", "model-routing",
                            "prompt.enhance", "text.chat", "text.chat.images"}}}).toJson()));
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "model-routing.get.completed"},
                        {"routing", Nukleus::Protocol::modelRoutingToJson(routing)}}).toJson()));
                }
                if (request.value("type") == "models.list")
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "models.list.result"},
                        {"categories", QJsonArray{QJsonObject{{"category", "image-generation"},
                            {"models", QJsonArray{QJsonObject{{"id", "image-model"}, {"displayName", "Image"},
                                {"recommendedOptions", QJsonObject{{"promptFormat", "natural"}}}},
                                QJsonObject{{"id", "sketch-model"}, {"displayName", "Sketch"},
                                    {"recommendedOptions", QJsonObject{{"promptFormat", "natural"},
                                        {"canEdit", false}}}}}}},
                            QJsonObject{{"category", "llm-chat"}, {"models", QJsonArray{QJsonObject{
                                {"id", "llm-chat:enhancer"}, {"displayName", "Enhancer"},
                                {"recommendedOptions", QJsonObject{{"defaultPromptEnhancer", true}}}},
                                QJsonObject{{"id", "llm-chat:vision"}, {"displayName", "Vision"},
                                    {"mmprojModelId", "companion:projector.gguf"},
                                    {"recommendedOptions", QJsonObject{{"defaultImageDescriber", true}}}}}}}}}}).toJson()));
                if (request.value("type") == "submit") submissions.append(request);
            });
        });
        MainWindow window;
        auto* client = window.findChild<CanvasServiceController*>();
        auto* workspace = window.findChild<GenerationWorkspaceWidget*>();
        auto* sparkle = workspace->sparkle();
        auto* activity = workspace->activity();
        auto* send = sparkle->sendButton();
        sparkle->prompt()->setPlainText("Un chevalier debout dans une forêt, style photo");
        // Without a service, Send explains why it is unavailable and keeps the request.
        QVERIFY(!send->isEnabled());
        QVERIFY(send->toolTip().contains("prompt enhancer"));
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        client->connectToService();
        QTRY_VERIFY(send->isEnabled());
        const auto reply = [&](const QJsonObject& message) {
            peer->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));
        };
        const auto answer = [&](const QJsonObject& request, const QJsonObject& plan) {
            reply({{"type", "result"}, {"jobId", request.value("jobId")}, {"operation", "text.chat"},
                {"text", QString::fromUtf8(QJsonDocument(plan).toJson())}, {"json", plan}});
        };
        // The engine button offers the card's presets and the edit bubble's qualities.
        auto* engine = sparkle->engineButton();
        QTRY_VERIFY(engine->toolTip().contains("Creation: Photorealism"));
        QVERIFY(engine->toolTip().contains("Editing: Balanced"));
        QCOMPARE(engine->size(), QSize(engine->width(), send->height()));
        const auto choose = [&](const QString& text) {
            QStringList offered;
            QTimer::singleShot(0, this, [&offered, text] {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if (!menu) return;
                for (auto* action : menu->actions()) {
                    offered.append(action->text());
                    if (action->text().section('\t', 0, 0) == text) action->trigger();
                }
                menu->close();
            });
            engine->click();
            return offered;
        };
        const QStringList offered = choose("Illustration");
        QCOMPARE(offered, (QStringList{"Creation", "Photorealism", "Illustration\tNo references", "Editing", "Fast",
                              "Balanced", "Quality"}));
        auto* parameters = window.findChild<GenerationParametersWidget*>();
        QCOMPARE(parameters->selectedPreset()->id, QString("illustration"));
        QVERIFY(engine->toolTip().contains("Creation: Illustration"));
        // The card's preset choice shows in the engine button too.
        QVERIFY(parameters->selectPreset("photorealism"));
        QVERIFY(engine->toolTip().contains("Creation: Photorealism"));
        choose("Illustration");
        // Without any image, the enhancer may answer, ask, write a prompt or create an image.
        send->click();
        QVERIFY(sparkle->prompt()->toPlainText().isEmpty());
        // Planning shows in the chat as a progress message, not in the workspace pill.
        auto* thinking = sparkle->findChild<QLabel*>("kiraSparkleThinking");
        QVERIFY(thinking);
        QCOMPARE(thinking->text(), QString("Understanding your request…"));
        QCOMPARE(activity->state(), JobActivityIndicator::State::Idle);
        QTRY_COMPARE(submissions.size(), 1);
        auto chat = submissions.takeFirst();
        QCOMPARE(chat.value("operation").toString(), QString("text.chat"));
        auto options = chat.value("options").toObject();
        QCOMPARE(options.value("modelId").toString(), QString("llm-chat:enhancer"));
        const auto messages = options.value("messages").toArray();
        QCOMPARE(messages.size(), 2);
        const auto actionsOf = [](const QJsonObject& options) {
            QStringList actions;
            for (const auto& branch : options.value("parameters").toObject().value("jsonSchema").toObject()
                                          .value("anyOf").toArray())
                actions.append(branch.toObject().value("properties").toObject().value("action").toObject()
                                   .value("const").toString());
            return actions;
        };
        QVERIFY(messages.at(0).toObject().value("content").toString().contains("No image is displayed."));
        QVERIFY(messages.at(0).toObject().value("content").toString().contains("question in French"));
        QCOMPARE(messages.at(1).toObject().value("content").toString(),
            QString("Un chevalier debout dans une forêt, style photo"));
        QCOMPARE(options.value("parameters").toObject().value("penalizeRepeats").toBool(true), false);
        QCOMPARE(actionsOf(options), (QStringList{"reply", "ask", "prompt", "generate", "edit"}));
        QVERIFY(messages.at(0).toObject().value("content").toString().contains("the preset the user chose, \"Illustration\""));
        QVERIFY(client->jobActive());
        QVERIFY(!engine->isEnabled()); // Locked while planning, like the card's preset.
        // Creating is first proposed in the user's language, with the English prompts as tooltip;
        // nothing is queued until the user accepts it.
        const QString cardRatio = parameters->resolutionRatio();
        answer(chat, {{"action", "generate"}, {"shot", "full-body"}, {"ratio", "2:3"},
            {"description", "Un chevalier debout dans une forêt."}, {"avoid", "casque, casque"},
            {"prompt", "A knight standing in a forest"}, {"negativePrompt", "helmet, Helmet"}});
        const auto lastLabel = [&] { return sparkle->findChildren<QLabel*>("kiraSparkleMessage").last(); };
        const auto lastMessage = [&] { return lastLabel()->text(); };
        QTRY_COMPARE(lastMessage(), QString("Un chevalier debout dans une forêt.\n\nShot: Full body · Ratio: 2:3\n"
                                            "Avoid: casque"));
        QCOMPARE(lastLabel()->toolTip(), QString("Prompt: A knight standing in a forest\nNegative prompt: helmet"));
        QVERIFY(!sparkle->findChild<QLabel*>("kiraSparkleThinking"));
        QTest::qWait(50);
        QVERIFY(submissions.isEmpty());
        auto proposals = sparkle->findChildren<QPushButton*>("kiraSparkleProposalAction");
        QCOMPARE(proposals.size(), 1);
        QCOMPARE(proposals.last()->text(), QString("Create the image"));
        // Accepting creates it with the plan's English prompts, shot and ratio and the card's preset,
        // without translating them or changing the card, and follows the image in the chat.
        proposals.last()->click();
        QVERIFY(!proposals.last()->isEnabled());
        QCOMPARE(proposals.last()->text(), QString("Created"));
        QTRY_COMPARE(submissions.size(), 1);
        auto generation = submissions.takeFirst();
        QCOMPARE(generation.value("operation").toString(), QString("image.generate"));
        auto generationOptions = generation.value("options").toObject();
        QCOMPARE(generationOptions.value("presetId").toString(), QString("illustration"));
        QVERIFY(generationOptions.value("prompt").toString().startsWith("full body"));
        QVERIFY(generationOptions.value("prompt").toString().endsWith(", A knight standing in a forest"));
        QVERIFY(generationOptions.value("negativePrompt").toString().endsWith(", helmet"));
        const QSize portrait = parameters->resolutionForRatio("2:3");
        QVERIFY(portrait.isValid());
        QCOMPARE(QSize(generationOptions.value("width").toInt(), generationOptions.value("height").toInt()), portrait);
        QCOMPARE(generationOptions.value("inputCount").toInt(), 0);
        QVERIFY(engine->isEnabled());
        QVERIFY(!client->jobActive());
        QCOMPARE(window.findChild<ShotPicker*>("shotPicker")->currentShot(), ShotPicker::Shot::Free);
        QCOMPARE(parameters->resolutionRatio(), cardRatio);
        QCOMPARE(parameters->selectedPreset()->id, QString("illustration"));
        QCOMPARE(activity->state(), JobActivityIndicator::State::Idle);
        QCOMPARE(sparkle->findChildren<QWidget*>("kiraSparkleResult").size(), 1);
        QImage knight(portrait, QImage::Format_RGB32);
        knight.fill(Qt::darkGreen);
        QByteArray generated;
        QBuffer generatedBuffer(&generated);
        QVERIFY(generatedBuffer.open(QIODevice::WriteOnly) && knight.save(&generatedBuffer, "PNG"));
        reply({{"type", "result"}, {"jobId", generation.value("jobId")}, {"operation", "image.generate"},
            {"mimeType", "image/png"}, {"prompt", "A knight standing in a forest"}});
        peer->sendBinaryMessage(generated);
        // The image joins the history without being selected, and becomes Kira Sparkle's base
        // image, which the planner then knows it created.
        auto* historyModel = window.findChild<GenerationHistoryWidget*>()->model();
        QTRY_COMPARE(historyModel->state(historyModel->index(0).data(GenerationHistoryModel::IdRole).toUuid()),
            GenerationHistoryModel::State::Ready);
        QVERIFY(workspace->currentImage().isNull());
        QTRY_COMPARE(sparkle->baseImage(), knight);
        // Answers, follow-up questions and written prompts show in the chat as they are.
        const auto ask = [&](const QString& request, const QJsonObject& plan) {
            sparkle->prompt()->setPlainText(request);
            if (!QTest::qWaitFor([&] { return send->isEnabled(); })) return QJsonObject();
            send->click();
            if (!QTest::qWaitFor([&] { return submissions.size() == 1; })) return QJsonObject();
            const auto chat = submissions.takeFirst();
            answer(chat, plan);
            return chat.value("options").toObject();
        };
        options = ask("Merci !", {{"action", "reply"}, {"message", "Avec plaisir !"}});
        QTRY_COMPARE(lastMessage(), QString("Avec plaisir !"));
        // Earlier exchanges come back as conversation turns: the request, then the JSON plan.
        auto turns = options.value("messages").toArray();
        QCOMPARE(turns.size(), 4);
        QCOMPARE(turns.at(1).toObject().value("role").toString(), QString("user"));
        QCOMPARE(turns.at(1).toObject().value("content").toString(),
            QString("Un chevalier debout dans une forêt, style photo"));
        QCOMPARE(turns.at(2).toObject().value("role").toString(), QString("assistant"));
        QCOMPARE(QJsonDocument::fromJson(turns.at(2).toObject().value("content").toString().toUtf8()).object()
                     .value("prompt").toString(), QString("A knight standing in a forest"));
        QCOMPARE(turns.at(3).toObject().value("content").toString(), QString("Merci !"));
        sparkle->prompt()->setPlainText("Rends-le plus sombre");
        QTRY_VERIFY(send->isEnabled());
        send->click();
        QTRY_COMPARE(submissions.size(), 1);
        chat = submissions.takeFirst();
        QVERIFY(chat.value("options").toObject().value("messages").toArray().at(0).toObject().value("content")
                    .toString().contains("You created it from your accepted proposal, with this prompt: A knight "
                                         "standing in a forest"));
        answer(chat, {{"action", "reply"}, {"message", "D'accord."}});
        QTRY_COMPARE(lastMessage(), QString("D'accord."));
        // A written acceptance of the latest proposal creates it without asking the LLM again.
        sparkle->prompt()->setPlainText("Un dragon");
        QTRY_VERIFY(send->isEnabled());
        send->click();
        QTRY_COMPARE(submissions.size(), 1);
        chat = submissions.takeFirst();
        answer(chat, {{"action", "generate"}, {"shot", "free"}, {"ratio", "1:1"}, {"description", "Un dragon."},
            {"avoid", ""}, {"prompt", "A dragon"}, {"negativePrompt", ""}});
        QTRY_COMPARE(lastMessage(), QString("Un dragon.\n\nShot: Free · Ratio: 1:1"));
        sparkle->prompt()->setPlainText("Oui, vas-y !");
        QTRY_VERIFY(send->isEnabled());
        send->click();
        QTRY_COMPARE(submissions.size(), 1);
        generation = submissions.takeFirst();
        QCOMPARE(generation.value("operation").toString(), QString("image.generate"));
        QCOMPARE(generation.value("options").toObject().value("prompt").toString(), QString("A dragon"));
        proposals = sparkle->findChildren<QPushButton*>("kiraSparkleProposalAction");
        QCOMPARE(proposals.last()->text(), QString("Created"));
        // Once nothing waits, an acceptance goes to the LLM and editing is offered again.
        sparkle->setBaseImage(QImage());
        sparkle->prompt()->setPlainText("Un autre dragon");
        QTRY_VERIFY(send->isEnabled());
        send->click();
        QTRY_COMPARE(submissions.size(), 1);
        chat = submissions.takeFirst();
        QVERIFY(actionsOf(chat.value("options").toObject()).contains("edit"));
        answer(chat, {{"action", "generate"}, {"shot", "free"}, {"ratio", "1:1"}, {"description", "Un dragon rouge."},
            {"avoid", ""}, {"prompt", "A red dragon"}, {"negativePrompt", ""}});
        QTRY_COMPARE(lastMessage(), QString("Un dragon rouge.\n\nShot: Free · Ratio: 1:1"));
        // Changing a waiting proposal makes a new one, which replaces it; editing is not offered.
        sparkle->prompt()->setPlainText("Oui mais de nuit");
        QTRY_VERIFY(send->isEnabled());
        send->click();
        QTRY_COMPARE(submissions.size(), 1);
        chat = submissions.takeFirst();
        QVERIFY(!actionsOf(chat.value("options").toObject()).contains("edit"));
        answer(chat, {{"action", "generate"}, {"shot", "free"}, {"ratio", "1:1"},
            {"description", "Un dragon rouge la nuit."}, {"avoid", ""}, {"prompt", "A red dragon at night"},
            {"negativePrompt", ""}});
        QTRY_COMPARE(lastMessage(), QString("Un dragon rouge la nuit.\n\nShot: Free · Ratio: 1:1"));
        proposals = sparkle->findChildren<QPushButton*>("kiraSparkleProposalAction");
        QCOMPARE(proposals.at(proposals.size() - 2)->text(), QString("Replaced"));
        QVERIFY(!proposals.at(proposals.size() - 2)->isEnabled());
        proposals.last()->click();
        QTRY_COMPARE(submissions.size(), 1);
        generation = submissions.takeFirst();
        QCOMPARE(generation.value("options").toObject().value("prompt").toString(), QString("A red dragon at night"));
        ask("Fais quelque chose", {{"action", "ask"}, {"question", "Un portrait ou un paysage ?"}});
        QTRY_COMPARE(lastMessage(), QString("Un portrait ou un paysage ?"));
        ask("Écris un prompt de dragon", {{"action", "prompt"}, {"message", "Voici un prompt :"},
            {"prompt", "A dragon in a misty forest"}, {"negativePrompt", ""}});
        QTRY_COMPARE(lastMessage(), QString("Voici un prompt :\n\nA dragon in a misty forest"));
        // Without an image to change, an edit asks for a base image.
        ask("Rends le ciel violet", {{"action", "edit"}, {"instruction", "Make the sky violet"}});
        QTRY_COMPARE(lastMessage(), QString("Attach the image to change as the base image with the + button, "
                                            "then send your request again."));
        // With a displayed image, a base and a reference, editing and looking are offered too.
        QImage image(64, 48, QImage::Format_RGB32); image.fill(Qt::blue);
        workspace->appendImage(image, "Source", "a blue square");
        QImage attachment(16, 16, QImage::Format_RGB32); attachment.fill(Qt::yellow);
        sparkle->setBaseImage(image);
        QVERIFY(sparkle->addAttachment(attachment));
        // The quality chosen in the engine button is the edit bubble's, and the reverse.
        choose("Quality");
        QCOMPARE(workspace->editQualityId(), QString("edit-high"));
        auto* bubbleQuality = workspace->findChild<QualityStepSlider*>("editQuality");
        bubbleQuality->setValue(0);
        QVERIFY(engine->toolTip().contains("Editing: Fast"));
        bubbleQuality->setValue(2);
        QVERIFY(engine->toolTip().contains("Editing: Quality"));
        options = ask("Mets-lui la tenue de la référence 1",
            {{"action", "edit"}, {"instruction", "Dress her in the outfit of reference 1"}});
        const auto system = options.value("messages").toArray().at(0).toObject().value("content").toString();
        QVERIFY(system.contains("64 × 48"));
        QVERIFY(system.contains("generated from this prompt: a blue square"));
        QVERIFY(system.contains("attached a base image"));
        QVERIFY(system.contains("attached 1 reference image(s)"));
        QVERIFY(!system.contains("edit-high"));
        QCOMPARE(actionsOf(options), (QStringList{"reply", "ask", "prompt", "generate", "edit", "look"}));
        QTRY_COMPARE(lastMessage(), QString("Next step, not run yet: edit the base image.\n"
                                            "Instruction: Dress her in the outfit of reference 1"));
        ask("Porte-t-elle des lunettes ?", {{"action", "look"}, {"images", QJsonArray{"base", "reference 1"}},
            {"describerQuestion", "Porte-t-elle des lunettes ?"}});
        QTRY_COMPARE(lastMessage(), QString("Next step, not run yet: show the base image, reference 1 to the image "
                                            "describer.\nQuestion: Porte-t-elle des lunettes ?"));
        QTest::qWait(50);
        QVERIFY(submissions.isEmpty()); // Nothing is edited or shown to the describer yet.
        QCOMPARE(workspace->currentImage(), image);
        // Illustration cannot use the reference: it shows as ignored for creating, the planner
        // is told and a new image leaves it out.
        auto* attachments = sparkle->referenceStrip();
        QVERIFY(attachments->ignoredReason().contains("only edits use them"));
        QVERIFY(engine->toolTip().contains("The creation preset ignores the reference images."));
        QVERIFY(system.contains("This preset cannot use reference images"));
        options = ask("Crée-la dans un café", {{"action", "generate"}, {"shot", "medium"}, {"ratio", "1:1"},
            {"prompt", "A woman in a cafe"}, {"negativePrompt", ""}});
        QTRY_VERIFY(lastMessage().endsWith("Illustration cannot use reference images: they will be left out."));
        sparkle->findChildren<QPushButton*>("kiraSparkleProposalAction").last()->click();
        QTRY_COMPARE(submissions.size(), 1);
        generation = submissions.takeFirst();
        QCOMPARE(generation.value("operation").toString(), QString("image.generate"));
        QVERIFY(!generation.value("options").toObject().contains("inputCount"));
        // With a preset that can use them, they count again.
        choose("Photorealism");
        QVERIFY(attachments->ignoredReason().isEmpty());
        QVERIFY(!engine->toolTip().contains("ignores"));
        choose("Illustration");
        // A failed plan answers in the chat and gives the request back.
        sparkle->prompt()->setPlainText("Ajoute un chat");
        QTRY_VERIFY(send->isEnabled());
        send->click();
        QTRY_COMPARE(submissions.size(), 1);
        chat = submissions.takeFirst();
        reply({{"type", "error"}, {"jobId", chat.value("jobId")}, {"code", "chat-invalid-json"},
            {"message", "The reply is not valid JSON"}});
        QTRY_VERIFY(lastMessage().contains("not valid JSON"));
        QVERIFY(!sparkle->findChild<QLabel*>("kiraSparkleThinking"));
        QCOMPARE(activity->state(), JobActivityIndicator::State::Idle);
        QCOMPARE(sparkle->prompt()->toPlainText(), QString("Ajoute un chat"));
        QVERIFY(submissions.isEmpty());
        // New chat empties the conversation and greets again, keeping the draft.
        sparkle->findChild<QToolButton*>("kiraSparkleNewChat")->click();
        QCOMPARE(sparkle->findChildren<QLabel*>("kiraSparkleMessage").size(), 1);
        QCOMPARE(lastMessage(), QString("How can I help you?"));
        QCOMPARE(sparkle->prompt()->toPlainText(), QString("Ajoute un chat"));
        // ...and forgets the earlier exchanges; a failed request was never remembered. The memory
        // keeps the last six exchanges.
        for (int i = 0; i < 7; ++i) {
            options = ask(QString("Question %1").arg(i), {{"action", "reply"}, {"message", QString("Réponse %1").arg(i)}});
            QTRY_COMPARE(lastMessage(), QString("Réponse %1").arg(i));
            turns = options.value("messages").toArray();
            QCOMPARE(turns.size(), 2 + 2 * qMin(i, 6));
        }
        QCOMPARE(turns.at(1).toObject().value("content").toString(), QString("Question 0"));
        options = ask("Question 7", {{"action", "reply"}, {"message", "Réponse 7"}});
        turns = options.value("messages").toArray();
        QCOMPARE(turns.size(), 14);
        QCOMPARE(turns.at(1).toObject().value("content").toString(), QString("Question 1"));
    }

    void generationSelectorHonorsModelCapabilities()
    {
        GenerationParametersWidget parameters;
        Nukleus::ModelInfo editOnly;
        editOnly.id = "image-generation:edit";
        editOnly.category = Nukleus::ModelCategory::ImageGeneration;
        editOnly.recommendedOptions = {{"canEdit", true}, {"canGenerate", false}};
        auto generateOnly = editOnly;
        generateOnly.id = "image-generation:generate";
        generateOnly.recommendedOptions = {{"canEdit", false}, {"canGenerate", true}};
        parameters.setModels({editOnly, generateOnly});
        QCOMPARE(parameters.modelId(), generateOnly.id);
        parameters.setModels({editOnly});
        QVERIFY(parameters.modelId().isEmpty());
        auto legacy = editOnly;
        legacy.id = "image-generation:legacy"; legacy.recommendedOptions = {};
        parameters.setModels({editOnly, legacy});
        QCOMPARE(parameters.modelId(), legacy.id);
    }

    void defaultModelPreferencesPersistAndCancel()
    {
        CanvasServiceController client;
        QSettings().setValue("Service/DefaultEditionModel", "image-generation:saved");
        QSettings().setValue("Service/DefaultUpscalerModel", "upscaler:saved");
        CanvasServiceController reopened;
        QCOMPARE(reopened.defaultEditionModel(), QString("image-generation:saved"));
        QVERIFY(reopened.defaultUpscalerModel().isEmpty());
        QVERIFY(!reopened.canUpscale());
        {
            ServicePreferencesDialog dialog(&client);
            auto* edition = dialog.findChild<QComboBox*>("defaultEditionModel");
            auto* upscaler = dialog.findChild<QComboBox*>("defaultUpscalerModel");
            QVERIFY(!edition && !upscaler);
            dialog.reject();
            QCOMPARE(client.defaultEditionModel(), QString("image-generation:saved"));
        }
        ServicePreferencesDialog dialog(&client);
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        buttons->button(QDialogButtonBox::Save)->click();
        QCOMPARE(client.defaultEditionModel(), QString("image-generation:saved"));
        QVERIFY(client.defaultUpscalerModel().isEmpty());
        QCOMPARE(QSettings().value("Service/DefaultUpscalerModel").toString(), QString("upscaler:saved"));
    }

    void configuredUpscaleMenuRequestsTargetImage()
    {
        GenerationWorkspaceWidget workspace;
        workspace.resize(720, 600);
        workspace.show();
        Nukleus::ModelInfo model;
        model.id = "upscaler:test";
        model.category = Nukleus::ModelCategory::Upscaler;
        workspace.setDefaultModels({model}, {}, model.id);
        QImage image(40, 40, QImage::Format_RGB32); image.fill(Qt::red);
        workspace.appendImage(image, "Preview");
        QCoreApplication::processEvents();
        auto* history = workspace.findChild<GenerationHistoryWidget*>();
        QSignalSpy requests(&workspace, &GenerationWorkspaceWidget::upscaleRequested);
        for (auto* viewport : {workspace.preview()->viewport(), history->view()->viewport()}) {
            const QPoint point = viewport == workspace.preview()->viewport() ? QPoint(20, 20)
                : history->view()->visualRect(history->model()->index(0)).center();
            QTimer::singleShot(0, &workspace, [&] {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                QVERIFY(menu);
                QCOMPARE(menu->actions().size(), 4);
                QCOMPARE(menu->actions().at(2)->text(), QString("Upscale"));
                QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                    menu->actionGeometry(menu->actions().at(2)).center());
            });
            QContextMenuEvent event(QContextMenuEvent::Mouse, point, viewport->mapToGlobal(point));
            QApplication::sendEvent(viewport, &event);
            QCOMPARE(workspace.currentImage(), image);
            QCOMPARE(history->model()->rowCount(), 1);
        }
        QCOMPARE(requests.count(), 2);
        QCOMPARE(requests.first().at(0).value<QImage>(), image);
    }

    void upscaleUploadsImageAndAppendsDoubleResolutionResult()
    {
        QWebSocketServer server("upscale-test", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QWebSocket* peer = nullptr;
        QJsonObject submission;
        QByteArray uploaded;
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this, [&](const QString& text) {
                const auto message = QJsonDocument::fromJson(text.toUtf8()).object();
                if (message.value("type") == "authenticate") {
                    peer->sendTextMessage("{\"type\":\"authenticated\"}");
                    peer->sendTextMessage("{\"type\":\"capabilities\",\"state\":\"ready\",\"operations\":[\"image.upscale\"]}");
                    peer->sendTextMessage("{\"type\":\"models.list.result\",\"categories\":[{\"category\":\"upscaler\",\"models\":[{\"id\":\"upscaler:test\",\"name\":\"Test\"},{\"id\":\"upscaler:second\",\"name\":\"Second\"}]}]}");
                } else if (message.value("type") == "submit") {
                    submission = message;
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                        {"type", "upload-required"}, {"jobId", message.value("jobId")}}).toJson()));
                }
            });
            connect(peer, &QWebSocket::binaryMessageReceived, this, [&](const QByteArray& bytes) { uploaded = bytes; });
        });
        MainWindow window;
        auto* client = window.findChild<CanvasServiceController*>();
        auto* workspace = window.findChild<GenerationWorkspaceWidget*>();
        auto* history = workspace->findChild<GenerationHistoryWidget*>();
        QSettings().setValue("Service/DefaultUpscalerModel", "upscaler:second");
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        client->connectToService();
        QTRY_VERIFY(client->canUpscale());
        QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::blue);
        workspace->appendImage(image, "Original");
        workspace->upscaleRequested(image);
        QTRY_VERIFY(!uploaded.isEmpty());
        QCOMPARE(QImage::fromData(uploaded), image);
        QCOMPARE(submission.value("operation").toString(), QString("image.upscale"));
        QCOMPARE(submission.value("options").toObject().value("modelId").toString(), QString("upscaler:test"));
        QVERIFY(!client->canUpscale());
        QVERIFY(!client->submitUpscale(image));
        QImage output(128, 96, QImage::Format_RGB32); output.fill(Qt::green);
        QByteArray bytes; QBuffer buffer(&bytes); QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(output.save(&buffer, "PNG"));
        peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
            {"type", "result"}, {"jobId", submission.value("jobId")}, {"operation", "image.upscale"},
            {"mimeType", "image/png"}}).toJson()));
        QVERIFY(!client->canUpscale());
        peer->sendBinaryMessage(bytes);
        QTRY_COMPARE(history->model()->rowCount(), 2);
        QCOMPARE(workspace->currentImage().size(), QSize(64, 48));
        QTRY_VERIFY(client->canUpscale());
        QCOMPARE(history->model()->image(history->model()->index(0).data(GenerationHistoryModel::IdRole).toUuid()), image);
        workspace->upscaleRequested(image);
        QTRY_VERIFY(!client->canUpscale());
        peer->close();
        QTRY_VERIFY(!client->isAuthenticated());
        QCOMPARE(history->model()->rowCount(), 2);
        QCOMPARE(workspace->currentImage().size(), QSize(64, 48));
    }

    void editingUploadsPromptAndRetainsSourceResolution()
    {
        QWebSocketServer server("edit-test", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QWebSocket* peer = nullptr;
        QJsonObject submission;
        QList<QByteArray> uploaded;
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this, [&](const QString& text) {
                const auto message = QJsonDocument::fromJson(text.toUtf8()).object();
                if (message.value("type") == "authenticate") {
                    peer->sendTextMessage("{\"type\":\"authenticated\"}");
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                        {"type", "capabilities"}, {"state", "ready"}, {"operations", QJsonArray{"image.edit", "model-routing"}}}).toJson()));
                    auto routing = Nukleus::Protocol::starterModelRouting();
                    routing.editingQualities[1].model = {"image-generation:test", 4, 1.0};
                    routing.editingQualities[2].model = {"image-generation:test", 9, 2.5};
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                        {"type", "model-routing.get.completed"}, {"routing", Nukleus::Protocol::modelRoutingToJson(routing)}}).toJson()));
                    peer->sendTextMessage("{\"type\":\"models.list.result\",\"categories\":[{\"category\":\"image-generation\",\"models\":[{\"id\":\"image-generation:test\",\"name\":\"Test\"}]}]}");
                } else if (message.value("type") == "submit") {
                    submission = message;
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                        {"type", "upload-required"}, {"jobId", message.value("jobId")}}).toJson()));
                }
            });
            connect(peer, &QWebSocket::binaryMessageReceived, this, [&](const QByteArray& bytes) {
                uploaded.append(bytes);
                if (uploaded.size() < submission.value("options").toObject().value("inputCount").toInt())
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "upload-required"},
                        {"jobId", submission.value("jobId")}, {"inputIndex", int(uploaded.size())}}).toJson()));
            });
        });
        MainWindow window;
        auto* client = window.findChild<CanvasServiceController*>();
        auto* workspace = window.findChild<GenerationWorkspaceWidget*>();
        auto* history = workspace->findChild<GenerationHistoryWidget*>();
        QSettings().setValue("Service/DefaultEditionModel", "image-generation:test");
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        client->connectToService();
        QTRY_VERIFY(client->canEdit());
        QImage image(128, 96, QImage::Format_RGB32); image.fill(Qt::blue);
        workspace->appendImage(image, "Original");
        QImage firstReference(32, 32, QImage::Format_RGB32); firstReference.fill(Qt::yellow);
        QImage secondReference(24, 48, QImage::Format_RGB32); secondReference.fill(Qt::magenta);
        auto* bubble = workspace->findChild<EditBubble*>();
        QVERIFY(bubble->addReference(firstReference) && bubble->addReference(secondReference));
        workspace->findChild<Ardoise::PlainTextEdit*>("imageEditPrompt")->setPlainText("Make the sky green");
        workspace->findChild<QualityStepSlider*>("editQuality")->setValue(2);
        workspace->findChild<QPushButton*>("imageEditButton")->click();
        QTRY_COMPARE(uploaded.size(), 3);
        QCOMPARE(submission.value("options").toObject().value("inputCount").toInt(), 3);
        QCOMPARE(QImage::fromData(uploaded.at(0)), image);
        QCOMPARE(QImage::fromData(uploaded.at(1)), firstReference);
        QCOMPARE(QImage::fromData(uploaded.at(2)), secondReference);
        QCOMPARE(submission.value("operation").toString(), QString("image.edit"));
        QCOMPARE(submission.value("options").toObject().value("qualityId").toString(), QString("edit-high"));
        QCOMPARE(submission.value("options").toObject().value("steps").toInt(), 9);
        QCOMPARE(submission.value("options").toObject().value("cfgScale").toDouble(), 2.5);
        // Edits queue like generations: no single-job lock, and a progress tile in the history.
        QVERIFY(workspace->findChild<QualityStepSlider*>("editQuality")->isEnabled());
        QCOMPARE(history->model()->rowCount(), 2);
        const QUuid editTile = history->model()->index(1).data(GenerationHistoryModel::IdRole).toUuid();
        QCOMPARE(history->model()->state(editTile), GenerationHistoryModel::State::Pending);
        QVERIFY(!submission.value("options").toObject().contains("presetId"));
        QCOMPARE(submission.value("options").toObject().value("modelId").toString(), QString("image-generation:test"));
        QCOMPARE(submission.value("options").toObject().value("prompt").toString(), QString("Make the sky green"));
        QCOMPARE(submission.value("options").toObject().value("width").toInt(), 128);
        QCOMPARE(submission.value("options").toObject().value("height").toInt(), 96);
        QVERIFY(client->canEdit());
        QVERIFY(!client->jobActive());
        QImage output(256, 192, QImage::Format_RGB32); output.fill(Qt::green);
        QByteArray bytes; QBuffer buffer(&bytes); QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(output.save(&buffer, "PNG"));
        peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
            {"type", "result"}, {"jobId", submission.value("jobId")}, {"operation", "image.edit"},
            {"mimeType", "image/png"}}).toJson()));
        peer->sendBinaryMessage(bytes);
        // The edited image replaces its tile at the source's size, without being selected.
        QTRY_COMPARE(history->model()->state(editTile), GenerationHistoryModel::State::Ready);
        QCOMPARE(history->model()->rowCount(), 2);
        QCOMPARE(history->model()->image(editTile).size(), QSize(128, 96));
        QCOMPARE(workspace->currentImage(), image);
        QCOMPARE(history->model()->image(history->model()->index(0).data(GenerationHistoryModel::IdRole).toUuid()), image);
        // An identity attaches its references and joins its edit instruction to the draft.
        const bool nativeDialogsDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restoreDialogs = qScopeGuard([&] {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogsDisabled);
        });
        QTemporaryDir identityDirectory;
        QImage face(20, 20, QImage::Format_RGB32); face.fill(Qt::darkRed);
        QImage profile(20, 30, QImage::Format_RGB32); profile.fill(Qt::darkCyan);
        const QString identityPath = writeIdentity(identityDirectory, {face, profile}, "Use the faces.",
            "Edit the face to match the references.");
        QVERIFY(!identityPath.isEmpty());
        auto* editPrompt = workspace->findChild<Ardoise::PlainTextEdit*>("imageEditPrompt");
        // The bubble has no identity button; identities go through the toolbar tool.
        QVERIFY(!workspace->findChild<QToolButton*>("imageEditAttachIdentity"));
        editPrompt->clear();
        bubble->clearReferences();
        // The toolbar tool edits the selected image with the identity straight away.
        auto* identityEdit = workspace->findChild<QToolButton*>("imageEditWithIdentity");
        QVERIFY(identityEdit && identityEdit->isEnabled());
        const QImage edited = workspace->currentImage();
        uploaded.clear();
        selectInNextFileDialog(&window, identityPath);
        identityEdit->click();
        QTRY_COMPARE(uploaded.size(), 3);
        QCOMPARE(submission.value("options").toObject().value("prompt").toString(),
            QString("Edit the face to match the references."));
        // Without a quality choice, identity edits use the default quality.
        QCOMPARE(submission.value("options").toObject().value("qualityId").toString(), QString("edit-medium"));
        QCOMPARE(submission.value("options").toObject().value("steps").toInt(), 4);
        QCOMPARE(QImage::fromData(uploaded.at(0)), edited);
        QCOMPARE(QImage::fromData(uploaded.at(1)), face);
        QCOMPARE(QImage::fromData(uploaded.at(2)), profile);
        QVERIFY(editPrompt->toPlainText().isEmpty()); // The bubble's draft is untouched.
        QVERIFY(identityEdit->isEnabled()); // Queued edits never block the next one.
        QCOMPARE(history->model()->rowCount(), 3);
        const QUuid identityTile = history->model()->index(2).data(GenerationHistoryModel::IdRole).toUuid();
        peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
            {"type", "error"}, {"jobId", submission.value("jobId")},
            {"code", "generation-failed"}, {"message", "Identity edit failed"}}).toJson()));
        QTRY_COMPARE(history->model()->state(identityTile), GenerationHistoryModel::State::Failed);
        const auto completedJob = submission.value("jobId");
        workspace->findChild<Ardoise::PlainTextEdit*>("imageEditPrompt")->setPlainText("Make the sky green");
        workspace->findChild<QPushButton*>("imageEditButton")->click();
        QTRY_VERIFY(submission.value("jobId") != completedJob);
        const QUuid failedTile = history->model()->index(3).data(GenerationHistoryModel::IdRole).toUuid();
        peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
            {"type", "error"}, {"jobId", submission.value("jobId")},
            {"code", "generation-failed"}, {"message", "ErrorOutOfDeviceMemory"}}).toJson()));
        // Failures turn the tile into a failed tile carrying the message; the pill stays out of it.
        QTRY_COMPARE(history->model()->state(failedTile), GenerationHistoryModel::State::Failed);
        QVERIFY(history->model()->index(3).data(GenerationHistoryModel::StatusRole).toString()
                    .contains("ErrorOutOfDeviceMemory"));
        QVERIFY(workspace->activity()->state() != JobActivityIndicator::State::Failed);
        QCOMPARE(workspace->currentImage(), edited);
        peer->close();
        QTRY_VERIFY(!client->isAuthenticated());
        QCOMPARE(history->model()->rowCount(), 4); // Failed tiles stay until removed.
        QCOMPARE(workspace->currentImage().size(), QSize(128, 96));
    }

    void explicitModelPreferencesSaveAndCancel()
    {
        CanvasServiceController client;
        QVERIFY(!client.allowsExplicitModels());
        {
            ServicePreferencesDialog dialog(&client);
            auto* checkbox = dialog.findChild<QCheckBox*>("allowExplicitModels");
            QVERIFY(checkbox);
            QVERIFY(!checkbox->isChecked());
            checkbox->setChecked(true);
            dialog.reject();
        }
        QVERIFY(!client.allowsExplicitModels());
        {
            ServicePreferencesDialog dialog(&client);
            dialog.findChild<QCheckBox*>("allowExplicitModels")->setChecked(true);
            dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
        QVERIFY(client.allowsExplicitModels());
        CanvasServiceController restored;
        QVERIFY(restored.allowsExplicitModels());
    }

    void retiredLocalStylesDoNotAffectRequests()
    {
        LocalStyleData data;
        data.overrides.append({"server", "Override", "override positive", "override negative"});
        QString error;
        QVERIFY(saveLocalStyles("visible/model", data, &error));
        GenerationInspectorWidget panel;
        Nukleus::ModelInfo model; model.id = "visible/model";
        model.category = Nukleus::ModelCategory::ImageGeneration;
        model.styles = {{"server", "Server", "server positive", "server negative"}};
        panel.parameters()->setModels({model});
        panel.findChild<PromptEditorWidget *>("positivePrompt")->setText("a landscape");
        panel.findChild<PromptEditorWidget *>("negativePrompt")->setText("blur");
        QCOMPARE(panel.generationRequest().prompt, QString("a landscape"));
        QCOMPARE(panel.generationRequest().negativePrompt, QString("blur"));
        QVERIFY(panel.generationRequest().styleId.isEmpty());
        QVERIFY(!panel.findChild<QComboBox *>("generationStyle"));
        QVERIFY(!panel.findChild<QToolButton *>("editLocalStyles"));
        QVERIFY(!loadLocalStyles(model.id).overrides.isEmpty());
    }



    void oldServiceRequiresUpgradeForWorkspace()
    {
        QWebSocketServer server("old-service", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            auto *peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, peer, [peer](const QString &text) {
                const auto message = QJsonDocument::fromJson(text.toUtf8()).object();
                if (message.value("type") == "authenticate") {
                    peer->sendTextMessage("{\"type\":\"authenticated\"}");
                    peer->sendTextMessage("{\"type\":\"capabilities\",\"state\":\"ready\",\"operations\":[\"image.generate\"]}");
                    peer->sendTextMessage("{\"type\":\"models.list.result\",\"categories\":[{\"category\":\"image-generation\",\"models\":[{\"id\":\"old-model\",\"displayName\":\"Old model\"}]}]}");
                }
            });
        });
        MainWindow window;
        auto *client = window.findChild<CanvasServiceController *>();
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        client->connectToService();
        QTRY_VERIFY(client->modelListAvailable());
        QVERIFY(client->canGenerate("old-model")); // Direct model workflow remains eligible.
        QVERIFY(!window.findChild<QPushButton *>("generateButton")->isEnabled());
        QVERIFY(window.findChild<QComboBox *>("generationPreset")->currentText().contains("Upgrade"));
    }

    void detectsPromptLanguage()
    {
        using PromptInstructions::Language;
        using PromptInstructions::detectLanguage;
        QCOMPARE(detectLanguage("une femme avec des taches de rousseur, cheveux roux"), Language::French);
        QCOMPARE(detectLanguage("Ajoute un chapeau"), Language::French);
        QCOMPARE(detectLanguage("l'homme âgé"), Language::French);
        QCOMPARE(detectLanguage("a woman with freckles and red hair"), Language::English);
        QCOMPARE(detectLanguage("Add a hat on the head"), Language::English);
        QCOMPARE(detectLanguage("portrait"), Language::Unknown);
        QCOMPARE(detectLanguage("blurry"), Language::Unknown);
        // Weights the source never had are removed; existing ones and ratios are kept.
        QCOMPARE(PromptInstructions::withoutAddedWeights("une femme, plage",
                     "une belle jeune femme:1.2, assise sur le sable:1.2, (à la plage:1.3), elle:1.2"),
            QString("une belle jeune femme, assise sur le sable, à la plage, elle"));
        QCOMPARE(PromptInstructions::withoutAddedWeights("(chat:1.2)", "(chat roux:1.2)"), QString("(chat roux:1.2)"));
        QCOMPARE(PromptInstructions::withoutAddedWeights("format", "format 16:9, 4.5 m"), QString("format 16:9, 4.5 m"));
        for (const auto language : {Language::French, Language::English, Language::Unknown})
            QVERIFY(!PromptInstructions::improvement(PromptInstructions::Kind::Image, language).contains(":1.2"));
        QVERIFY(!PromptInstructions::translation().contains(":1.2"));
        // Dropped content words are detected; agreement and conjugation still count as kept.
        QCOMPARE(PromptInstructions::missingWords(
            "Une belle jeune femme assise sur la sable a la plage portant un micro bikini rouge, elle a les cheveux "
            "mi-long blond avec quelques mèches noire. Il y a un ballon de plage à côté d'elle. Elle regarde la "
            "caméra tandis qu'il y a un couché de soleil derrière elle.",
            "Une belle jeune femme assise sur le sable, portant un micro bikini rouge, cheveux mi-long blond avec "
            "quelques mèches noires, un ballon à côté d'elle, regardant la caméra, un couché de soleil derrière elle."),
            QStringList{"plage"});
        QVERIFY(PromptInstructions::missingWords("une femme", "une femme adulte").isEmpty());
        // Made-up people, body parts, colours and clothing are detected in the source's language.
        QCOMPARE(PromptInstructions::inventedWords("Rends l'image légèrement plus sombre",
                     "Rends l'image légèrement plus sombre, en gardant la luminosité bleue des yeux de l'homme"),
            (QStringList{"bleue", "yeux", "homme"}));
        QVERIFY(PromptInstructions::inventedWords("Un chat vu de face", "Un chat, vu de face").isEmpty());
        QVERIFY(PromptInstructions::inventedWords("une femme aux yeux bleus", "une femme, yeux bleu clair").isEmpty());
        QCOMPARE(PromptInstructions::inventedWords("a cat", "a cat with green eyes"), (QStringList{"green", "eyes"}));
        // Framing and image quality belong to the application, not to the improved prompt.
        QCOMPARE(PromptInstructions::inventedWords("une femme prenant une douche dans la salle de bain",
                     "une femme prenant une douche, arrière-plan de salle de bain, éclairage doux, cadrage plein plan"),
            QStringList{"cadrage"});
        QCOMPARE(PromptInstructions::inventedWords("a cat on a sofa", "a cat on a sofa, wide shot, high quality"),
            (QStringList{"shot", "quality"}));
        QVERIFY(PromptInstructions::inventedWords("une femme regarde la caméra", "une femme regardant la caméra")
                    .isEmpty());
        // An image prompt may add visible details and clothing, but no people and no framing.
        const QString shower = "une jolie jeune femme nue prenant une douche";
        QVERIFY(PromptInstructions::inventedWords(shower,
            "une jolie jeune femme nue prenant une douche, cheveux mouillés, mains dans les cheveux, peau perlée", false)
                    .isEmpty());
        QCOMPARE(PromptInstructions::inventedWords(shower, "une jolie jeune femme nue et un homme, cadrage serré", false),
            (QStringList{"homme", "cadrage"}));
        // The composition is context for image prompts only: the shot and the format, never for Free alone.
        using Composition = PromptInstructions::Composition;
        QVERIFY(PromptInstructions::compositionContext({}, Language::French).isEmpty());
        const Composition wide{ShotPicker::Shot::Wide, QSize(1344, 768), QStringLiteral("16:9")};
        const QString wideFrench = PromptInstructions::improvement(PromptInstructions::Kind::Image, Language::French,
            {}, {}, wide);
        QVERIFY(wideFrench.contains("l'environnement occupe presque toute l'image ; l'image est horizontale (16:9)"));
        QVERIFY(PromptInstructions::improvement(PromptInstructions::Kind::Image, Language::English, {}, {},
            {ShotPicker::Shot::Free, QSize(832, 1216), QStringLiteral("2:3")})
                    .contains("Image context, never to be written in your answer: the image is vertical (2:3)."));
        QVERIFY(!PromptInstructions::improvement(PromptInstructions::Kind::Avoid, Language::French, {}, {}, wide)
                     .contains("Contexte"));
        // The image model's prompt format sets the shape of an image prompt, phrases by default.
        using Format = PromptInstructions::Format;
        QCOMPARE(PromptInstructions::formatFromName("natural"), Format::Natural);
        QCOMPARE(PromptInstructions::formatFromName("tags"), Format::Tags);
        QCOMPARE(PromptInstructions::formatFromName(QString()), Format::Phrases);
        const auto image = [](Language language, Format format) {
            return PromptInstructions::improvement(PromptInstructions::Kind::Image, language, {}, {},
                {ShotPicker::Shot::Free, {}, {}, format});
        };
        QVERIFY(image(Language::French, Format::Phrases).contains("Structure : des groupes de mots descriptifs"));
        QVERIFY(image(Language::French, Format::Natural).contains("Structure : des phrases descriptives fluides"));
        QVERIFY(image(Language::English, Format::Natural).contains("Structure: fluent, natural descriptive sentences"));
        QVERIFY(image(Language::English, Format::Phrases).contains("Structure: comma-separated descriptive phrases in"));
        // Tag models get English Danbooru tags for both generation prompts, whatever the text's language,
        // with the visible pose parts and the composition; other kinds keep their own instructions.
        const QString tags = PromptInstructions::improvement(PromptInstructions::Kind::Image, Language::French, {},
            {"masterpiece"}, {ShotPicker::Shot::Cowboy, QSize(832, 1216), QStringLiteral("2:3"), Format::Tags});
        QVERIFY(tags.contains("Answer only with English Danbooru tags"));
        QVERIFY(tags.contains("subject count tags") && tags.contains("never dress a nude subject"));
        QVERIFY(tags.contains("the hips and the legs") && tags.contains("the image is vertical (2:3)"));
        QVERIFY(tags.contains("do not write masterpiece.") && tags.endsWith("\n\nText:"));
        QVERIFY(!tags.contains("français"));
        const QString avoidTags = PromptInstructions::improvement(PromptInstructions::Kind::Avoid, Language::French,
            {}, {}, {ShotPicker::Shot::Free, {}, {}, Format::Tags});
        QVERIFY(avoidTags.contains("negative prompt") && avoidTags.contains("English Danbooru tags"));
        QVERIFY(PromptInstructions::improvement(PromptInstructions::Kind::Edit, Language::French, {}, {},
            {ShotPicker::Shot::Free, {}, {}, Format::Tags}).contains("Réponds uniquement en français"));
        QVERIFY(PromptInstructions::danbooruTranslation().contains("Be faithful")
            && PromptInstructions::danbooruTranslation().contains("English Danbooru tags"));
        // A tag answer is English, so only framing and quality tags the source never asked for count.
        QCOMPARE(PromptInstructions::inventedTagWords("une fille debout dans un jardin",
                     "1girl, solo, standing, garden, cowboy shot, masterpiece"), (QStringList{"shot", "masterpiece"}));
        QVERIFY(PromptInstructions::inventedTagWords("portrait d'une femme", "1girl, portrait").isEmpty());
        // Loops are refused; a subject named a few times is not one.
        QVERIFY(!PromptInstructions::repeatsItself("la vieille dame, la vieille dame, la vieille dame, la vieille "
                                                   "dame, la vieille dame tricote"));
        QVERIFY(PromptInstructions::repeatsItself(QString("bonnes ").repeated(10)));
        // Image prompts pin down the pose of the parts the shot shows, and only those.
        QVERIFY(wideFrench.contains("la posture d'ensemble (debout, assis, couché, à genoux, etc.), la position de la "
                                    "tête et la direction du regard, le torse, les bras et les mains, les jambes et "
                                    "les pieds."));
        const QString portrait = PromptInstructions::improvement(PromptInstructions::Kind::Image, Language::English,
            {}, {}, {ShotPicker::Shot::Portrait, {}, {}});
        QVERIFY(portrait.contains("part by part, skipping what does not apply: the position of the head and the "
                                  "direction of the gaze, the shoulders."));
        QVERIFY(!portrait.contains("feet"));
        QVERIFY(PromptInstructions::poseGuidance(ShotPicker::Shot::Cowboy, Language::English)
                    .contains("the hips and the legs."));
        QVERIFY(PromptInstructions::poseGuidance(ShotPicker::Shot::ExtremeCloseUp, Language::French).isEmpty());
        QVERIFY(!PromptInstructions::improvement(PromptInstructions::Kind::Edit, Language::French).contains("posture"));
        for (const auto language : {Language::French, Language::English}) {
            const QString image = PromptInstructions::improvement(PromptInstructions::Kind::Image, language);
            QVERIFY(!image.contains("cadrage.") && !image.contains("framing."));
            QVERIFY(image.contains(language == Language::French ? "aucun terme de cadrage" : "Add no framing"));
        }
        QVERIFY(PromptInstructions::improvement(PromptInstructions::Kind::Image, Language::French, {"plage"})
                    .contains("doit aussi contenir plage."));
        // Each language gets instructions written in it.
        QVERIFY(PromptInstructions::improvement(PromptInstructions::Kind::Face, Language::French)
                    .contains("Réponds uniquement en français"));
        QVERIFY(PromptInstructions::improvement(PromptInstructions::Kind::Face, Language::English)
                    .contains("Answer only in English"));
    }

    void sparklesImprovePromptsInPlace()
    {
        const auto originalLocale = QLocale();
        const auto restoreLocale = qScopeGuard([&] { QLocale::setDefault(originalLocale); });
        QLocale::setDefault(QLocale("en_US"));
        QWebSocketServer server("sparkles-test", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QWebSocket* peer = nullptr;
        QList<QJsonObject> submissions;
        auto routing = Nukleus::Protocol::starterModelRouting();
        for (auto& preset : routing.presets) preset.model.modelId = "image-model";
        bool withEnhancer = false;
        const auto sendModels = [&] {
            QJsonArray llms{QJsonObject{{"id", "llm-chat:translator"}, {"displayName", "Translator"},
                {"recommendedOptions", QJsonObject{{"defaultPromptTranslator", true}}}}};
            if (withEnhancer)
                llms.append(QJsonObject{{"id", "llm-chat:enhancer"}, {"displayName", "Enhancer"},
                    {"recommendedOptions", QJsonObject{{"defaultPromptEnhancer", true}}}});
            peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "models.list.result"},
                {"categories", QJsonArray{QJsonObject{{"category", "image-generation"},
                    {"models", QJsonArray{QJsonObject{{"id", "image-model"}, {"displayName", "Image"},
                        {"recommendedOptions", QJsonObject{{"promptFormat", "natural"}}}}}}},
                    QJsonObject{{"category", "llm-chat"}, {"models", llms}}}}}).toJson()));
        };
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this, [&](const QString& text) {
                const auto request = QJsonDocument::fromJson(text.toUtf8()).object();
                if (request.value("type") == "authenticate") {
                    peer->sendTextMessage("{\"type\":\"authenticated\"}");
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "capabilities"}, {"state", "ready"},
                        {"operations", QJsonArray{"image.generate", "image.edit", "model-routing", "prompt.enhance"}}}).toJson()));
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "model-routing.get.completed"},
                        {"routing", Nukleus::Protocol::modelRoutingToJson(routing)}}).toJson()));
                }
                if (request.value("type") == "models.list") sendModels();
                if (request.value("type") == "submit") submissions.append(request);
            });
        });
        MainWindow window;
        auto* client = window.findChild<CanvasServiceController*>();
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        client->connectToService();
        auto* positive = window.findChild<PromptEditorWidget*>("positivePrompt");
        auto* negative = window.findChild<PromptEditorWidget*>("negativePrompt");
        auto* assist = window.findChild<QToolButton*>("promptAssistButton");
        auto* editImprove = window.findChild<QToolButton*>("imageEditImprove");
        auto* editPrompt = window.findChild<Ardoise::PlainTextEdit*>("imageEditPrompt");
        auto* activity = window.findChild<GenerationWorkspaceWidget*>()->activity();
        QVERIFY(assist && editImprove && editPrompt);
        positive->setText("Un chat roux assis vu de face");
        // Without an enhancer (the translator does not count), Sparkles explains where to set one.
        QTRY_VERIFY(client->modelListAvailable());
        QVERIFY(!assist->isEnabled());
        QVERIFY(assist->toolTip().contains("Service Manager"));
        withEnhancer = true;
        sendModels();
        QTRY_VERIFY(assist->isEnabled());
        const auto reply = [&](const QJsonObject& message) {
            peer->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));
        };
        // Instructions are written in the text's language, name it, and give the kind's structure.
        const auto improveNext = [&](const QString& source, const QString& improved, const QStringList& expected,
                                     const QStringList& unexpected = {}) {
            [&] { QTRY_COMPARE(submissions.size(), 1); }();
            const auto request = submissions.takeFirst();
            const auto options = request.value("options").toObject();
            [&] {
                QCOMPARE(request.value("operation").toString(), QString("prompt.enhance"));
                QCOMPARE(options.value("modelId").toString(), QString("llm-chat:enhancer"));
                QCOMPARE(options.value("prompt").toString(), source);
                const auto parameters = options.value("parameters").toObject();
                QCOMPARE(parameters.value("penalizeRepeats").toBool(true), false);
                // The enhancer's saved temperature applies.
                QVERIFY(!parameters.contains("temperature"));
                const auto system = parameters.value("systemPrompt").toString();
                for (const auto& part : expected) QVERIFY2(system.contains(part), qPrintable(part));
                for (const auto& part : unexpected) QVERIFY2(!system.contains(part), qPrintable(part));
            }();
            reply({{"type", "result"}, {"jobId", request.value("jobId")}, {"operation", "prompt.enhance"},
                   {"prompt", improved}});
        };
        const QStringList french{"N'invente rien", "Réponds uniquement en français", "Texte :"};
        const QStringList english{"Do not invent anything", "Answer only in English", "Text:"};
        // Image prompts may be enriched with visible details, never with people, and keep described clothing.
        const QStringList frenchImage{"Tu peux enrichir l'image", "Respecte toujours les vêtements ou la nudité",
            "aucune autre personne", "Réponds uniquement en français", "Texte :"};
        // Generation: both written prompts are improved in place; a blank one stays blank.
        // The image prompt learns the selected shot as context for where details matter, and is written
        // in the preset model's prompt format (natural sentences here).
        auto* shot = window.findChild<ShotPicker*>("shotPicker");
        shot->setCurrentShot(ShotPicker::Shot::CloseUp);
        assist->click();
        QCOMPARE(activity->title(), QString("Improving the prompts"));
        QVERIFY(!window.findChild<QPushButton*>("generateButton")->isEnabled()); // One job at a time.
        improveNext("Un chat roux assis vu de face", "Un chat roux, assis, vu de face",
            frenchImage + QStringList{"prompt de génération d'image", "des phrases descriptives fluides", "Contexte de l'image",
                "seule la tête entière du sujet, avec le cou, remplit l'image", "N'ajoute aucune mention du cadrage"},
            {"N'invente rien"});
        shot->setCurrentShot(ShotPicker::Shot::Free);
        QTRY_COMPARE(positive->text(), QString("Un chat roux, assis, vu de face"));
        QVERIFY(negative->text().isEmpty());
        QCOMPARE(activity->state(), JobActivityIndicator::State::Succeeded);
        QVERIFY(submissions.isEmpty());
        negative->setText("blurry");
        QTRY_VERIFY(assist->isEnabled());
        assist->click();
        improveNext("Un chat roux, assis, vu de face", "Un chat roux assis, vu de face, pelage détaillé", frenchImage);
        // A single tag has no clear language: the instructions ask to keep the text's own language.
        improveNext("blurry", "blurry, out of focus", {"things to avoid", "never translate it"}, {"Image context"});
        QTRY_COMPARE(negative->text(), QString("blurry, out of focus"));
        QCOMPARE(positive->text(), QString("Un chat roux assis, vu de face, pelage détaillé"));
        // A failure keeps the editors and reports in the pill.
        QTRY_VERIFY(assist->isEnabled());
        assist->click();
        QTRY_COMPARE(submissions.size(), 1);
        reply({{"type", "error"}, {"jobId", submissions.takeFirst().value("jobId")},
               {"code", "prompt-enhancement-failed"}, {"message", "LLM failed"}});
        QTRY_COMPARE(activity->state(), JobActivityIndicator::State::Failed);
        QCOMPARE(activity->title(), QString("Prompt improvement failed"));
        QCOMPARE(negative->text(), QString("blurry, out of focus"));
        QTRY_VERIFY(!client->jobActive());
        // An answer translated into another language is refused and the draft is kept.
        QTRY_VERIFY(assist->isEnabled());
        assist->click();
        improveNext("Un chat roux assis, vu de face, pelage détaillé",
            "A ginger cat sitting, front view, with detailed fur and the eyes on the viewer", frenchImage);
        QTRY_COMPARE(activity->state(), JobActivityIndicator::State::Failed);
        QVERIFY(activity->status().contains("another language"));
        QCOMPARE(positive->text(), QString("Un chat roux assis, vu de face, pelage détaillé"));
        QVERIFY(submissions.isEmpty());
        QTRY_VERIFY(!client->jobActive());
        // A dropped element triggers one retry that names it; the retry's answer is used.
        negative->setText({});
        positive->setText("Une femme assise sur le sable à la plage");
        QTRY_VERIFY(assist->isEnabled());
        assist->click();
        improveNext("Une femme assise sur le sable à la plage", "Une femme assise sur le sable", frenchImage);
        improveNext("Une femme assise sur le sable à la plage", "Une jeune femme assise sur le sable, à la plage",
            {"doit aussi contenir plage."});
        QTRY_COMPARE(positive->text(), QString("Une jeune femme assise sur le sable, à la plage"));
        QTRY_VERIFY(!client->jobActive());
        // Still dropped after the retry: the job fails, naming it, and the draft is kept.
        positive->setText("Une femme assise sur le sable à la plage");
        QTRY_VERIFY(assist->isEnabled());
        assist->click();
        improveNext("Une femme assise sur le sable à la plage", "Une femme assise sur le sable", frenchImage);
        improveNext("Une femme assise sur le sable à la plage", "Une femme assise sur le sable fin", frenchImage);
        QTRY_COMPARE(activity->state(), JobActivityIndicator::State::Failed);
        QVERIFY(activity->status().contains("plage"));
        QCOMPARE(positive->text(), QString("Une femme assise sur le sable à la plage"));
        QVERIFY(submissions.isEmpty());
        QTRY_VERIFY(!client->jobActive());
        // An answer caught in a loop is retried once, then refused; the draft is kept.
        QString loop = "Une femme assise sur le sable à la plage";
        for (int i = 0; i < 8; ++i) loop += ", un bon endroit pour lire";
        QTRY_VERIFY(assist->isEnabled());
        assist->click();
        improveNext("Une femme assise sur le sable à la plage", loop, frenchImage);
        improveNext("Une femme assise sur le sable à la plage", loop, frenchImage);
        QTRY_COMPARE(activity->state(), JobActivityIndicator::State::Failed);
        QVERIFY(activity->status().contains("loop"));
        QCOMPARE(positive->text(), QString("Une femme assise sur le sable à la plage"));
        QVERIFY(submissions.isEmpty());
        QTRY_VERIFY(!client->jobActive());
        // Edit: the instruction needs text, then is improved in place.
        QVERIFY(!editImprove->isEnabled());
        editPrompt->setPlainText("Ajoute un chapeau");
        QTRY_VERIFY(editImprove->isEnabled());
        editImprove->click();
        QCOMPARE(activity->title(), QString("Improving the instruction"));
        improveNext("Ajoute un chapeau", "Ajoute un chapeau de paille sur la tête",
            french + QStringList{"instruction de modification", "que tu ne vois pas"});
        QTRY_COMPARE(editPrompt->toPlainText(), QString("Ajoute un chapeau de paille sur la tête"));
        QTRY_VERIFY(!client->jobActive());
        // A made-up subject (the reported case) triggers one retry naming it, then is refused.
        const QString darker = "Rends l'image légèrement plus sombre";
        const QString invention = "Rends l'image légèrement plus sombre, tout en gardant la luminosité bleue des yeux de l'homme";
        editPrompt->setPlainText(darker);
        QTRY_VERIFY(editImprove->isEnabled());
        editImprove->click();
        improveNext(darker, invention, french);
        improveNext(darker, "Assombris légèrement toute l'image", {"n'écris pas bleue, yeux, homme."});
        QTRY_COMPARE(editPrompt->toPlainText(), QString("Assombris légèrement toute l'image"));
        QTRY_VERIFY(!client->jobActive());
        editPrompt->setPlainText(darker);
        QTRY_VERIFY(editImprove->isEnabled());
        editImprove->click();
        improveNext(darker, invention, french);
        improveNext(darker, invention, french);
        QTRY_COMPARE(activity->state(), JobActivityIndicator::State::Failed);
        QVERIFY(activity->status().contains("yeux"));
        QCOMPARE(editPrompt->toPlainText(), darker);
        QTRY_VERIFY(!client->jobActive());
        // Identity: face and hair descriptions are improved in the panel, with its own pill.
        window.findChild<QAction*>("createPromptIdentityAction")->trigger();
        auto* identity = window.findChild<IdentityPanel*>();
        auto* identityImprove = identity->findChild<QToolButton*>("identityImprove");
        auto* face = identity->findChild<QPlainTextEdit*>("identityPrompt");
        auto* hair = identity->findChild<QPlainTextEdit*>("identityHairPrompt");
        QTRY_VERIFY(!client->jobActive());
        QVERIFY(!identityImprove->isEnabled());
        hair->setPlainText("red hair");
        QTRY_VERIFY(identityImprove->isEnabled());
        face->setPlainText("une femme");
        identityImprove->click();
        QCOMPARE(identity->activity()->title(), QString("Improving the descriptions"));
        improveNext("une femme", "une femme adulte", french + QStringList{"forme du visage"});
        improveNext("red hair", "long straight red hair", english + QStringList{"length, colour, texture"});
        QTRY_COMPARE(face->toPlainText(), QString("une femme adulte"));
        QCOMPARE(hair->toPlainText(), QString("long straight red hair"));
        QTRY_COMPARE(identity->activity()->state(), JobActivityIndicator::State::Succeeded);
        QVERIFY(submissions.isEmpty());
    }

    void tagModelsGetDanbooruTags()
    {
        const auto originalLocale = QLocale();
        const auto restoreLocale = qScopeGuard([&] { QLocale::setDefault(originalLocale); });
        QLocale::setDefault(QLocale("en_US"));
        QWebSocketServer server("danbooru-test", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QWebSocket* peer = nullptr;
        QList<QJsonObject> submissions;
        auto routing = Nukleus::Protocol::starterModelRouting();
        for (auto& preset : routing.presets) preset.model.modelId = "image-model";
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this, [&](const QString& text) {
                const auto request = QJsonDocument::fromJson(text.toUtf8()).object();
                if (request.value("type") == "authenticate") {
                    peer->sendTextMessage("{\"type\":\"authenticated\"}");
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "capabilities"}, {"state", "ready"},
                        {"operations", QJsonArray{"image.generate", "image.edit", "model-routing", "prompt.enhance"}}}).toJson()));
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "model-routing.get.completed"},
                        {"routing", Nukleus::Protocol::modelRoutingToJson(routing)}}).toJson()));
                }
                if (request.value("type") == "models.list")
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "models.list.result"},
                        {"categories", QJsonArray{QJsonObject{{"category", "image-generation"},
                            {"models", QJsonArray{QJsonObject{{"id", "image-model"}, {"displayName", "Illustrious"},
                                {"recommendedOptions", QJsonObject{{"promptFormat", "tags"}}}}}}},
                            QJsonObject{{"category", "llm-chat"}, {"models", QJsonArray{QJsonObject{
                                {"id", "llm-chat:llm"}, {"displayName", "LLM"}, {"recommendedOptions",
                                    QJsonObject{{"defaultPromptEnhancer", true}, {"defaultPromptTranslator", true}}}}}}}}}}).toJson()));
                if (request.value("type") == "submit") submissions.append(request);
            });
        });
        MainWindow window;
        auto* client = window.findChild<CanvasServiceController*>();
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        client->connectToService();
        auto* positive = window.findChild<PromptEditorWidget*>("positivePrompt");
        auto* negative = window.findChild<PromptEditorWidget*>("negativePrompt");
        auto* assist = window.findChild<QToolButton*>("promptAssistButton");
        auto* generate = window.findChild<QPushButton*>("generateButton");
        const auto answerNext = [&](const QString& source, const QString& answer, const QString& instruction) {
            [&] { QTRY_COMPARE(submissions.size(), 1); }();
            const auto request = submissions.takeFirst();
            const auto options = request.value("options").toObject();
            [&] {
                QCOMPARE(request.value("operation").toString(), QString("prompt.enhance"));
                QCOMPARE(options.value("prompt").toString(), source);
                const auto system = options.value("parameters").toObject().value("systemPrompt").toString();
                QVERIFY2(system.contains("English Danbooru tags") && system.contains(instruction), qPrintable(system));
            }();
            peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "result"},
                {"jobId", request.value("jobId")}, {"operation", "prompt.enhance"}, {"prompt", answer}}).toJson()));
        };
        // Sparkles turns French ideas into English tags: no language or kept-words failure.
        positive->setText("une fille nue assise sur un rocher");
        negative->setText("flou");
        QTRY_VERIFY(assist->isEnabled());
        assist->click();
        answerNext("une fille nue assise sur un rocher", "1girl, solo, nude, sitting, on rock, looking at viewer",
            "subject count tags");
        answerNext("flou", "blurry", "negative prompt");
        QTRY_COMPARE(positive->text(), QString("1girl, solo, nude, sitting, on rock, looking at viewer"));
        QCOMPARE(negative->text(), QString("blurry"));
        // Generate converts both prompts with the faithful Danbooru translator instead of translating them.
        QTRY_VERIFY(generate->isEnabled());
        generate->click();
        answerNext("1girl, solo, nude, sitting, on rock, looking at viewer",
            "1girl, solo, nude, sitting, on rock, looking at viewer", "Be faithful");
        answerNext("blurry", "blurry", "Be faithful");
        QTRY_COMPARE(submissions.size(), 1);
        const auto options = submissions.takeFirst().value("options").toObject();
        QCOMPARE(options.value("prompt").toString(), QString("1girl, solo, nude, sitting, on rock, looking at viewer"));
        QCOMPARE(options.value("negativePrompt").toString(), QString("blurry"));
        // An invented framing tag gets one retry naming it, then the draft is kept.
        positive->setText("un chat");
        negative->setText(QString());
        QTRY_VERIFY(assist->isEnabled());
        assist->click();
        answerNext("un chat", "no humans, cat, cowboy shot", "subject count tags");
        answerNext("un chat", "no humans, cat, cowboy shot", "do not write shot.");
        QTRY_VERIFY(assist->isEnabled());
        QCOMPARE(positive->text(), QString("un chat"));
        // With the user's Danbooru dictionary (tags of at least 100 posts), tag answers take their
        // Danbooru spelling, unknown tags get one retry, and known tags skip the translator.
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QVERIFY(QDir().mkpath(folder));
        QFile csv(folder + "/danbooru.csv");
        const auto removeCsv = qScopeGuard([&] { csv.remove(); });
        QVERIFY(csv.open(QIODevice::WriteOnly));
        csv.write("1girl,0,6008644,\"1girls,sole_female\"\nsolo,0,5000954,\nsitting,0,900000,\n"
                  "looking_at_viewer,0,3000000,\"looking_at_the_viewer\"\nhand_on_own_hip,0,153000,\n"
                  "rare_tag,0,50,\n");
        csv.close();
        // The cowboy shot gives Sparkles the visible pose vocabulary, and the generation Danbooru framing tags.
        auto* shot = window.findChild<ShotPicker*>("shotPicker");
        shot->setCurrentShot(ShotPicker::Shot::Cowboy);
        positive->setText("une fille assise");
        QTRY_VERIFY(assist->isEnabled());
        assist->click();
        answerNext("une fille assise", "1girls, solo, Sitting, rock seat, hand on the hip",
            "arms and hands: hand up");
        // Unknown tags come back with their closest existing tags.
        answerNext("une fille assise", "1girls, solo, sitting, looking at the viewer, hand on own hip",
            "replace rock seat; hand on the hip (hand on own hip) with existing Danbooru tags. In parentheses");
        QTRY_COMPARE(positive->text(), QString("1girl, solo, sitting, looking at viewer, hand on own hip"));
        QTRY_VERIFY(generate->isEnabled());
        generate->click();
        QTRY_COMPARE(submissions.size(), 1);
        auto submission = submissions.takeFirst();
        QCOMPARE(submission.value("operation").toString(), QString("image.generate"));
        QCOMPARE(submission.value("options").toObject().value("prompt").toString(),
            QString("cowboy shot, 1girl, solo, sitting, looking at viewer, hand on own hip"));
        QVERIFY(submission.value("options").toObject().value("negativePrompt").toString().startsWith("full body, "));
        shot->setCurrentShot(ShotPicker::Shot::Free);
        // A tag below 100 posts is unknown, so the prompt goes through the Danbooru translator.
        positive->setText("1girl, rare tag");
        QTRY_VERIFY(generate->isEnabled());
        generate->click();
        answerNext("1girl, rare tag", "1girl, rare tag", "Be faithful");
        QTRY_COMPARE(submissions.size(), 1);
        submission = submissions.takeFirst();
        QCOMPARE(submission.value("operation").toString(), QString("image.generate"));
        QCOMPARE(submission.value("options").toObject().value("prompt").toString(), QString("1girl, rare tag"));
    }

    void danbooruDictionaryNormalizesTags()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile csv(directory.filePath("danbooru.csv"));
        QVERIFY(csv.open(QIODevice::WriteOnly));
        csv.write("1girl,0,6008644,\"1girls,sole_female\"\nlong_hair,0,4350743,\"/lh,longhair\"\n"
                  "thea_(spy_kyoushitsu),4,25,\nsolo,0,5000954,\"long_hair\"\nnot,a,line\n");
        csv.close();
        DanbooruDictionary dictionary;
        QString error;
        QVERIFY(!dictionary.load(directory.filePath("missing.csv"), 0, &error) && !error.isEmpty());
        QVERIFY(dictionary.load(csv.fileName()));
        QCOMPARE(dictionary.size(), 4); // Malformed lines are skipped.
        // Any spelling or alias finds the tag; "/lh" shortcuts are not aliases, and an alias never
        // shadows a real tag.
        QCOMPARE(dictionary.find("Long Hair")->name, QString("long_hair"));
        QCOMPARE(dictionary.find("longhair")->name, QString("long_hair"));
        QCOMPARE(dictionary.find("long_hair")->name, QString("long_hair"));
        QVERIFY(!dictionary.find("/lh"));
        QCOMPARE(dictionary.find("thea \\(spy kyoushitsu\\)")->category, int(DanbooruDictionary::Character));
        QCOMPARE(dictionary.find("1girl")->posts, qint64(6008644));
        // Prompt form: spaces, escaped parentheses, kept weights; unknown tags stay as written.
        const auto result = dictionary.normalize(" 1girls ,(longhair:1.2), (solo), thea_(spy_kyoushitsu),, rock seat");
        QCOMPARE(result.prompt, QString("1girl, (long hair:1.2), (solo), thea \\(spy kyoushitsu\\), rock seat"));
        QCOMPARE(result.unknown, QStringList{"rock seat"});
        // Rare tags can be left out at load time.
        QVERIFY(dictionary.load(csv.fileName(), 100));
        QCOMPARE(dictionary.size(), 3);
        QVERIFY(!dictionary.find("thea_(spy_kyoushitsu)"));
        QVERIFY(!dictionary.load(csv.fileName(), 10000000)); // Nothing kept.
        // Suggestions: most words in common, then most posts, in the requested category only;
        // linking words and plural endings do not count.
        QFile closest(directory.filePath("closest.csv"));
        QVERIFY(closest.open(QIODevice::WriteOnly));
        closest.write("hand_on_own_hip,0,153000,\nhands_on_own_hips,0,30000,\nhand_on_another's_hip,0,800,\n"
                      "hand_up,0,350000,\nrock,0,9000,\non_rock,0,5000,\nrock_(character),4,90000,\n");
        closest.close();
        QVERIFY(dictionary.load(closest.fileName()));
        const auto names = [&](const QString& tag, int limit = 4) {
            QStringList result;
            for (const auto* found : dictionary.suggestions(tag, limit)) result.append(found->name);
            return result;
        };
        QCOMPARE(names("hand on the hip"),
            (QStringList{"hand_on_own_hip", "hands_on_own_hips", "hand_on_another's_hip", "hand_up"}));
        QCOMPARE(names("hand on the hip", 1), QStringList{"hand_on_own_hip"});
        QCOMPARE(names("rock seat"), (QStringList{"rock", "on_rock"}));
        QVERIFY(names("the of on").isEmpty());
    }

    void danbooruPoseVocabularyAndFramingFollowTheShot()
    {
        using Shot = ShotPicker::Shot;
        QVERIFY(PromptInstructions::danbooruPoseVocabulary(Shot::ExtremeCloseUp).isEmpty());
        const QString portrait = PromptInstructions::danbooruPoseVocabulary(Shot::Portrait);
        QVERIFY(portrait.contains("head and gaze: looking at viewer") && !portrait.contains("posture")
            && !portrait.contains("arms and hands"));
        const QString medium = PromptInstructions::danbooruPoseVocabulary(Shot::Medium);
        QVERIFY(medium.contains("posture: sitting") && medium.contains("arms and hands") && !medium.contains("; legs"));
        const QString cowboy = PromptInstructions::danbooruPoseVocabulary(Shot::Cowboy);
        QVERIFY(cowboy.contains("; legs: spread legs") && !cowboy.contains("tiptoes"));
        QVERIFY(PromptInstructions::danbooruPoseVocabulary(Shot::Free).contains("legs and feet:")
            && PromptInstructions::danbooruPoseVocabulary(Shot::FullBody).contains("crossed ankles"));
        // Only tag answers get it.
        const PromptInstructions::Composition composition{Shot::Free, {}, {}, PromptInstructions::Format::Tags};
        QVERIFY(PromptInstructions::improvement(PromptInstructions::Kind::Image, PromptInstructions::Language::French,
            {}, {}, composition).contains("Danbooru pose tags"));
        QVERIFY(!PromptInstructions::improvement(PromptInstructions::Kind::Avoid, PromptInstructions::Language::French,
            {}, {}, composition).contains("Danbooru pose tags"));
        QVERIFY(!PromptInstructions::improvement(PromptInstructions::Kind::Image, PromptInstructions::Language::French)
                     .contains("Danbooru"));
        // Tag models get Danbooru framing tags; other models keep the framing phrases.
        QCOMPARE(ShotPrompts::forShot(Shot::Cowboy, true).positive, QString("cowboy shot"));
        QCOMPARE(ShotPrompts::forShot(Shot::ExtremeCloseUp, true).positive, QString("close-up"));
        QVERIFY(ShotPrompts::forShot(Shot::Free, true).positive.isEmpty());
        QVERIFY(ShotPrompts::forShot(Shot::Cowboy).positive.startsWith("framed from the top of the head"));
        for (const auto shot : {Shot::ExtremeCloseUp, Shot::CloseUp, Shot::Portrait, Shot::Medium, Shot::Cowboy,
                 Shot::FullBody, Shot::Wide}) {
            const auto tags = ShotPrompts::forShot(shot, true);
            QVERIFY(!tags.positive.isEmpty() && !tags.negative.isEmpty());
            for (const auto& tag : tags.positive.split(", ")) QVERIFY(!tags.negative.contains(tag));
        }
    }

    void translatesGenerationAndEditPrompts()
    {
        const auto originalLocale = QLocale();
        const auto restoreLocale = qScopeGuard([&] { QLocale::setDefault(originalLocale); });
        QLocale::setDefault(QLocale("en_US"));
        QWebSocketServer server("translation-test", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QWebSocket* peer = nullptr;
        QList<QJsonObject> submissions;
        auto routing = Nukleus::Protocol::starterModelRouting();
        for (auto& preset : routing.presets) preset.model.modelId = "image-model";
        for (auto& quality : routing.editingQualities) quality.model.modelId = "image-model";
        bool withTranslator = false;
        const auto sendModels = [&] {
            QJsonArray llms{QJsonObject{{"id", "llm-chat:chat"}, {"displayName", "Chat"}}};
            if (withTranslator)
                llms.append(QJsonObject{{"id", "llm-chat:translator"}, {"displayName", "Translator"},
                    {"recommendedOptions", QJsonObject{{"defaultPromptTranslator", true}}}});
            peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "models.list.result"},
                {"categories", QJsonArray{QJsonObject{{"category", "image-generation"},
                    {"models", QJsonArray{QJsonObject{{"id", "image-model"}, {"displayName", "Image"}}}}},
                    QJsonObject{{"category", "llm-chat"}, {"models", llms}}}}}).toJson()));
        };
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this, [&](const QString& text) {
                const auto request = QJsonDocument::fromJson(text.toUtf8()).object();
                if (request.value("type") == "authenticate") {
                    peer->sendTextMessage("{\"type\":\"authenticated\"}");
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "capabilities"}, {"state", "ready"},
                        {"operations", QJsonArray{"image.generate", "image.edit", "model-routing", "prompt.enhance"}}}).toJson()));
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "model-routing.get.completed"},
                        {"routing", Nukleus::Protocol::modelRoutingToJson(routing)}}).toJson()));
                }
                if (request.value("type") == "models.list") sendModels();
                if (request.value("type") == "submit") submissions.append(request);
            });
        });
        MainWindow window;
        auto* client = window.findChild<CanvasServiceController*>();
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        client->connectToService();
        auto* positive = window.findChild<PromptEditorWidget*>("positivePrompt");
        auto* negative = window.findChild<PromptEditorWidget*>("negativePrompt");
        auto* generate = window.findChild<QPushButton*>("generateButton");
        auto* panel = window.findChild<GenerationInspectorWidget*>();
        auto* activity = window.findChild<GenerationWorkspaceWidget*>()->activity();
        positive->setText("Un chat");
        QTRY_VERIFY(generate->isEnabled());
        const auto reply = [&](const QJsonObject& message) {
            peer->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));
        };
        const auto translateNext = [&](const QString& source, const QString& english) {
            [&] { QTRY_COMPARE(submissions.size(), 1); }();
            const auto request = submissions.takeFirst();
            const auto options = request.value("options").toObject();
            [&] {
                QCOMPARE(request.value("operation").toString(), QString("prompt.enhance"));
                QCOMPARE(options.value("modelId").toString(), QString("llm-chat:translator"));
                QCOMPARE(options.value("prompt").toString(), source);
                const auto parameters = options.value("parameters").toObject();
                QVERIFY(parameters.value("systemPrompt").toString().contains("Output only the English translation"));
                QCOMPARE(parameters.value("temperature").toDouble(), 0.0);
                QCOMPARE(parameters.value("penalizeRepeats").toBool(true), false);
            }();
            reply({{"type", "result"}, {"jobId", request.value("jobId")}, {"operation", "prompt.enhance"},
                   {"prompt", english}});
        };
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG"));
        const auto finish = [&](const QJsonObject& request) {
            reply({{"type", "result"}, {"jobId", request.value("jobId")}, {"operation", request.value("operation")},
                   {"mimeType", "image/png"}, {"prompt", request.value("options").toObject().value("prompt")}});
            peer->sendBinaryMessage(png);
            [&] { QTRY_VERIFY(!client->jobActive() && client->queuedGenerationCount() == 0); }();
        };
        auto* history = window.findChild<GenerationHistoryView*>()->model();
        const auto lastTile = [&](int role) { return history->index(history->rowCount() - 1, 0).data(role); };
        // Without a translator, the prompts are sent unchanged.
        generate->click();
        QTRY_COMPARE(submissions.size(), 1);
        auto submission = submissions.takeFirst();
        auto options = submission.value("options").toObject();
        QCOMPARE(submission.value("operation").toString(), QString("image.generate"));
        QCOMPARE(options.value("prompt").toString(), QString("Un chat"));
        QVERIFY(!options.contains("skipPromptFlow"));
        finish(submission);
        // With a translator, both prompts are translated first, then the identity instruction follows.
        withTranslator = true;
        sendModels();
        QTRY_COMPARE(client->defaultPromptTranslatorModelId(), QString("llm-chat:translator"));
        panel->setIdentityPrompt("Use the references.");
        positive->setText("Un lapin blanc");
        negative->setText("flou");
        generate->click();
        translateNext("Un lapin blanc", "A white rabbit");
        QVERIFY(!generate->isEnabled()); // Translations hold the single-job guard.
        QCOMPARE(lastTile(GenerationHistoryModel::StatusRole).toString(), QString("Translating the prompt…"));
        QVERIFY(activity->isHidden());
        translateNext("flou", "blurry");
        QTRY_COMPARE(submissions.size(), 1);
        submission = submissions.takeFirst();
        options = submission.value("options").toObject();
        QCOMPARE(submission.value("operation").toString(), QString("image.generate"));
        QCOMPARE(options.value("prompt").toString(), QString("A white rabbit, Use the references."));
        QCOMPARE(options.value("negativePrompt").toString(), QString("blurry"));
        QVERIFY(positive->text().isEmpty()); // The editors never show the translation.
        finish(submission);
        // Restore brings back the original draft; the same text reuses its cached translation.
        window.findChild<QToolButton*>("restorePromptsButton")->click();
        QCOMPARE(positive->text(), QString("Un lapin blanc"));
        QTRY_VERIFY(generate->isEnabled());
        generate->click();
        QTRY_COMPARE(submissions.size(), 1);
        submission = submissions.takeFirst();
        QCOMPARE(submission.value("operation").toString(), QString("image.generate"));
        QCOMPARE(submission.value("options").toObject().value("prompt").toString(),
            QString("A white rabbit, Use the references."));
        finish(submission);
        // A camera shot's fixed framing clauses go before the translated prompts, never through the translator.
        panel->shotPicker()->setCurrentShot(ShotPicker::Shot::Cowboy);
        const auto cowboy = ShotPrompts::forShot(ShotPicker::Shot::Cowboy);
        window.findChild<QToolButton*>("restorePromptsButton")->click();
        QTRY_VERIFY(generate->isEnabled());
        generate->click();
        QTRY_COMPARE(submissions.size(), 1);
        submission = submissions.takeFirst();
        options = submission.value("options").toObject();
        QCOMPARE(submission.value("operation").toString(), QString("image.generate"));
        QCOMPARE(options.value("prompt").toString(), cowboy.positive + ", A white rabbit, Use the references.");
        QCOMPARE(options.value("negativePrompt").toString(), cowboy.negative + ", blurry");
        QCOMPARE(positive->text(), QString()); // The editors and the restored draft stay unprefixed.
        window.findChild<QToolButton*>("restorePromptsButton")->click();
        QCOMPARE(positive->text(), QString("Un lapin blanc"));
        QCOMPARE(negative->text(), QString("flou"));
        panel->clearPrompts();
        finish(submission);
        QVERIFY(ShotPrompts::forShot(ShotPicker::Shot::Free).positive.isEmpty());
        QVERIFY(ShotPrompts::forShot(ShotPicker::Shot::Free).negative.isEmpty());
        panel->shotPicker()->setCurrentShot(ShotPicker::Shot::Free);
        // A failed translation fails the generation without submitting an image job.
        positive->setText("Un renard");
        QTRY_VERIFY(generate->isEnabled());
        generate->click();
        QTRY_COMPARE(submissions.size(), 1);
        reply({{"type", "error"}, {"jobId", submissions.takeFirst().value("jobId")},
               {"code", "prompt-enhancement-failed"}, {"message", "LLM failed"}});
        QTRY_COMPARE(GenerationHistoryModel::State(lastTile(GenerationHistoryModel::StateRole).toInt()),
            GenerationHistoryModel::State::Failed);
        QVERIFY(lastTile(GenerationHistoryModel::StatusRole).toString().contains("LLM failed"));
        QVERIFY(activity->isHidden());
        QVERIFY(!client->jobActive());
        QVERIFY(submissions.isEmpty());
        // Edit instructions are translated too; identity instructions go straight to the edit.
        const QString quality = routing.editingQualities.first().id;
        QVERIFY(client->queueEdit(QUuid::createUuid(), image, "Ajoute un chapeau", {}, quality));
        translateNext("Ajoute un chapeau", "Add a hat");
        QTRY_COMPARE(submissions.size(), 1);
        submission = submissions.takeFirst();
        QCOMPARE(submission.value("operation").toString(), QString("image.edit"));
        QCOMPARE(submission.value("options").toObject().value("prompt").toString(), QString("Add a hat"));
        finish(submission);
        QVERIFY(client->queueEdit(QUuid::createUuid(), image, "Use the references for the face.", {}, quality, false));
        QTRY_COMPARE(submissions.size(), 1);
        submission = submissions.takeFirst();
        QCOMPARE(submission.value("operation").toString(), QString("image.edit"));
        QCOMPARE(submission.value("options").toObject().value("prompt").toString(),
            QString("Use the references for the face."));
        finish(submission);
        // Sharpening: the default quality's model as a direct edit (no prefixes), with full
        // positive and negative prompts and no LLM, even with a translator.
        auto* sharpen = window.findChild<QToolButton*>("imageSharpen");
        QVERIFY(sharpen);
        QVERIFY(client->canSharpen());
        QVERIFY(client->submitSharpen(image));
        QTRY_COMPARE(submissions.size(), 1);
        submission = submissions.takeFirst();
        options = submission.value("options").toObject();
        QCOMPARE(submission.value("operation").toString(), QString("image.edit"));
        QVERIFY(!options.contains("qualityId"));
        QCOMPARE(options.value("modelId").toString(), QString("image-model"));
        // A light pass: subtle sharpening without added detail; repeat it for more.
        QVERIFY(options.value("prompt").toString().startsWith("Slightly improve the sharpness"));
        QVERIFY(options.value("negativePrompt").toString().contains("oversharpened"));
        QVERIFY(options.value("negativePrompt").toString().contains("added details"));
        QCOMPARE(options.value("width").toInt(), image.width());
        QVERIFY(!client->canSharpen()); // One job at a time.
        finish(submission);
        QVERIFY(client->canSharpen());
    }

    void generatesOneImageWithProgressAndSessionHistory()
    {
        QWebSocketServer server("generation-test", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QWebSocket* peer = nullptr;
        QList<QJsonObject> submissions;
        const QJsonObject recommendations{{"width", 768}, {"height", 768}, {"steps", 37},
            {"cfgScale", 5.5}, {"sampler", "euler"}, {"scheduler", "karras"}, {"clipSkip", 2}};
        connect(&server, &QWebSocketServer::newConnection, this, [&]
        {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this, [&](const QString& text)
            {
                const auto request = QJsonDocument::fromJson(text.toUtf8()).object();
                if (request.value("type") == "authenticate")
                {
                    peer->sendTextMessage("{\"type\":\"authenticated\"}");
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                        {"type", "capabilities"}, {"state", "ready"}, {"operations", QJsonArray{"image.generate", "model-routing"}}}).toJson()));
                    auto routing = Nukleus::Protocol::starterModelRouting();
                    routing.presets[0].model.modelId = "opaque-model-id";
                    routing.presets[1].model.modelId = "explicit-model-id";
                    routing.presets[0].positivePrefix = "prefix"; routing.presets[0].negativePrefix = "negative";
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                        {"type", "model-routing.get.completed"}, {"routing", Nukleus::Protocol::modelRoutingToJson(routing)}}).toJson()));
                }
                if (request.value("type") == "models.list")
                {
                    const QJsonObject model{{"id", "opaque-model-id"}, {"name", "Test model"},
                        {"recommendedOptions", recommendations},
                        {"styles", Nukleus::Protocol::promptStylesToJson({{"style", "Style", "prefix", "negative"}})}};
                    const QJsonObject explicitModel{{"id", "explicit-model-id"}, {"name", "Explicit model"},
                        {"explicitContent", true}, {"recommendedOptions", recommendations}};
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                        {"type", "models.list.result"}, {"categories", QJsonArray{QJsonObject{
                            {"category", "image-generation"}, {"models", QJsonArray{model, explicitModel}}}}}}).toJson()));
                }
                if (request.value("type") == "submit")
                    submissions.append(request);
            });
        });
        MainWindow window;
        auto* client = window.findChild<CanvasServiceController*>();
        auto* panel = window.findChild<GenerationInspectorWidget*>();
        auto* workspace = window.findChild<GenerationWorkspaceWidget*>();
        auto* button = window.findChild<QPushButton*>("generateButton");
        QVERIFY(!button->isEnabled());
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        client->connectToService();
        QTRY_VERIFY(button->isEnabled());
        auto* selector = panel->parameters()->findChild<QComboBox*>("generationModel");
        QVERIFY(selector);
        QCOMPARE(selector->count(), 2); // Explicit models are not filtered.
        QCOMPARE(client->models(Nukleus::ModelCategory::ImageGeneration).size(), 2);
        LocalStyleData explicitStyles;
        explicitStyles.styles.append({"local:explicit", "Explicit local style", "local", ""});
        QVERIFY(saveLocalStyles("explicit-model-id", explicitStyles));
        auto *preset = panel->findChild<QComboBox *>("generationPreset");
        preset->setCurrentIndex(1);
        QCOMPARE(panel->parameters()->modelId(), QString("explicit-model-id"));
        QVERIFY(client->canGenerate("explicit-model-id"));
        client->setAllowsExplicitModels(false);
        QCOMPARE(selector->count(), 2);
        QCOMPARE(panel->parameters()->modelId(), QString("explicit-model-id"));
        QCOMPARE(preset->currentData().toString(), QString("illustration"));
        preset->setCurrentIndex(0);
        QCOMPARE(panel->parameters()->modelId(), QString("opaque-model-id"));
        QVERIFY(!loadLocalStyles("explicit-model-id").styles.isEmpty());
        window.findChild<PromptEditorWidget*>("positivePrompt")->setText("A violet nebula");
        window.findChild<PromptEditorWidget*>("negativePrompt")->setText("blur");
        window.resize(1300, 700);
        window.show();
        workspace->showGenerationCard();
        QTest::qWait(1);
        auto* inspectorScroll = panel->findChild<QScrollArea*>();
        const auto inspectorViewportSize = inspectorScroll->viewport()->size();
        button->click();
        QTRY_COMPARE(submissions.size(), 1);
        const auto options = submissions.last().value("options").toObject();
        QCOMPARE(options.value("modelId").toString(), QString("opaque-model-id"));
        QCOMPARE(options.value("prompt").toString(), QString("A violet nebula"));
        QCOMPARE(options.value("negativePrompt").toString(), QString("blur"));
        QVERIFY(!options.contains("styleId"));
        QCOMPARE(options.value("presetId").toString(), QString("photorealism"));
        QVERIFY(!options.contains("qualityId"));
        // Generations queue on the service: the controls stay available for the next one.
        QVERIFY(preset->isEnabled());
        QVERIFY(window.findChild<PromptEditorWidget*>("positivePrompt")->text().isEmpty());
        QVERIFY(window.findChild<PromptEditorWidget*>("negativePrompt")->text().isEmpty());
        for (const auto& key : {QString("width"), QString("height"), QString("steps"), QString("cfgScale"), QString("sampler"), QString("scheduler")})
            QCOMPARE(options.value(key), recommendations.value(key));
        QCOMPARE(options.value("clipSkip").toInt(), -1);
        QCOMPARE(options.value("seed").toInt(), -1);
        QVERIFY(!options.value("hasInput").toBool());
        QVERIFY(button->isEnabled());
        QVERIFY(!client->jobActive());
        const auto send = [&](const QJsonObject& submission, QJsonObject message)
        {
            message.insert("jobId", submission.value("jobId"));
            peer->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));
        };
        const auto first = submissions.first();
        send(first, {{"type", "queued"}});
        // Progress lives in a history tile instead of a dialog or the top pill.
        QVERIFY(!window.findChild<Ardoise::Dialog*>("generationProgressDialog"));
        auto* activity = workspace->activity();
        QVERIFY(!panel->findChild<JobActivityIndicator*>());
        QVERIFY(!panel->findChild<QLabel*>("generationStatus"));
        QVERIFY(activity->isHidden());
        QCOMPARE(activity->state(), JobActivityIndicator::State::Idle);
        auto* history = window.findChild<GenerationHistoryView*>();
        auto* model = history->model();
        QCOMPARE(model->rowCount(), 1);
        const auto tile = [&](int row) { return model->index(row, 0); };
        const auto tileState = [&](int row) {
            return GenerationHistoryModel::State(tile(row).data(GenerationHistoryModel::StateRole).toInt());
        };
        QCOMPARE(tileState(0), GenerationHistoryModel::State::Pending);
        QTRY_VERIFY(tile(0).data(GenerationHistoryModel::StatusRole).toString().contains("Queued"));
        QCOMPARE(tile(0).data(GenerationHistoryModel::ProgressRole).toInt(), -1);
        send(first, {{"type", "progress"}, {"progress", QJsonObject{{"stage", "sampling"},
            {"percentage", 45}, {"currentStep", 9}, {"totalSteps", 20}}}});
        QTRY_COMPARE(tile(0).data(GenerationHistoryModel::ProgressRole).toInt(), 45);
        QVERIFY(tile(0).data(GenerationHistoryModel::StatusRole).toString().contains("9"));
        QVERIFY(tile(0).data(Qt::ToolTipRole).toString().contains("45%"));
        QCOMPARE(inspectorScroll->viewport()->size(), inspectorViewportSize);
        QVERIFY(activity->isHidden());
        // A pending tile cannot be selected.
        QTest::mouseClick(history->viewport(), Qt::LeftButton, {}, history->visualRect(tile(0)).center());
        QVERIFY(!history->currentIndex().isValid());

        // A second generation queues behind the first with its own parameters.
        panel->parameters()->findChild<QSpinBox*>("steps")->setValue(12);
        panel->parameters()->findChild<QDoubleSpinBox*>("cfgScale")->setValue(4.25);
        panel->parameters()->findChild<QSpinBox*>("seed")->setValue(123);
        auto* resolution = panel->parameters()->findChild<QComboBox*>("resolution");
        resolution->setCurrentIndex(1);
        const auto size = panel->parameters()->resolution();
        button->click();
        QTRY_COMPARE(submissions.size(), 2);
        const auto second = submissions.last();
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(tileState(1), GenerationHistoryModel::State::Pending);
        const auto edited = second.value("options").toObject();
        QCOMPARE(edited.value("steps").toInt(), 12);
        QCOMPARE(edited.value("cfgScale").toDouble(), 4.25);
        QCOMPARE(edited.value("seed").toInt(), 123);
        QCOMPARE(edited.value("width").toInt(), size.width());
        QCOMPARE(edited.value("height").toInt(), size.height());
        send(second, {{"type", "queue-position"}, {"position", 2}});
        QTRY_VERIFY(tile(1).data(GenerationHistoryModel::StatusRole).toString().contains("2"));

        // The delivered image replaces its tile without being selected.
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::magenta);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        send(first, {{"type", "result"}, {"mimeType", "image/png"}});
        peer->sendBinaryMessage(bytes);
        QTRY_COMPARE(tileState(0), GenerationHistoryModel::State::Ready);
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(window.findChild<GenerationHistoryWidget*>()->model()->image(
            tile(0).data(GenerationHistoryModel::IdRole).toUuid()), image);
        QVERIFY(workspace->currentImage().isNull());
        QVERIFY(!history->currentIndex().isValid());
        QVERIFY(workspace->findChild<QWidget*>("generationCard")->isVisible());
        QVERIFY(activity->isHidden());

        // Failures stay in their tile with the full message until removed.
        send(second, {{"type", "output"}, {"stream", "stderr"}, {"text", "[ERROR] get sd version from file failed"}});
        send(second, {{"type", "error"}, {"code", "generation-failed"}, {"message", "Test failure"}});
        QTRY_COMPARE(tileState(1), GenerationHistoryModel::State::Failed);
        const QString failure = tile(1).data(GenerationHistoryModel::StatusRole).toString();
        QVERIFY(failure.contains("Test failure"));
        QVERIFY(failure.contains("get sd version from file failed"));
        QVERIFY(tile(1).data(Qt::ToolTipRole).toString().contains("Test failure"));
        QVERIFY(activity->isHidden());
        QVERIFY(button->isEnabled());
        QCOMPARE(workspace->currentImage(), QImage());
        // Keyboard navigation and Next skip the failed tile.
        history->setCurrentIndex(tile(0));
        QCOMPARE(workspace->currentImage(), image);
        QVERIFY(!window.findChild<QToolButton*>("historyNext")->isEnabled());
        workspace->removeImage(tile(1).data(GenerationHistoryModel::IdRole).toUuid());
        QCOMPARE(model->rowCount(), 1);

        button->click();
        QTRY_COMPARE(submissions.size(), 3);
        send(submissions.last(), {{"type", "result"}, {"mimeType", "image/png"}});
        peer->sendBinaryMessage("invalid image");
        QTRY_COMPARE(tileState(1), GenerationHistoryModel::State::Failed);
        const QString unreadable = tile(1).data(GenerationHistoryModel::StatusRole).toString();
        QVERIFY(unreadable.contains("image", Qt::CaseInsensitive));
        QVERIFY(!unreadable.contains("get sd version from file failed"));
        QCOMPARE(workspace->currentImage(), image); // The selection never moves to new tiles.
        button->click();
        QTRY_COMPARE(submissions.size(), 4);
        QCOMPARE(model->rowCount(), 3);
        peer->close();
        QTRY_VERIFY(!client->isAuthenticated());
        QVERIFY(!button->isEnabled());
        QTRY_COMPARE(tileState(2), GenerationHistoryModel::State::Failed);
        QVERIFY(tile(2).data(GenerationHistoryModel::StatusRole).toString().contains("closed"));
        QCOMPARE(client->queuedGenerationCount(), 0);
        QVERIFY(activity->isHidden());
    }

    void generationUploadsReferenceImages()
    {
        QWebSocketServer server("generation-references-test", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QWebSocket* peer = nullptr;
        QJsonObject submission;
        QList<QByteArray> uploaded;
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this, [&](const QString& text) {
                const auto request = QJsonDocument::fromJson(text.toUtf8()).object();
                if (request.value("type") == "authenticate") {
                    peer->sendTextMessage("{\"type\":\"authenticated\"}");
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                        {"type", "capabilities"}, {"state", "ready"},
                        {"operations", QJsonArray{"image.generate", "model-routing"}}}).toJson()));
                    auto routing = Nukleus::Protocol::starterModelRouting();
                    routing.presets[0].model.modelId = "opaque-model-id";
                    routing.presets[1].model.modelId = "no-references-model";
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                        {"type", "model-routing.get.completed"},
                        {"routing", Nukleus::Protocol::modelRoutingToJson(routing)}}).toJson()));
                }
                if (request.value("type") == "models.list")
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                        {"type", "models.list.result"}, {"categories", QJsonArray{QJsonObject{
                            {"category", "image-generation"},
                            {"models", QJsonArray{QJsonObject{{"id", "opaque-model-id"}, {"name", "Test"}},
                                QJsonObject{{"id", "no-references-model"}, {"name", "SDXL"},
                                    {"recommendedOptions", QJsonObject{{"canEdit", false}}}}}}}}}}).toJson()));
                if (request.value("type") == "submit") {
                    submission = request;
                    if (!request.value("options").toObject().contains("inputCount")) return;
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "upload-required"},
                        {"jobId", request.value("jobId")}, {"inputRole", "reference"}, {"inputIndex", 0}}).toJson()));
                }
            });
            connect(peer, &QWebSocket::binaryMessageReceived, this, [&](const QByteArray& bytes) {
                uploaded.append(bytes);
                if (uploaded.size() < submission.value("options").toObject().value("inputCount").toInt())
                    peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "upload-required"},
                        {"jobId", submission.value("jobId")}, {"inputRole", "reference"},
                        {"inputIndex", int(uploaded.size())}}).toJson()));
            });
        });
        MainWindow window;
        auto* client = window.findChild<CanvasServiceController*>();
        auto* panel = window.findChild<GenerationInspectorWidget*>();
        auto* strip = panel->findChild<ReferenceImageStrip*>("generationReferences");
        QVERIFY(strip && strip->isHidden() && panel->attachButton()->isEnabled());
        QList<QImage> references;
        for (const auto color : {Qt::green, Qt::blue, Qt::yellow, Qt::cyan, Qt::magenta}) {
            QImage reference(16, 24, QImage::Format_RGB32); reference.fill(color);
            references.append(reference);
        }
        for (int i = 0; i < ReferenceImageStrip::MaximumReferences; ++i)
            QVERIFY(strip->addReference(references.at(i)));
        QVERIFY(!strip->addReference(references.at(4)));
        QVERIFY(!strip->isHidden());
        QVERIFY(!panel->attachButton()->isEnabled());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        strip->findChildren<QToolButton*>("generationRemoveReference").at(3)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(strip->findChildren<QToolButton*>("generationRemoveReference").size(), 3);
        strip->findChildren<QToolButton*>("generationRemoveReference").at(2)->click();
        QVERIFY(panel->attachButton()->isEnabled());
        // An identity adds its references; its instruction stays out of the editors.
        const bool nativeDialogsDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restoreDialogs = qScopeGuard([&] {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogsDisabled);
        });
        QTemporaryDir identityDirectory;
        QImage face(20, 20, QImage::Format_RGB32); face.fill(Qt::darkRed);
        QImage profile(20, 30, QImage::Format_RGB32); profile.fill(Qt::darkCyan);
        const QString identityPath = writeIdentity(identityDirectory, {face, profile},
            "Use the reference images for the face.", "Edit the face.");
        QVERIFY(!identityPath.isEmpty());
        auto* positive = window.findChild<PromptEditorWidget*>("positivePrompt");
        positive->setText("A violet nebula");
        selectInNextFileDialog(&window, identityPath);
        panel->attachIdentityButton()->click();
        QCOMPARE(strip->references().size(), 4);
        QCOMPARE(panel->identityPrompt(), QString("Use the reference images for the face."));
        QCOMPARE(positive->text(), QString("A violet nebula"));
        QVERIFY(!panel->attachIdentityButton()->isEnabled());
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        client->connectToService();
        auto* button = window.findChild<QPushButton*>("generateButton");
        QTRY_VERIFY(button->isEnabled());
        button->click();
        QTRY_COMPARE(uploaded.size(), 4);
        const auto options = submission.value("options").toObject();
        QCOMPARE(options.value("inputCount").toInt(), 4);
        QVERIFY(!options.value("hasInput").toBool());
        QCOMPARE(options.value("prompt").toString(), QString("A violet nebula, Use the reference images for the face."));
        QCOMPARE(QImage::fromData(uploaded.at(0)), references.at(0));
        QCOMPARE(QImage::fromData(uploaded.at(1)), references.at(1));
        QCOMPARE(QImage::fromData(uploaded.at(2)), face);
        QCOMPARE(QImage::fromData(uploaded.at(3)), profile);
        // References and the identity stay with the draft until New Image resets it.
        QCOMPARE(strip->references().size(), 4);
        QVERIFY(!panel->identityPrompt().isEmpty());
        QVERIFY(strip->ignoredReason().isEmpty());
        // A preset whose model cannot edit cannot use them: they show as ignored, none can be
        // added, and Generate leaves them and the identity's instruction out.
        auto* parameters = panel->parameters();
        QVERIFY(parameters->selectPreset(Nukleus::Protocol::starterModelRouting().presets[1].id));
        QVERIFY(strip->ignoredReason().contains("cannot use reference images"));
        QCOMPARE(strip->findChild<QWidget*>("referenceThumbnail")->toolTip(),
            QString("Reference image 1\n") + strip->ignoredReason());
        strip->removeReference(3);
        QVERIFY(!panel->attachButton()->isEnabled());
        QVERIFY(panel->attachButton()->toolTip().contains("cannot use reference images"));
        QVERIFY(!panel->attachIdentityButton()->isEnabled());
        uploaded.clear();
        positive->setText("A violet nebula");
        QTRY_VERIFY(button->isEnabled());
        button->click();
        QTRY_COMPARE(submission.value("options").toObject().value("presetId").toString(),
            Nukleus::Protocol::starterModelRouting().presets[1].id);
        QVERIFY(!submission.value("options").toObject().contains("inputCount"));
        QCOMPARE(submission.value("options").toObject().value("prompt").toString(), QString("A violet nebula"));
        QVERIFY(uploaded.isEmpty());
        QCOMPARE(strip->references().size(), 3);
        QVERIFY(parameters->selectPreset(Nukleus::Protocol::starterModelRouting().presets[0].id));
        QVERIFY(strip->ignoredReason().isEmpty());
        QVERIFY(panel->attachButton()->isEnabled());
        panel->resetDraft();
        QVERIFY(strip->references().isEmpty() && strip->isHidden());
        QVERIFY(panel->identityPrompt().isEmpty());
        // Removing every thumbnail detaches the identity as well.
        panel->setIdentityPrompt("Use the faces.");
        QVERIFY(strip->addReference(face));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        strip->findChildren<QToolButton*>("generationRemoveReference").at(0)->click();
        QVERIFY(panel->identityPrompt().isEmpty());
    }

    void uneditedResolutionFollowsRefreshedModelRecommendations()
    {
        Nukleus::ModelInfo model;
        model.id = "model";
        model.category = Nukleus::ModelCategory::ImageGeneration;
        model.recommendedOptions = {{"width", 1024}, {"height", 1024}};
        GenerationParametersWidget parameters;
        parameters.setModels({model});
        model.recommendedOptions = {{"width", 768}, {"height", 768}};
        parameters.setModels({model});
        QCOMPARE(parameters.resolution(), QSize(768, 768));
    }

    void kiraWorkspaceShell()
    {
        MainWindow window;
        QCOMPARE(window.minimumSize(), QSize(1300, 700));
        window.resize(1000, 550);
        QCOMPARE(window.size(), QSize(1300, 700));
        auto* workspace = qobject_cast<GenerationWorkspaceWidget*>(window.contentWidget());
        QVERIFY(workspace);
        QVERIFY(!window.findChild<QSplitter*>());
        // The generation controls float in a card over the preview.
        auto* card = workspace->findChild<QWidget*>(QStringLiteral("generationCard"));
        QVERIFY(card);
        QCOMPARE(card->parentWidget(), static_cast<QWidget*>(workspace->preview()));
        QVERIFY(card->isAncestorOf(window.findChild<GenerationInspectorWidget*>()));
        QVERIFY(!window.windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(!window.findChild<Ardoise::TitleBar*>());
        QVERIFY(!window.findChild<QToolBar*>());
        QVERIFY(!window.findChild<QMenu*>(QStringLiteral("applicationMenu")));
        auto* toolbar = workspace->findChild<QWidget*>(QStringLiteral("imageEditingToolbar"));
        QVERIFY(toolbar);
        auto* settingsButton = toolbar->findChild<QToolButton*>(QStringLiteral("servicePreferencesButton"));
        QVERIFY(settingsButton && settingsButton->isEnabled());
        QVERIFY(!settingsButton->menu());
        bool preferencesOpened = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<ServicePreferencesDialog*>();
            preferencesOpened = dialog && dialog->isVisible();
            if (dialog) dialog->reject();
        });
        settingsButton->click();
        QVERIFY(preferencesOpened);
        QVERIFY(qobject_cast<QVBoxLayout*>(toolbar->layout()));
        QVERIFY(!window.findChild<QAction*>(QStringLiteral("availableModelsAction")));
        QVERIFY(!window.findChild<QAction*>(QStringLiteral("clearPromptsAction")));
        auto* generate = window.findChild<QPushButton*>(QStringLiteral("generateButton"));
        QVERIFY(generate);
        QVERIFY(!generate->isEnabled());
        QVERIFY(!generate->accessibleDescription().isEmpty());
        auto* scroll = window.findChild<QScrollArea*>();
        QVERIFY(scroll);
        QVERIFY(!scroll->isAncestorOf(generate));
        QVERIFY(!window.findChild<QAction*>(QStringLiteral("saveImageAction"))->isEnabled());
        QVERIFY(window.styleSheet().isEmpty());
    }
    void remembersGeometry()
    {
        QByteArray geometry;
        {
            MainWindow window;
            window.resize(1320, 760);
            window.close();
            geometry = QSettings().value(QStringLiteral("MainWindow/Geometry")).toByteArray();
            QVERIFY(!geometry.isEmpty());
        }
        // Retired splitter settings remain untouched and are never used.
        QSettings().setValue(QStringLiteral("MainWindow/WorkspaceSplitter"), QByteArray("retired"));
        QSettings().setValue(QStringLiteral("MainWindow/GenerationWorkspaceSplitter"), QByteArray("retired"));
        MainWindow restored;
        // Qt clamps restored geometry to the available screen (offscreen is only 800 pixels wide).
        QMainWindow expected;
        expected.setMinimumSize(restored.minimumSize());
        QVERIFY(expected.restoreGeometry(geometry));
        QCOMPARE(restored.size(), expected.size());
        restored.close();
        QCOMPARE(QSettings().value(QStringLiteral("MainWindow/WorkspaceSplitter")).toByteArray(),
            QByteArray("retired"));
        QCOMPARE(QSettings().value(QStringLiteral("MainWindow/GenerationWorkspaceSplitter")).toByteArray(),
            QByteArray("retired"));
    }
    void landscapeInspectorColumnsAlign()
    {
        GenerationInspectorWidget panel;
        panel.setLandscape(true);
        panel.resize(900, panel.sizeHint().height());
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        const auto rect = [&](QWidget* widget) { return QRect(widget->mapTo(&panel, QPoint()), widget->size()); };
        const auto child = [&](const char* name) { return panel.findChild<QWidget*>(name); };
        // Both columns' first headers, and their first fields, share a top edge.
        QCOMPARE(rect(child("generationStep4")).center().y(), rect(child("generationStep1")).center().y());
        auto* positive = child("positivePrompt")->findChild<QPlainTextEdit*>();
        QCOMPARE(rect(positive).top(), rect(child("generationPreset")).top());
        // The positive prompt grows so the negative prompt ends with the reference buttons.
        auto* negative = child("negativePrompt")->findChild<QPlainTextEdit*>();
        QCOMPARE(rect(negative).bottom(), rect(child("generationAttach")).bottom());
        QVERIFY(positive->height() > negative->height());    }

    void generationCardFollowsNewImageMode()
    {
        MainWindow window;
        window.resize(1300, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* workspace = qobject_cast<GenerationWorkspaceWidget*>(window.contentWidget());
        auto* drawer = workspace->findChild<QWidget*>(QStringLiteral("generationCard"));
        auto* panel = drawer->findChild<GenerationInspectorWidget*>();
        auto* generate = drawer->findChild<QPushButton*>(QStringLiteral("generateButton"));
        auto* history = workspace->findChild<GenerationHistoryWidget*>();
        QVERIFY(!history->findChild<QAbstractButton*>(QStringLiteral("historyNewImage")));
        auto* newImage = workspace->findChild<QToolButton*>(QStringLiteral("imageNew"));
        auto* viewport = workspace->preview()->viewport();
        const auto centred = [&] {
            const QPoint offset = drawer->geometry().center() - viewport->geometry().center();
            return qAbs(offset.x()) <= 2 && qAbs(offset.y()) <= 2;
        };
        // The workspace starts on an empty preview; the New Image tool opens the centred card.
        QVERIFY(newImage->isCheckable() && !newImage->isChecked());
        QVERIFY(drawer->isHidden());
        newImage->click();
        QVERIFY(newImage->isChecked());
        QTRY_VERIFY(centred());
        QVERIFY(viewport->geometry().contains(drawer->geometry()));
        QVERIFY(generate->isVisible());
        QVERIFY(!drawer->findChild<QToolButton*>(QStringLiteral("generationDrawerExpand")));
        // Landscape: the look sits beside the prompts, and the card is wider than tall.
        QVERIFY(panel->isLandscape());
        QVERIFY(drawer->width() > drawer->height());
        auto* positive = window.findChild<PromptEditorWidget*>(QStringLiteral("positivePrompt"));
        const auto left = [&](QWidget* widget) { return widget->mapTo(panel, QPoint()).x(); };
        QVERIFY(left(panel->parameters()) + panel->parameters()->width() < left(positive));
        QVERIFY(panel->findChild<QLabel*>(QStringLiteral("generateShortcutHint"))->isVisible());
        // The card follows the viewport when the window grows.
        window.resize(1500, 820);
        QTRY_VERIFY(centred());
        // Ctrl+Return generates from the prompts when generation is available.
        window.activateWindow();
        if (QTest::qWaitForWindowActive(&window)) {
            QSignalSpy requests(panel, &GenerationInspectorWidget::generationRequested);
            auto* editor = positive->findChild<QPlainTextEdit*>();
            editor->setFocus();
            panel->setGenerationAvailable(false);
            QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
            QCOMPARE(requests.count(), 0);
            panel->setGenerationAvailable(true);
            QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
            QCOMPARE(requests.count(), 1);
            panel->setGenerationAvailable(false);
        }
        // Selecting an image keeps the card open over it, without the edit bubble.
        auto routing = Nukleus::Protocol::starterModelRouting();
        Nukleus::ModelInfo editor;
        editor.id = "drawer-editor"; editor.category = Nukleus::ModelCategory::ImageGeneration;
        for (auto& quality : routing.editingQualities)
            quality.model.modelId = editor.id;
        workspace->setEditRouting(routing, {editor});
        QImage image(640, 480, QImage::Format_RGB32); image.fill(Qt::darkCyan);
        workspace->appendImage(image, "Drawer");
        auto* bubble = workspace->findChild<QWidget*>(QStringLiteral("imageEditRow"));
        QCOMPARE(workspace->currentImage(), image);
        QVERIFY(newImage->isChecked());
        QVERIFY(!drawer->isHidden() && generate->isVisible());
        QTRY_VERIFY(bubble->isHidden());
        // Closing the card hands the image over to editing and keeps the draft.
        positive->setText(QStringLiteral("kept"));
        drawer->findChild<QToolButton*>(QStringLiteral("generationCardClose"))->click();
        QVERIFY(!newImage->isChecked());
        QTRY_VERIFY(drawer->isHidden());
        QVERIFY(!bubble->isHidden());
        newImage->click();
        QTRY_VERIFY(!drawer->isHidden());
        QCOMPARE(workspace->currentImage(), image);
        QCOMPARE(positive->text(), QStringLiteral("kept"));
        // The toolbar button toggles the card; Escape inside it closes it too.
        newImage->click();
        QTRY_VERIFY(drawer->isHidden());
        newImage->click();
        QTRY_VERIFY(!drawer->isHidden());
        if (QTest::qWaitForWindowActive(&window)) {
            positive->findChild<QPlainTextEdit*>()->setFocus();
            QTest::keyClick(positive->findChild<QPlainTextEdit*>(), Qt::Key_Escape);
            QVERIFY(!newImage->isChecked());
            QTRY_VERIFY(drawer->isHidden());
        }
        // There is no New image tile to return to: Previous stops at the first image.
        auto* previous = history->findChild<QToolButton*>(QStringLiteral("historyPrevious"));
        QVERIFY(!previous->isEnabled());
        // The New Image action opens the card over the image.
        window.findChild<QAction*>(QStringLiteral("newImageAction"))->trigger();
        QVERIFY(newImage->isChecked());
        QTRY_VERIFY(!drawer->isHidden());
        QCOMPARE(workspace->currentImage(), image);
    }
    void preferencesHaveFixedSize()
    {
        CanvasServiceController client;
        ServicePreferencesDialog dialog(&client);
        QCOMPARE(dialog.minimumSize(), dialog.maximumSize());
        const QSize fixedSize = dialog.size();
        dialog.resize(fixedSize + QSize(200, 200));
        QCOMPARE(dialog.size(), fixedSize);
        const auto captureDirectory = qEnvironmentVariable("KAIMERA_SCREENSHOT_DIR");
        if (!captureDirectory.isEmpty())
        {
            QVERIFY(QDir().mkpath(captureDirectory));
            dialog.show();
            QCoreApplication::processEvents();
            QVERIFY(dialog.grab().save(QDir(captureDirectory).filePath(QStringLiteral("service-preferences.png"))));
        }
    }
    void bundledAppIconLoads()
    {
        for (const int size : { 16, 32, 48, 64, 128, 256, 512, 1024 })
        {
            QIcon icon(QStringLiteral(":/app-icon/%1.png").arg(size));
            QVERIFY(!icon.isNull());
            QVERIFY(!icon.pixmap(size, size).isNull());
        }
    }
    void outputPreferencesSaveCancelAndPersist()
    {
        CanvasServiceController client;
        const QString folder = m_settingsDirectory.filePath("exports");
        {
            ServicePreferencesDialog dialog(&client);
            auto* output = dialog.findChild<OutputSettingsWidget*>();
            QVERIFY(output);
            output->findChild<QLineEdit*>(QStringLiteral("outputDirectory"))->setText(folder);
            auto* formats = output->findChild<QComboBox*>(QStringLiteral("outputFormat"));
            formats->setCurrentIndex(formats->count() - 1);
            const QByteArray chosenFormat = output->format();
            // Invalid service input must not save local output changes either.
            auto* host = dialog.findChild<QLineEdit*>(QStringLiteral("serviceHostInput"));
            host->setText(QStringLiteral("not-an-ip"));
            auto* save = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
            save->click();
            QVERIFY(!QSettings().contains(QStringLiteral("Output/Directory")));
            host->setText(QStringLiteral("localhost"));
            save->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
            QCOMPARE(OutputSettingsWidget::preferredDirectory(), folder);
            QCOMPARE(OutputSettingsWidget::preferredFormat(), chosenFormat);
        }
        {
            ServicePreferencesDialog reopened(&client);
            auto* output = reopened.findChild<OutputSettingsWidget*>();
            QCOMPARE(output->directory(), folder);
            QCOMPARE(output->format(), OutputSettingsWidget::preferredFormat());
            output->findChild<QLineEdit*>(QStringLiteral("outputDirectory"))->setText("/discarded");
            reopened.show();
            reopened.close();
            QCOMPARE(OutputSettingsWidget::preferredDirectory(), folder);
        }
        GenerationInspectorWidget panel;
        panel.resetDraft();
        QCOMPARE(OutputSettingsWidget::preferredDirectory(), folder);
        QVERIFY(!panel.findChild<OutputSettingsWidget*>());
        QSettings().setValue(QStringLiteral("Output/Format"), QByteArray("unsupported"));
        QCOMPARE(OutputSettingsWidget::preferredFormat(), QByteArray("png"));
    }
    void parametersAndCatalog()
    {
        GenerationParametersWidget parameters;
        QCOMPARE(parameters.resolution(), QSize(1024, 1024));
        QCOMPARE(parameters.steps(), 28);
        QCOMPARE(parameters.cfgScale(), 7.0);
        QCOMPARE(parameters.seed(), -1);
        QVERIFY(parameters.modelId().isEmpty());
        auto* combo = parameters.findChild<QComboBox*>(QStringLiteral("generationModel"));
        QVERIFY(!combo->isEnabled());
        Nukleus::ModelInfo model;
        model.id = QStringLiteral("opaque-model-id");
        model.name = QStringLiteral("Installed model");
        model.category = Nukleus::ModelCategory::ImageGeneration;
        parameters.setModels({ model });
        QVERIFY(combo->isEnabled());
        QCOMPARE(parameters.modelId(), model.id);
        model.name = QStringLiteral("Renamed model");
        parameters.setModels({ model });
        QCOMPARE(parameters.modelId(), model.id);
        parameters.findChild<QSpinBox*>(QStringLiteral("steps"))->setValue(42);
        parameters.resetDefaults();
        QCOMPARE(parameters.steps(), 42);
        parameters.setModels({ });
        QVERIFY(parameters.modelId().isEmpty());
    }
    void modelRecommendationsAndSessionOverrides()
    {
        Nukleus::ModelInfo first;
        first.id = QStringLiteral("first");
        first.category = Nukleus::ModelCategory::ImageGeneration;
        first.recommendedOptions = {{"width", 1024}, {"height", 1024}, {"steps", 37}, {"cfgScale", 5.75}};
        auto second = first;
        second.id = QStringLiteral("second");
        second.recommendedOptions = {{"width", 512}, {"height", 512}, {"steps", 22}, {"cfgScale", 3.5}};
        GenerationParametersWidget parameters;
        parameters.setModels({first, second});
        QCOMPARE(parameters.steps(), 37);
        QCOMPARE(parameters.cfgScale(), 5.75);
        auto* resolutions = parameters.findChild<QComboBox*>(QStringLiteral("resolution"));
        QCOMPARE(resolutions->count(), 15);
        QCOMPARE(parameters.resolution(), QSize(1024, 1024));
        for (int i = 0; i < resolutions->count(); ++i)
        {
            const auto size = resolutions->itemData(i).toSize();
            QVERIFY(qint64(size.width()) * size.height() <= qint64(1024) * 1024);
            QCOMPARE(size.width() % 64, 0);
            QCOMPARE(size.height() % 64, 0);
            QVERIFY(resolutions->itemText(i).contains(QStringLiteral(":")));
            QVERIFY(resolutions->itemText(i).contains(QStringLiteral("×")));
        }
        QVERIFY(resolutions->findData(QSize(896, 1152)) >= 0); // Target 3:4.
        QVERIFY(resolutions->findData(QSize(1152, 896)) >= 0); // Target 4:3.
        QVERIFY(resolutions->findData(QSize(768, 1344)) >= 0); // Target 3:5.
        QVERIFY(resolutions->findData(QSize(1344, 768)) >= 0); // Target 5:3.
        QVERIFY(resolutions->itemText(resolutions->findData(QSize(896, 1152))).contains(QStringLiteral("≈3:4")));
        resolutions->setCurrentIndex(1);
        const QSize chosen = parameters.resolution();
        auto* steps = parameters.findChild<QSpinBox*>(QStringLiteral("steps"));
        auto* cfg = parameters.findChild<QDoubleSpinBox*>(QStringLiteral("cfgScale"));
        auto* models = parameters.findChild<QComboBox*>(QStringLiteral("generationModel"));
        steps->setValue(49);
        cfg->setValue(6.25);
        parameters.resetDefaults();
        QCOMPARE(parameters.steps(), 49);
        QCOMPARE(parameters.cfgScale(), 6.25);
        first.name = QStringLiteral("Renamed");
        first.recommendedOptions["steps"] = 35;
        parameters.setModels({second, first});
        QCOMPARE(parameters.modelId(), first.id);
        QCOMPARE(parameters.resolution(), chosen);
        QCOMPARE(parameters.steps(), 49);
        QCOMPARE(parameters.cfgScale(), 6.25);
        models->setCurrentIndex(0);
        QCOMPARE(parameters.steps(), 22);
        QCOMPARE(parameters.cfgScale(), 3.5);
        QCOMPARE(parameters.resolution(), QSize(512, 512));
        models->setCurrentIndex(1);
        QCOMPARE(parameters.steps(), 35);
        QCOMPARE(parameters.cfgScale(), 5.75);
        first.recommendedOptions["steps"] = 31;
        parameters.setModels({first, second});
        QCOMPARE(parameters.steps(), 31);
        GenerationParametersWidget reopened;
        reopened.setModels({first});
        QCOMPARE(reopened.steps(), 31);
        QCOMPARE(reopened.cfgScale(), 5.75);
        parameters.setModels({second});
        QCOMPARE(parameters.steps(), 22);
        QCOMPARE(parameters.cfgScale(), 3.5);
        first.recommendedOptions["width"] = 768;
        first.recommendedOptions["height"] = 768;
        parameters.setModels({first});
        QCOMPARE(parameters.resolution(), QSize(768, 768));
        QVERIFY(resolutions->findData(QSize(1024, 576)) >= 0); // Exact 16:9 at this budget.
        QVERIFY(resolutions->findData(QSize(576, 1024)) >= 0);
        for (int i = 0; i < resolutions->count(); ++i)
        {
            const auto size = resolutions->itemData(i).toSize();
            QVERIFY(qint64(size.width()) * size.height() <= qint64(768) * 768);
        }
    }
    void resolutionChoicesStayWithinAllModelBudgets()
    {
        for (int side : {256, 512, 768, 1024, 1280, 1536, 1792, 2048})
        {
            Nukleus::ModelInfo model;
            model.id = QStringLiteral("budget-model");
            model.category = Nukleus::ModelCategory::ImageGeneration;
            model.recommendedOptions = {{"width", side}, {"height", side}};
            GenerationParametersWidget parameters;
            parameters.setModels({model});
            auto* resolutions = parameters.findChild<QComboBox*>(QStringLiteral("resolution"));
            QCOMPARE(resolutions->count(), 15);
            QCOMPARE(parameters.resolution(), QSize(side, side));
            for (int i = 0; i < resolutions->count(); ++i)
            {
                const QSize size = resolutions->itemData(i).toSize();
                QVERIFY(qint64(size.width()) * size.height() <= qint64(side) * side);
                QVERIFY(size.width() >= 64 && size.width() <= 4096);
                QVERIFY(size.height() >= 64 && size.height() <= 4096);
                QCOMPARE(size.width() % 64, 0);
                QCOMPARE(size.height() % 64, 0);
                if (i > 0 && i % 2 == 1)
                {
                    const QSize portrait = resolutions->itemData(i + 1).toSize();
                    QVERIFY(size.width() > size.height());
                    QCOMPARE(portrait, QSize(size.height(), size.width()));
                }
            }
        }
    }
    void draftAndHistoryActions()
    {
        MainWindow window;
        auto* workspace = window.findChild<GenerationWorkspaceWidget*>();
        auto* panel = window.findChild<GenerationInspectorWidget*>();
        auto* positive = panel->findChild<PromptEditorWidget*>(QStringLiteral("positivePrompt"));
        auto* negative = panel->findChild<PromptEditorWidget*>(QStringLiteral("negativePrompt"));
        QVERIFY(positive->text().isEmpty());
        QVERIFY(negative->text().isEmpty());
        QVERIFY(!panel->parameters()->isHidden());
        QVERIFY(!panel->findChild<OutputSettingsWidget*>());
        QVERIFY(panel->findChildren<CollapsibleSection*>().isEmpty());
        QImage image(80, 40, QImage::Format_RGB32);
        image.fill(Qt::green);
        workspace->appendImage(image, QStringLiteral("First image"));
        QVERIFY(window.findChild<QAction*>(QStringLiteral("saveImageAction"))->isEnabled());
        QVERIFY(positive->text().isEmpty());
        QVERIFY(negative->text().isEmpty());
        QCOMPARE(workspace->currentImage(), image);
        panel->parameters()->findChild<QSpinBox*>(QStringLiteral("steps"))->setValue(45);
        positive->setText(QStringLiteral("draft"));
        window.findChild<QAction*>(QStringLiteral("newImageAction"))->trigger();
        QCOMPARE(panel->parameters()->steps(), 45);
        QVERIFY(positive->text().isEmpty());
        // New Image opens the card over the selected image without clearing it.
        QVERIFY(window.findChild<GenerationCard*>()->isActive());
        QCOMPARE(workspace->currentImage(), image);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("saveImageAction"))->isEnabled());
        auto* history = workspace->findChild<GenerationHistoryView*>();
        QCOMPARE(history->model()->rowCount(), 1);
        QVERIFY(positive->text().isEmpty());
    }
    void previewAndHistory()
    {
        GenerationWorkspaceWidget workspace;
        workspace.resize(700, 600);
        workspace.show();
        QCoreApplication::processEvents();
        QImage landscape(800, 400, QImage::Format_RGB32);
        landscape.fill(Qt::red);
        QImage portrait(400, 800, QImage::Format_RGB32);
        portrait.fill(Qt::blue);
        workspace.appendImage(landscape, QStringLiteral("Landscape"));
        workspace.appendImage(portrait, QStringLiteral("Portrait"));
        auto* history = workspace.findChild<GenerationHistoryView*>();
        auto* model = qobject_cast<GenerationHistoryModel*>(history->model());
        QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(workspace.currentImage(), portrait);
        const auto id = model->index(0).data(GenerationHistoryModel::IdRole).toUuid();
        QCOMPARE(model->image(id), landscape);
        const auto icon = model->index(0).data(Qt::DecorationRole).value<QIcon>();
        QCOMPARE(icon.pixmap(QSize(120, 90)).size(), QSize(120, 60));
        history->setCurrentIndex(model->index(0));
        QCOMPARE(workspace.currentImage(), landscape);
        auto* preview = workspace.preview();
        QVERIFY(preview->transform().m11() > 0);
        QCOMPARE(preview->transform().m11(), preview->transform().m22());
        preview->actualSize();
        QCOMPARE(preview->transform().m11(), 1.0);
        preview->zoomIn();
        QVERIFY(preview->transform().m11() > 1);
        preview->zoomOut();
        QVERIFY(qAbs(preview->transform().m11() - 1) < 0.0001);
        for (int i = 0; i < 60; ++i)
            preview->zoomIn();
        QVERIFY(preview->transform().m11() <= 64.0001);
        preview->fitImage();
        const auto fitted = preview->transform().m11();
        workspace.resize(900, 700);
        QCoreApplication::processEvents();
        QVERIFY(preview->transform().m11() > fitted);
        workspace.clearSelection();
        QVERIFY(workspace.currentImage().isNull());
        QCOMPARE(model->rowCount(), 2);
    }
    void openAndAtomicExport()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto source = directory.filePath(QStringLiteral("source.png"));
        QImage image(80, 40, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(source));
        GenerationWorkspaceWidget workspace;
        QString error;
        QVERIFY(workspace.openImage(source, &error));
        QCOMPARE(workspace.currentImage(), image);
        const auto target = directory.filePath(QStringLiteral("export.png"));
        QVERIFY(workspace.saveImage(target, QByteArray("png"), &error));
        QCOMPARE(QImage(target), image);
        QVERIFY(!workspace.openImage(directory.filePath(QStringLiteral("missing.png")), &error));
        QVERIFY(!error.isEmpty());
        QFile corrupt(directory.filePath(QStringLiteral("corrupt.png")));
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("invalid image");
        corrupt.close();
        QVERIFY(!workspace.openImage(corrupt.fileName(), &error));
        QCOMPARE(workspace.currentImage(), image);
        QVERIFY(!workspace.saveImage(target, QByteArray("unsupported-format"), &error));
        QCOMPARE(QImage(target), image);
        QVERIFY(!workspace.saveImage(
            directory.filePath(QStringLiteral("missing/export.png")), QByteArray("png"), &error));
        QCOMPARE(workspace.currentImage(), image);
        OutputSettingsWidget output;
        QCOMPARE(output.format(), QByteArray("png"));
        auto* combo = output.findChild<QComboBox*>();
        for (int i = 0; i < combo->count(); ++i)
            QVERIFY(QImageWriter::supportedImageFormats().contains(combo->itemData(i).toByteArray()));
    }
    void localizedScreen()
    {
        QTranslator translator;
        QVERIFY(translator.load(QStringLiteral(":/translations/kaimera_fr.qm")));
        QVERIFY(qApp->installTranslator(&translator));
        const QLocale previousLocale;
        QLocale::setDefault(QLocale(QLocale::French, QLocale::France));
        {
            MainWindow window;
            QCOMPARE(window.findChild<QAction*>(QStringLiteral("newImageAction"))->text(),
                QStringLiteral("Nouvelle image"));
            QCOMPARE(window.findChild<QPushButton*>(QStringLiteral("generateButton"))->text(),
                QStringLiteral("Générer"));
            QCOMPARE(window.findChild<QDoubleSpinBox*>(QStringLiteral("cfgScale"))->text(),
                QStringLiteral("7,0000"));
        }
        qApp->removeTranslator(&translator);
        QLocale::setDefault(previousLocale);
        MainWindow english;
        QCOMPARE(english.findChild<QAction*>(QStringLiteral("newImageAction"))->text(),
            QStringLiteral("New Image"));
    }
    void collapsibleSections()
    {
        auto* content = new QWidget;
        CollapsibleSection section(QStringLiteral("Section"), content, false);
        QVERIFY(content->isHidden());
        section.setExpanded(true);
        QVERIFY(!content->isHidden());
        auto* header = section.findChild<QToolButton*>();
        QCOMPARE(section.findChildren<QToolButton*>().size(), 1);
        section.resize(400, 200);
        section.show();
        QCoreApplication::processEvents();
        QVERIFY(header->width() > 350);
        QTest::mouseClick(
            header, Qt::LeftButton, Qt::NoModifier, QPoint(header->width() - 8, header->height() / 2));
        QVERIFY(content->isHidden());
        QTest::mouseClick(
            header, Qt::LeftButton, Qt::NoModifier, QPoint(header->width() / 2, header->height() / 2));
        QVERIFY(!content->isHidden());
    }
    void keyboardNavigationAndEditing()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        if (!QTest::qWaitForWindowActive(&window))
            QSKIP("The window manager did not grant keyboard focus. Run this interaction test offscreen.");
        auto* prompt = window.findChild<PromptEditorWidget*>(QStringLiteral("positivePrompt"));
        auto* editor = prompt->findChild<QPlainTextEdit*>();
        prompt->setText(QStringLiteral("prompt"));
        editor->setFocus();
        editor->selectAll();
        QAction* copy = nullptr;
        for (auto* action : window.findChildren<QAction*>())
            if (action->text() == QStringLiteral("Copy"))
                copy = action;
        QVERIFY(copy);
        QTRY_VERIFY(copy->isEnabled());
        copy->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("prompt"));
        editor->setFocus();
        QTest::keyClicks(editor, "replacement");
        QCOMPARE(prompt->text(), QStringLiteral("replacement"));
        QTest::keySequence(editor, QKeySequence::Undo);
        QCOMPARE(prompt->text(), QStringLiteral("prompt"));
        auto* fullscreen = window.findChild<QAction*>(QStringLiteral("fullscreenAction"));
        window.showFullScreen();
        QCoreApplication::processEvents();
        QVERIFY(fullscreen->isChecked());
        window.showNormal();
        QCoreApplication::processEvents();
        QVERIFY(!fullscreen->isChecked());
    }
    void narrowPanelKeepsGenerateVisible()
    {
        GenerationInspectorWidget panel;
        // Exercise scrolling with all remaining sections opened.
        for (auto* section : panel.findChildren<CollapsibleSection*>())
            section->setExpanded(true);
        panel.resize(320, 360);
        panel.show();
        QCoreApplication::processEvents();
        auto* scroll = panel.findChild<QScrollArea*>();
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        auto* generate = panel.findChild<QPushButton*>(QStringLiteral("generateButton"));
        QVERIFY(generate->isVisible());
        QVERIFY(panel.rect().contains(generate->geometry()));
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QVERIFY(generate->isVisible());
    }
    void inspectorGuidesThroughSteps()
    {
        GenerationInspectorWidget panel;
        panel.resize(390, 900);
        panel.show();
        QTest::qWait(20);
        // Five numbered steps in order, with no expanders.
        QVERIFY(panel.findChildren<CollapsibleSection*>().isEmpty());
        QLabel* steps[5];
        for (int i = 0; i < 5; ++i) {
            steps[i] = panel.findChild<QLabel*>(QStringLiteral("generationStep%1").arg(i + 1));
            QVERIFY(steps[i] && steps[i]->isVisible());
        }
        auto* positive = panel.findChild<PromptEditorWidget*>(QStringLiteral("positivePrompt"));
        auto* negative = panel.findChild<PromptEditorWidget*>(QStringLiteral("negativePrompt"));
        const auto top = [&](QWidget* widget) { return widget->mapTo(&panel, QPoint()).y(); };
        auto* shot = panel.findChild<ShotPicker*>(QStringLiteral("shotPicker"));
        QVERIFY(shot && shot->isVisible());
        QVERIFY(top(steps[0]) < top(steps[1]) && top(steps[1]) < top(shot) && top(shot) < top(steps[2]));
        QVERIFY(top(steps[2]) < top(steps[3]) && top(steps[3]) < top(positive) && top(positive) < top(steps[4]));
        QVERIFY(top(steps[4]) < top(negative));
        // The camera shot starts free, follows the keyboard and resets with the draft.
        QCOMPARE(shot->currentShot(), ShotPicker::Shot::Free);
        QSignalSpy shotChanges(shot, &ShotPicker::currentShotChanged);
        shot->setFocus();
        QTest::keyClick(shot, Qt::Key_Left);
        QCOMPARE(shotChanges.count(), 0);
        QTest::keyClick(shot, Qt::Key_Right);
        QCOMPARE(shot->currentShot(), ShotPicker::Shot::ExtremeCloseUp);
        QCOMPARE(shotChanges.count(), 1);
        panel.resetDraft();
        QCOMPARE(shot->currentShot(), ShotPicker::Shot::Free);
        QVERIFY(!positive->findChild<QPlainTextEdit*>()->placeholderText().isEmpty());
        // Restore and a single Sparkles sit at the end of the description step's header.
        auto* restore = panel.findChild<QToolButton*>("restorePromptsButton");
        QVERIFY(restore && restore->isVisible() && !restore->isEnabled());
        const auto assistants = panel.findChildren<QToolButton*>("promptAssistButton");
        QCOMPARE(assistants.size(), 1);
        auto* assist = assistants.first();
        QVERIFY(assist->isVisible() && !assist->isEnabled()); // Until an enhancer is available.
        QVERIFY(!assist->accessibleName().isEmpty());
        for (auto* action : {static_cast<QWidget*>(restore), static_cast<QWidget*>(assist)}) {
            QVERIFY(qAbs(action->mapTo(&panel, action->rect().center()).y()
                - steps[3]->mapTo(&panel, steps[3]->rect().center()).y()) <= 2);
            QVERIFY(action->mapTo(&panel, QPoint()).x() > steps[3]->mapTo(&panel, QPoint()).x() + steps[3]->width());
        }
        panel.setPromptAssistAvailable(true);
        QVERIFY(!assist->isEnabled()); // Nothing to improve yet.
        positive->setText("A cat");
        panel.setPromptAssistAvailable(true);
        QVERIFY(assist->isEnabled());
        QSignalSpy assistRequests(&panel, &GenerationInspectorWidget::promptAssistRequested);
        assist->click();
        QCOMPARE(assistRequests.count(), 1);
        positive->setText({});
        // Steps, CFG and seed follow the preset and stay random, so they are hidden.
        QVERIFY(panel.findChild<QSpinBox*>("steps")->isHidden());
        QVERIFY(panel.findChild<QDoubleSpinBox*>("cfgScale")->isHidden());
        QVERIFY(panel.findChild<QSpinBox*>("seed")->parentWidget()->isHidden());
        QCOMPARE(panel.generationRequest().seed, -1);
        // The preset alone chooses the model; there is no quality control.
        auto* preset = panel.findChild<QComboBox*>("generationPreset");
        QVERIFY(preset->isHidden()); // Until the service provides routing.
        QVERIFY(!panel.findChild<QSlider*>());
        auto routing = Nukleus::Protocol::starterModelRouting();
        Nukleus::ModelInfo model;
        model.id = "guided-model"; model.category = Nukleus::ModelCategory::ImageGeneration;
        for (auto& p : routing.presets)
            p.model.modelId = model.id;
        panel.parameters()->setModels({model});
        panel.parameters()->setRouting(routing, true);
        QVERIFY(!preset->isHidden());
        QCOMPARE(panel.generationRequest().presetId, QString("photorealism"));
        panel.parameters()->setRoutingLocked(true);
        QVERIFY(!preset->isEnabled());
        panel.parameters()->setRoutingLocked(false);
        QVERIFY(preset->isEnabled());
        // Resolution is a strip of ratio tiles with a landscape/portrait switch.
        auto* resolution = panel.findChild<QComboBox*>("resolution");
        auto* picker = panel.findChild<ResolutionPicker*>("resolutionPicker");
        QVERIFY(picker && !picker->isHidden() && resolution->isHidden());
        QCOMPARE(picker->currentIndex(), resolution->currentIndex());
        QCOMPARE(panel.generationRequest().width, panel.generationRequest().height); // Square by default.
        QVERIFY(!picker->isPortrait());
        picker->setFocus();
        QTest::keyClick(picker, Qt::Key_Right);
        const QSize landscape = resolution->currentData().toSize();
        QVERIFY(landscape.width() > landscape.height());
        QCOMPARE(picker->currentIndex(), resolution->currentIndex());
        // Turning to portrait selects the same ratio, rotated.
        QTest::keyClick(picker, Qt::Key_Down);
        QVERIFY(picker->isPortrait());
        const QSize portrait = resolution->currentData().toSize();
        QVERIFY(portrait.width() < portrait.height());
        QVERIFY(qAbs(double(portrait.height()) / portrait.width() - double(landscape.width()) / landscape.height()) < 0.05);
        QCOMPARE(panel.generationRequest().width, portrait.width());
        // Choosing a landscape size elsewhere turns the strip back.
        resolution->setCurrentIndex(resolution->findData(landscape));
        QVERIFY(!picker->isPortrait());
        QCOMPARE(picker->currentIndex(), resolution->currentIndex());
    }
    void previewKeepsRoundedFrameVisible()
    {
        GenerationPreviewWidget preview;
        preview.resize(320, 240);
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::red);
        preview.setImage(image);
        preview.show();
        preview.clearFocus();
        preview.setZoom(10);
        QCoreApplication::processEvents();
        const auto capture = preview.grab().toImage();
        const qreal scale = capture.devicePixelRatio();
        for (const QPoint corner : { QPoint(1, 1), QPoint(preview.width() - 2, 1),
                 QPoint(1, preview.height() - 2), QPoint(preview.width() - 2, preview.height() - 2) })
            QVERIFY(capture.pixelColor(corner * scale) != QColor(Qt::red));
    }
    void promptsShowThreeLinesAndCounter()
    {
        GenerationInspectorWidget panel;
        panel.resize(390, 680);
        panel.show();
        for (const auto& name : { QStringLiteral("positivePrompt"), QStringLiteral("negativePrompt") })
        {
            auto* prompt = panel.findChild<PromptEditorWidget*>(name);
            auto* editor = prompt->findChild<Ardoise::PlainTextEdit*>();
            prompt->setText(QStringLiteral("First line\nSecond line\nThird line"));
            QCoreApplication::processEvents();
            QCOMPARE(editor->verticalScrollBar()->maximum(), 0);
            auto* counter = editor->findChild<QLabel*>();
            QVERIFY(counter->isVisible());
            QVERIFY(editor->viewport()->geometry().bottom() < counter->geometry().top());
            prompt->setText(QStringLiteral("First line\nSecond line\nThird line\nFourth line"));
            QCoreApplication::processEvents();
            QVERIFY(editor->verticalScrollBar()->maximum() > 0);
        }
    }
    void restoresLastSubmittedPromptsAcrossNewImage()
    {
        GenerationInspectorWidget panel;
        auto* restore = panel.findChild<QToolButton*>("restorePromptsButton");
        QVERIFY(restore && !restore->isEnabled());
        panel.setPrompts("first positive", "first negative");
        panel.rememberSubmittedPrompts("first positive", "first negative");
        QVERIFY(panel.generationRequest().prompt.isEmpty());
        QVERIFY(panel.generationRequest().negativePrompt.isEmpty());
        panel.resetDraft();
        panel.setPrompts("replacement", "replacement negative");
        restore->click();
        QCOMPARE(panel.generationRequest().prompt, QString("first positive"));
        QCOMPARE(panel.generationRequest().negativePrompt, QString("first negative"));
        panel.rememberSubmittedPrompts("second positive", {});
        restore->click();
        QCOMPARE(panel.generationRequest().prompt, QString("second positive"));
        QVERIFY(panel.generationRequest().negativePrompt.isEmpty());
    }

    void promptCounterUndoAndReset()
    {
        GenerationInspectorWidget panel;
        auto* prompt = panel.findChild<PromptEditorWidget*>(QStringLiteral("positivePrompt"));
        auto* editor = prompt->findChild<Ardoise::PlainTextEdit*>();
        QVERIFY(editor);
        const QString longText(2100, QLatin1Char('x'));
        prompt->setText(longText);
        QCOMPARE(prompt->text(), longText);
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(QStringLiteral("detail"));
        QCOMPARE(prompt->text(), longText + QStringLiteral("detail"));
        editor->undo();
        QCOMPARE(prompt->text(), longText);
        panel.resetDraft();
        QVERIFY(prompt->text().isEmpty());
    }
    void imageEditingToolbarAndCrop()
    {
        GenerationWorkspaceWidget workspace;
        workspace.resize(720, 650); workspace.show();
        auto* fit = workspace.findChild<QToolButton*>("imageFitToggle");
        auto* crop = workspace.findChild<QToolButton*>("imageCrop");
        auto* upscale = workspace.findChild<QToolButton*>("imageUpscale");
        auto* save = workspace.findChild<QToolButton*>("imageSaveAs");
        QVERIFY(fit && crop && upscale && save);
        QVERIFY(!fit->isEnabled()); QVERIFY(!crop->isEnabled()); QVERIFY(!save->isEnabled());
        QImage original(640, 480, QImage::Format_RGB32);
        for (int y = 0; y < original.height(); ++y)
            for (int x = 0; x < original.width(); ++x)
                original.setPixelColor(x, y, QColor(x % 256, y % 256, (x + y) % 256));
        workspace.appendImage(original, "Original");
        QCoreApplication::processEvents();
        QVERIFY(!fit->isChecked()); QVERIFY(fit->isEnabled());
        QCOMPARE(workspace.preview()->zoom(), 1.0);
        workspace.resize(800, 700);
        QCoreApplication::processEvents();
        QCOMPARE(workspace.preview()->zoom(), 1.0);
        fit->click(); QVERIFY(workspace.preview()->isFitToView());
        fit->click(); QCOMPARE(workspace.preview()->zoom(), 1.0);
        workspace.findChild<QToolButton*>("imageZoomIn")->click();
        QCOMPARE(workspace.preview()->zoom(), 1.25); QVERIFY(!fit->isChecked());
        workspace.findChild<QToolButton*>("imageActualSize")->click();
        QCOMPARE(workspace.preview()->zoom(), 1.0);
        workspace.preview()->setZoom(2);
        QVERIFY(workspace.preview()->accessibleDescription().contains("640 × 480 px"));
        QVERIFY(workspace.preview()->accessibleDescription().contains(QLocale().toString(200.0, 'f', 1) + '%'));
        crop->click(); QVERIFY(workspace.preview()->cropEnabled());
        QVERIFY(workspace.preview()->hasFocus());
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        QVERIFY(!crop->isChecked());
        QSignalSpy saved(&workspace, &GenerationWorkspaceWidget::saveAsRequested);
        save->click(); QCOMPARE(saved.count(), 1);
        QVERIFY(!upscale->isEnabled());
        Nukleus::ModelInfo model;
        model.id = "upscaler:test"; model.category = Nukleus::ModelCategory::Upscaler;
        workspace.setDefaultModels({model}, {}, model.id);
        QVERIFY(upscale->isEnabled());
        QSignalSpy upscaleRequests(&workspace, &GenerationWorkspaceWidget::upscaleRequested);
        upscale->click(); QCOMPARE(upscaleRequests.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(upscaleRequests.first().first()), original);
        workspace.setUpscaleEnabled(false); QVERIFY(!upscale->isEnabled());
        QVERIFY(!workspace.findChild<QToolButton*>("imageIdentity"));
        QVERIFY(!workspace.findChild<QToolButton*>("imageInpaint"));
        QVERIFY(!workspace.findChild<QToolButton*>("imagePhotoMakerInpaint"));
        const QRect selection(300, 220, 30, 25);
        auto* preview = workspace.preview();
        preview->centerOn(320, 240);
        const QPoint start = preview->mapFromScene(QPointF(selection.x(), selection.y()));
        const QPoint end = preview->mapFromScene(QPointF(selection.x() + selection.width(), selection.y() + selection.height()));
        crop->click();
        // Reverse drag while zoomed and scrolled must copy source pixels, not a screen capture.
        QTest::mousePress(preview->viewport(), Qt::LeftButton, {}, end);
        QTest::mouseMove(preview->viewport(), start);
        QVERIFY(preview->accessibleDescription().contains("Crop: 30 × 25 px"));
        QVERIFY(preview->accessibleDescription().contains("640 × 480 px"));
        const auto captureDirectory = qEnvironmentVariable("KAIMERA_SCREENSHOT_DIR");
        if (!captureDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(captureDirectory));
            QVERIFY(workspace.grab().save(QDir(captureDirectory).filePath("crop-native-size.png")));
        }
        QTest::mouseRelease(preview->viewport(), Qt::LeftButton, {}, start);
        QCOMPARE(workspace.currentImage(), original.copy(selection));
        QVERIFY(preview->accessibleDescription().contains("30 × 25 px"));
        QVERIFY(!preview->accessibleDescription().contains("Crop:"));
        QVERIFY(!crop->isChecked());
        auto* history = workspace.findChild<GenerationHistoryWidget*>();
        QCOMPARE(history->model()->rowCount(), 2);
        history->view()->setCurrentIndex(history->model()->index(0, 0));
        QCOMPARE(workspace.currentImage(), original);
        crop->click();
        QTest::mouseClick(preview->viewport(), Qt::LeftButton, {}, preview->viewport()->rect().center());
        QCOMPARE(history->model()->rowCount(), 2);
        workspace.clearSelection();
        QVERIFY(preview->accessibleDescription().isEmpty());
        QVERIFY(!crop->isChecked()); QVERIFY(!save->isEnabled()); QVERIFY(!upscale->isEnabled());
        const auto directory = qEnvironmentVariable("KAIMERA_SCREENSHOT_DIR");
        if (!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));
            workspace.appendImage(original, "Original");
            QVERIFY(workspace.grab().save(QDir(directory).filePath("image-tools.png")));
        }
    }

    void previewRepaintsFixedBadgeWhenPanning()
    {
        GenerationPreviewWidget preview;
        preview.resize(400, 300);
        QImage image(1024, 1024, QImage::Format_RGB32);
        image.fill(Qt::white);
        preview.setImage(image);
        preview.show();
        QTest::qWait(20);
        struct PaintObserver : QObject {
            QRegion painted;
            bool eventFilter(QObject*, QEvent* event) override
            {
                if (event->type() == QEvent::Paint)
                    painted += static_cast<QPaintEvent*>(event)->region();
                return false;
            }
        } observer;
        preview.viewport()->installEventFilter(&observer);
        for (auto* scrollBar : {preview.horizontalScrollBar(), preview.verticalScrollBar()}) {
            observer.painted = {};
            scrollBar->setValue(scrollBar->value() + 30);
            QTRY_VERIFY(!observer.painted.isEmpty());
            QVERIFY(QRegion(preview.viewport()->rect()).subtracted(observer.painted).isEmpty());
        }
        QCOMPARE(preview.currentImage(), image);
        // The badge keeps its bottom-left corner unless floating content overlaps it.
        const QRect corner = preview.overlayRect();
        QVERIFY(!corner.isEmpty());
        QCOMPARE(corner.bottom(), preview.viewport()->height() - 9);
        QCOMPARE(corner.left(), 8);
        preview.setOverlayObstacle(QRect(preview.viewport()->width() - 120, preview.viewport()->height() - 60, 100, 50));
        QCOMPARE(preview.overlayRect(), corner);
        const QRect wide(20, preview.viewport()->height() - 60, preview.viewport()->width() - 40, 50);
        preview.setOverlayObstacle(wide);
        QCOMPARE(preview.overlayRect().bottom(), wide.top() - 8);
        QCOMPARE(preview.overlayRect().left(), corner.left());
        preview.setOverlayObstacle({});
        QCOMPARE(preview.overlayRect(), corner);
        QCOMPARE(preview.zoom(), 1.0);
    }

    void fitModePersistsAcrossImagesAndRestart()
    {
        QImage image(1200, 900, QImage::Format_RGB32);
        image.fill(Qt::blue);
        {
            GenerationWorkspaceWidget workspace;
            workspace.resize(500, 400);
            workspace.show();
            workspace.appendImage(image, "First");
            auto* fit = workspace.findChild<QToolButton*>("imageFitToggle");
            QVERIFY(!fit->isChecked());
            fit->click();
            QVERIFY(workspace.preview()->isFitToView());
            QVERIFY(QSettings().value("Preview/FitToView").toBool());
            workspace.appendImage(image.scaled(900, 1200), "Second");
            QVERIFY(fit->isChecked());
            QVERIFY(workspace.preview()->isFitToView());
            workspace.clearSelection();
            QVERIFY(fit->isChecked());
            QVERIFY(QSettings().value("Preview/FitToView").toBool());
        }
        {
            GenerationWorkspaceWidget workspace;
            workspace.resize(500, 400);
            workspace.show();
            auto* fit = workspace.findChild<QToolButton*>("imageFitToggle");
            QVERIFY(fit->isChecked());
            workspace.appendImage(image, "Restored");
            QVERIFY(workspace.preview()->isFitToView());
            QVERIFY(workspace.preview()->zoom() < 1.0);
            fit->click();
            QVERIFY(!QSettings().value("Preview/FitToView").toBool());
            workspace.appendImage(image, "Actual size");
            QCOMPARE(workspace.preview()->zoom(), 1.0);
        }
        GenerationWorkspaceWidget restored;
        restored.appendImage(image, "Actual restored");
        QVERIFY(!restored.findChild<QToolButton*>("imageFitToggle")->isChecked());
        QCOMPARE(restored.preview()->zoom(), 1.0);
    }

    void cropClipsToImageBounds()
    {
        GenerationPreviewWidget preview;
        preview.resize(320, 240); preview.show();
        QImage original(40, 30, QImage::Format_RGB32); original.fill(Qt::blue);
        preview.setImage(original);
        QCoreApplication::processEvents();
        preview.actualSize();
        QSignalSpy cropped(&preview, &GenerationPreviewWidget::imageCropped);
        preview.setCropEnabled(true);
        QTest::mousePress(preview.viewport(), Qt::LeftButton, {}, preview.mapFromScene(QPointF(10, 10)));
        QTest::mouseMove(preview.viewport(), preview.mapFromScene(QPointF(-10, -10)));
        QTest::mouseRelease(preview.viewport(), Qt::LeftButton, {}, preview.mapFromScene(QPointF(-10, -10)));
        QCOMPARE(cropped.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(cropped.first().first()), original.copy(0, 0, 10, 10));
        QCOMPARE(preview.currentImage(), original);
    }

    void previewZoomApi()
    {
        GenerationPreviewWidget preview;
        QImage image(400, 200, QImage::Format_RGB32);
        image.fill(Qt::blue);
        preview.setImage(image);
        QCOMPARE(preview.currentImage(), image);
        preview.setZoom(2.0);
        QCOMPARE(preview.zoom(), 2.0);
        preview.setZoom(100.0);
        QCOMPARE(preview.zoom(), 64.0);
        preview.clear();
        QVERIFY(preview.currentImage().isNull());
    }
    void captureForVisualReview()
    {
        const auto directory = qEnvironmentVariable("KAIMERA_SCREENSHOT_DIR");
        if (directory.isEmpty())
            return;
        QVERIFY(QDir().mkpath(directory));
        if (qEnvironmentVariableIsSet("KAIMERA_STANDARD_PALETTE"))
            qApp->setPalette(qApp->style()->standardPalette());
        QTranslator translator;
        QVERIFY(translator.load(QStringLiteral(":/translations/kaimera_fr.qm")));
        qApp->installTranslator(&translator);
        {
            MainWindow window;
            window.show();
            QTest::qWait(100);
            QVERIFY(window.grab().save(QDir(directory).filePath(QStringLiteral("workspace-fr.png"))));
            window.resize(1100, 700);
            QTest::qWait(100);
            QVERIFY(
                window.grab().save(QDir(directory).filePath(QStringLiteral("workspace-fr-narrow.png"))));
        }
        Ardoise::Theme::installTranslations(*qApp, QLocale(QLocale::French));
        {
            MainWindow window;
            window.resize(1448, 1086);
            auto* workspace = window.findChild<GenerationWorkspaceWidget*>();
            for (int i = 0; i < 5; ++i)
            {
                QImage image(1024, 768, QImage::Format_RGB32);
                QPainter painter(&image);
                QLinearGradient gradient(0, 0, 1024, 768);
                gradient.setColorAt(0, QColor::fromHsv(255 + i * 12, 190, 95));
                gradient.setColorAt(1, QColor::fromHsv(25 + i * 10, 170, 235));
                painter.fillRect(image.rect(), gradient);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(255, 220, 150));
                painter.drawEllipse(QPoint(700, 220), 85, 85);
                painter.setBrush(QColor(25, 35, 58));
                painter.drawPolygon(QPolygon { { 0, 620 }, { 220, 320 }, { 480, 650 }, { 760, 390 },
                    { 1024, 600 }, { 1024, 768 }, { 0, 768 } });
                painter.end();
                workspace->appendImage(image, QStringLiteral("Visual fixture %1").arg(i + 1));
            }
            auto* positive = window.findChild<PromptEditorWidget*>(QStringLiteral("positivePrompt"));
            positive->setText(QStringLiteral(
                "Paysage fantastique au coucher du soleil, grand château médiéval sur une falaise, rivière "
                "et cascades, montagnes lointaines, ciel dramatique et nuages dorés, style anime détaillé, "
                "ambiance épique, très haute qualité, lumineux, cinématique"));
            window.findChild<PromptEditorWidget*>(QStringLiteral("negativePrompt"))
                ->setText(QStringLiteral("low quality, blurry, bad anatomy, text, watermark, logo, "
                                         "cropped, monochrome, low contrast, noise"));
            auto routing = Nukleus::Protocol::starterModelRouting();
            Nukleus::ModelInfo editor;
            editor.id = "visual-editor"; editor.category = Nukleus::ModelCategory::ImageGeneration;
            for (auto& quality : routing.editingQualities)
                quality.model.modelId = editor.id;
            for (auto& preset : routing.presets)
                preset.model.modelId = editor.id;
            window.show();
            QTest::qWait(100);
            workspace->setEditRouting(routing, {editor});
            auto* parameters = window.findChild<GenerationInspectorWidget*>()->parameters();
            parameters->setModels({editor});
            parameters->setRouting(routing, true);
            QTest::qWait(700); // Let the drawer and edit bubble finish sliding in.
            QVERIFY(window.grab().save(QDir(directory).filePath(QStringLiteral("workspace-fr-edit-collapsed.png"))));
            window.findChild<QToolButton*>(QStringLiteral("imageEditExpand"))->click();
            QTest::qWait(500);
            QVERIFY(
                window.grab().save(QDir(directory).filePath(QStringLiteral("workspace-fr-populated.png"))));
            {
                // The longest format name must render in full, without digit grouping.
                auto* picker = window.findChild<ResolutionPicker*>(QStringLiteral("resolutionPicker"));
                auto* resolution = window.findChild<QComboBox*>(QStringLiteral("resolution"));
                int longest = 0;
                for (int i = 1; i < resolution->count(); ++i)
                    if (resolution->itemData(i, Qt::UserRole + 2).toString().size()
                        > resolution->itemData(longest, Qt::UserRole + 2).toString().size())
                        longest = i;
                resolution->setCurrentIndex(longest);
                QTest::qWait(500);
                QVERIFY(picker->grab().save(QDir(directory).filePath(QStringLiteral("resolution-picker-fr.png"))));
                resolution->setCurrentIndex(resolution->findData(QSize(1024, 1024)));
            }
            // The New image tool opens the centred generation card over the image.
            window.findChild<QToolButton*>(QStringLiteral("imageNew"))->click();
            QTest::qWait(700);
            QVERIFY(window.grab().save(QDir(directory).filePath(QStringLiteral("workspace-fr-new-image.png"))));
            window.resize(1300, 700);
            QTest::qWait(200);
            QVERIFY(window.grab().save(QDir(directory).filePath(QStringLiteral("workspace-fr-new-image-small.png"))));
            window.resize(1100, 700);
            QTest::qWait(100);
            QVERIFY(window.grab().save(
                QDir(directory).filePath(QStringLiteral("workspace-fr-populated-narrow.png"))));
        }
        Ardoise::Theme::installTranslations(*qApp, QLocale(QLocale::English));
        qApp->removeTranslator(&translator);
    }
    // The project page's screenshots, in English and French under <dir>/en and <dir>/fr:
    // KAIMERA_SCREENSHOT_DIR=<dir> [KAIMERA_SCREENSHOT_IMAGES=<folder of images>].
    void captureProjectScreenshots()
    {
        const auto directory = qEnvironmentVariable("KAIMERA_SCREENSHOT_DIR");
        if (directory.isEmpty())
            return;
        QList<QImage> images;
        const QDir imageFolder(qEnvironmentVariable("KAIMERA_SCREENSHOT_IMAGES"));
        if (!imageFolder.path().isEmpty() && imageFolder.path() != QStringLiteral("."))
        {
            for (const auto& name : imageFolder.entryList({ "*.png", "*.jpg", "*.jpeg", "*.webp" }, QDir::Files, QDir::Name))
                images.append(QImage(imageFolder.filePath(name)));
        }
        for (int i = images.size(); i < 5; ++i)
        {
            QImage image(1024, 768, QImage::Format_RGB32);
            QPainter painter(&image);
            QLinearGradient gradient(0, 0, 1024, 768);
            gradient.setColorAt(0, QColor::fromHsv(255 + i * 12, 190, 95));
            gradient.setColorAt(1, QColor::fromHsv(25 + i * 10, 170, 235));
            painter.fillRect(image.rect(), gradient);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 220, 150));
            painter.drawEllipse(QPoint(700, 220), 85, 85);
            painter.setBrush(QColor(25, 35, 58));
            painter.drawPolygon(QPolygon { { 0, 620 }, { 220, 320 }, { 480, 650 }, { 760, 390 },
                { 1024, 600 }, { 1024, 768 }, { 0, 768 } });
            painter.end();
            images.append(image);
        }
        struct Texts
        {
            QString language, positive, negative, edit, request, proposal, face, hair;
        };
        const QList<Texts> languages {
            { "en",
                "A medieval castle on a cliff at sunset, rivers and waterfalls below, distant "
                "mountains, dramatic sky with golden clouds, detailed anime style, epic atmosphere",
                "blurry, text, watermark, logo, low contrast",
                "Make it a snowy winter night, with warm light in the castle windows",
                "A cozy reading nook by a rainy window, for a book cover",
                "A cushioned window seat with a knitted blanket, a stack of books and a steaming mug, "
                "rain streaming down the glass and soft lamp light.\nShot: Medium · Ratio: 2:3",
                "An adult woman in her thirties, olive skin, oval face, almond-shaped brown eyes, "
                "thick eyebrows, a few freckles",
                "Shoulder-length chestnut hair, loose waves, side part" },
            { "fr",
                "Un château médiéval sur une falaise au coucher du soleil, rivières et cascades en "
                "contrebas, montagnes lointaines, ciel dramatique aux nuages dorés, style anime détaillé",
                "flou, texte, filigrane, logo, faible contraste",
                "Transforme-la en nuit d'hiver enneigée, avec une lumière chaude aux fenêtres du château",
                "Un coin lecture douillet près d'une fenêtre sous la pluie, pour une couverture de livre",
                "Une banquette de fenêtre avec un plaid tricoté, une pile de livres et une tasse fumante, "
                "la pluie qui ruisselle sur la vitre et la lumière douce d'une lampe.\nCadrage : Taille · Format : 2:3",
                "Une femme adulte d'une trentaine d'années, peau olive, visage ovale, yeux bruns en "
                "amande, sourcils épais, quelques taches de rousseur",
                "Cheveux châtains mi-longs, ondulations souples, raie sur le côté" },
        };
        for (const auto& texts : languages)
        {
            const QString folder = QDir(directory).filePath(texts.language);
            QVERIFY(QDir().mkpath(folder));
            QTranslator translator;
            if (texts.language == QStringLiteral("fr"))
            {
                QVERIFY(translator.load(QStringLiteral(":/translations/kaimera_fr.qm")));
                qApp->installTranslator(&translator);
                Ardoise::Theme::installTranslations(*qApp, QLocale(QLocale::French));
            }
            const auto save = [&](QWidget& window, const QString& name) {
                QTest::qWait(700); // Let panels finish sliding and fading in.
                QVERIFY(window.grab().save(QDir(folder).filePath(name)));
            };
            MainWindow window;
            window.resize(1440, 900);
            auto* workspace = window.findChild<GenerationWorkspaceWidget*>();
            for (int i = 0; i < images.size(); ++i)
                workspace->appendImage(images[i], QStringLiteral("Screenshot image %1").arg(i + 1));
            auto routing = Nukleus::Protocol::starterModelRouting();
            Nukleus::ModelInfo model;
            model.id = "screenshot-model";
            model.category = Nukleus::ModelCategory::ImageGeneration;
            for (auto& quality : routing.editingQualities)
                quality.model.modelId = model.id;
            for (auto& preset : routing.presets)
                preset.model.modelId = model.id;
            window.show();
            QTest::qWait(100);
            workspace->setEditRouting(routing, { model });
            auto* parameters = window.findChild<GenerationInspectorWidget*>()->parameters();
            parameters->setModels({ model });
            parameters->setRouting(routing, true);

            window.findChild<QToolButton*>(QStringLiteral("imageNew"))->click();
            window.findChild<PromptEditorWidget*>(QStringLiteral("positivePrompt"))->setText(texts.positive);
            window.findChild<PromptEditorWidget*>(QStringLiteral("negativePrompt"))->setText(texts.negative);
            save(window, QStringLiteral("1-new-image.png"));
            window.findChild<QToolButton*>(QStringLiteral("imageNew"))->click();

            window.findChild<QToolButton*>(QStringLiteral("imageEditExpand"))->click();
            window.findChild<Ardoise::PlainTextEdit*>(QStringLiteral("imageEditPrompt"))->setPlainText(texts.edit);
            save(window, QStringLiteral("2-edit.png"));

            auto* sparkle = workspace->sparkle();
            // Only the conversation is shown: no planner answers the request.
            QObject::disconnect(sparkle, &KiraSparkle::submitted, nullptr, nullptr);
            sparkle->setExpanded(true);
            sparkle->setSendEnabled(true);
            sparkle->prompt()->setPlainText(texts.request);
            sparkle->sendButton()->click();
            sparkle->addProposal(texts.proposal, {},
                texts.language == QStringLiteral("fr") ? QStringLiteral("Créer l'image") : QStringLiteral("Create the image"));
            save(window, QStringLiteral("3-sparkle.png"));
            sparkle->setExpanded(false);

            window.findChild<QAction*>(QStringLiteral("createPromptIdentityAction"))->trigger();
            for (auto* identityParameters :
                window.findChild<IdentityPanel*>()->findChildren<GenerationParametersWidget*>())
            {
                identityParameters->setModels({ model });
                identityParameters->setRouting(routing, true);
            }
            window.findChild<Ardoise::PlainTextEdit*>(QStringLiteral("identityPrompt"))->setPlainText(texts.face);
            window.findChild<Ardoise::PlainTextEdit*>(QStringLiteral("identityHairPrompt"))->setPlainText(texts.hair);
            save(window, QStringLiteral("4-identity.png"));
            window.close();
            if (texts.language == QStringLiteral("fr"))
            {
                qApp->removeTranslator(&translator);
                Ardoise::Theme::installTranslations(*qApp, QLocale(QLocale::English));
            }
        }
    }
    void animationSizeFollowsStartImage_R8()
    {
        const auto size = &CanvasServiceController::animationSize;
        // Without model defaults: about 480 × 832 pixels, in the start image's ratio, multiples of 16.
        QCOMPARE(size({512, 768}, {}), QSize(512, 768));
        QCOMPARE(size({1216, 832}, {}), QSize(768, 528));
        QCOMPARE(size({1024, 1024}, {}), QSize(624, 624));
        // The model's default width × height sets the area; the start image still sets the ratio.
        const QJsonObject square{{"width", 640}, {"height", 640}};
        QCOMPARE(size({512, 768}, square), QSize(528, 784));
        QCOMPARE(size({1216, 832}, square), QSize(768, 528));
        QCOMPARE(size({300, 300}, square), QSize(640, 640));
        for (const QSize start : {QSize(512, 768), QSize(1216, 832), QSize(7, 3000)}) {
            const QSize video = size(start, {});
            QVERIFY(video.width() % 16 == 0 && video.height() % 16 == 0);
            QVERIFY(video.width() >= 16 && video.height() >= 16);
        }
    }

    void animateFormFollowsSelectionAndService_R1_R2_R3_R4_R5_R6_R15()
    {
        GenerationWorkspaceWidget workspace;
        workspace.resize(900, 700);
        workspace.show();
        auto* animate = workspace.findChild<QToolButton*>("imageAnimate");
        auto* form = workspace.findChild<AnimateForm*>();
        auto* bubble = workspace.findChild<EditBubble*>();
        QVERIFY(animate && form && bubble);
        QCOMPARE(animate->accessibleName(), QString("Animate"));
        // R1: no selection, then a service that cannot animate, then everything needed.
        QVERIFY(!animate->isEnabled());
        QCOMPARE(animate->toolTip(), QString("Select a still image to animate it."));
        const QString issue = "The service cannot animate images — update Nukleus.";
        workspace.setAnimateAvailability(issue, {}, false);
        Nukleus::ModelInfo editor;
        editor.id = "image-generation:edit";
        editor.category = Nukleus::ModelCategory::ImageGeneration;
        workspace.setDefaultModels({editor}, editor.id, {});
        QImage source(64, 96, QImage::Format_RGB32);
        source.fill(Qt::darkGreen);
        workspace.appendImage(source, "Source");
        QVERIFY(!animate->isEnabled());
        QCOMPARE(animate->toolTip(), issue);
        const AnimateForm::Model wan{"animation:wan", "Wan 2.2", true};
        const AnimateForm::Model still{"animation:still", "Still", false};
        workspace.setAnimateAvailability({}, {wan}, false);
        QVERIFY(animate->isEnabled());
        QCOMPARE(animate->toolTip(), QString("Animate"));
        QTRY_VERIFY(bubble->isActive());
        // R2: the form replaces the edit bubble over the preview, for the selected image.
        animate->click();
        QVERIFY(animate->isChecked() && form->isActive());
        QVERIFY(!bubble->isActive());
        QCOMPARE(form->parentWidget(), workspace.preview());
        QTRY_VERIFY(form->isVisible() && workspace.preview()->rect().contains(form->geometry()));
        // Only a prompt, a paperclip and Animate: no identity, quality or other generation option.
        QVERIFY(form->prompt()->isVisible() && form->attachButton()->isVisible() && form->submitButton()->isVisible());
        QVERIFY(!form->findChild<QualityStepSlider*>());
        QVERIFY(!form->findChild<QSpinBox*>() && !form->findChild<QDoubleSpinBox*>());
        QCOMPARE(form->findChildren<QToolButton*>().size(), 2); // The paperclip and the close button.
        QCOMPARE(form->findChildren<QCheckBox*>().size(), 1);
        QCOMPARE(form->findChildren<QComboBox*>().size(), 1);
        // R4, R5: one model and no ping-pong: neither the Loop switch nor the model selector.
        QVERIFY(form->loopSwitch()->isHidden());
        QVERIFY(form->modelSelector()->isHidden());
        QCOMPARE(form->modelId(), wan.id);
        // R3: one end image at most; removing it allows another.
        QImage end(32, 48, QImage::Format_RGB32);
        end.fill(Qt::yellow);
        QVERIFY(form->attachButton()->isEnabled());
        QVERIFY(form->setEndImage(end));
        QVERIFY(!form->attachButton()->isEnabled());
        QVERIFY(!form->setEndImage(source));
        QCOMPARE(form->endImage(), end);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto removes = form->findChildren<QToolButton*>("animateRemoveEndImage");
        QCOMPARE(removes.size(), 1);
        removes.first()->click();
        QVERIFY(form->endImage().isNull());
        QVERIFY(form->attachButton()->isEnabled());
        QVERIFY(form->setEndImage(end));
        // R4, R5: ping-pong shows the Loop switch at the model's default; two models show the selector.
        workspace.setAnimateAvailability({}, {wan, still}, true);
        QVERIFY(!form->loopSwitch()->isHidden());
        QVERIFY(form->loopSwitch()->isChecked());
        QVERIFY(!form->modelSelector()->isHidden());
        QCOMPARE(form->modelSelector()->count(), 2);
        form->modelSelector()->setCurrentIndex(1);
        QCOMPARE(form->modelId(), still.id);
        QVERIFY(!form->loopSwitch()->isChecked());
        form->loopSwitch()->setChecked(true);
        // R6: Animate needs a prompt; Enter submits only then, and Shift+Enter adds a line.
        workspace.setAnimateSubmitAvailable(true);
        QSignalSpy requests(&workspace, &GenerationWorkspaceWidget::animateRequested);
        auto* prompt = form->prompt();
        prompt->setFocus();
        prompt->setPlainText("   ");
        QVERIFY(!form->submitButton()->isEnabled());
        QTest::keyClick(prompt, Qt::Key_Return);
        QCOMPARE(requests.count(), 0);
        QCOMPARE(prompt->toPlainText(), QString("   "));
        prompt->setPlainText("She breathes slowly");
        prompt->moveCursor(QTextCursor::End);
        QTest::keyClick(prompt, Qt::Key_Return, Qt::ShiftModifier);
        QTest::keyClicks(prompt, "and blinks");
        QCOMPARE(prompt->toPlainText(), QString("She breathes slowly\nand blinks"));
        QCOMPARE(requests.count(), 0);
        // A running single job holds Animate back, with a reason.
        workspace.setAnimateSubmitAvailable(false, "Wait for the current job to finish.");
        QVERIFY(!form->submitButton()->isEnabled());
        QCOMPARE(form->submitButton()->toolTip(), QString("Wait for the current job to finish."));
        QTest::keyClick(prompt, Qt::Key_Enter, Qt::KeypadModifier);
        QCOMPARE(requests.count(), 0);
        workspace.setAnimateSubmitAvailable(true);
        QVERIFY(form->submitButton()->isEnabled());
        QTest::keyClick(prompt, Qt::Key_Enter, Qt::KeypadModifier);
        QCOMPARE(requests.count(), 1);
        const auto request = requests.takeFirst();
        QCOMPARE(request.at(0).value<QImage>(), source);
        QCOMPARE(request.at(1).value<QImage>(), end);
        QCOMPARE(request.at(2).toString(), QString("She breathes slowly\nand blinks"));
        QCOMPARE(request.at(3).toString(), still.id);
        QCOMPARE(request.at(4).toBool(), true);
        QCOMPARE(request.at(5).toBool(), true);
        // R7: the form closes and clears its prompt and end image.
        QVERIFY(!animate->isChecked() && !form->isActive());
        QVERIFY(prompt->toPlainText().isEmpty());
        QVERIFY(form->endImage().isNull());
        QTRY_VERIFY(bubble->isActive());
        // Escape closes the form and keeps the draft; without ping-pong, no pingPong is sent.
        workspace.setAnimateAvailability({}, {wan}, false);
        animate->click();
        prompt->setFocus();
        prompt->setPlainText("A slow wave");
        QTest::keyClick(prompt, Qt::Key_Escape);
        QVERIFY(!animate->isChecked() && !form->isActive());
        animate->click();
        QCOMPARE(prompt->toPlainText(), QString("A slow wave"));
        form->submitButton()->click();
        QCOMPARE(requests.count(), 1);
        QCOMPARE(requests.first().at(3).toString(), wan.id);
        QCOMPARE(requests.first().at(4).toBool(), false);
        // Losing the still image closes the form.
        animate->click();
        QVERIFY(form->isActive());
        workspace.clearSelection();
        QVERIFY(!animate->isEnabled() && !animate->isChecked() && !form->isActive());
        // R15: the button reads "Animer" in French.
        QTranslator french;
        QVERIFY(french.load(QStringLiteral(":/translations/kaimera_fr.qm")));
        qApp->installTranslator(&french);
        {
            GenerationWorkspaceWidget translated;
            QCOMPARE(translated.findChild<QToolButton*>("imageAnimate")->accessibleName(), QString("Animer"));
            QCOMPARE(translated.findChild<QPushButton*>("animateButton")->text(), QString("Animer"));
        }
        qApp->removeTranslator(&french);
    }

    void animatesSelectedImageThroughService_R1_R7_R8_R9_R10_R11_R12_R13()
    {
        QFile fixture(QStringLiteral(KAIMERA_TEST_ANIMATION));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const QByteArray video = fixture.readAll();
        QWebSocketServer server("animation-test", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QWebSocket* peer = nullptr;
        QList<QJsonObject> submissions;
        QHash<QString, QList<QByteArray>> uploads;
        QStringList operations{"image.edit", "model-routing", "prompt.enhance"};
        auto routing = Nukleus::Protocol::starterModelRouting();
        for (auto& quality : routing.editingQualities) quality.model.modelId = "image-model";
        const QJsonObject ready{{"id", "animation:wan"}, {"displayName", "Wan 2.2"},
            {"recommendedOptions", QJsonObject{{"vaeModelId", "vae"}, {"textEncoderModelId", "t5"},
                {"width", 640}, {"height", 640}, {"pingPong", true}, {"steps", 4}}}};
        const QJsonObject broken{{"id", "animation:broken"}, {"displayName", "Broken"},
            {"missingFiles", QJsonArray{"vae"}}};
        QJsonArray animationModels{broken};
        const auto send = [&](const QJsonObject& message) {
            peer->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));
        };
        const auto sendCapabilities = [&] {
            send({{"type", "capabilities"}, {"state", "ready"}, {"operations", QJsonArray::fromStringList(operations)}});
        };
        const auto sendModels = [&] {
            send({{"type", "models.list.result"}, {"categories", QJsonArray{
                QJsonObject{{"category", "image-generation"},
                    {"models", QJsonArray{QJsonObject{{"id", "image-model"}, {"displayName", "Image"}}}}},
                QJsonObject{{"category", "llm-chat"}, {"models", QJsonArray{QJsonObject{{"id", "llm-chat:translator"},
                    {"displayName", "Translator"}, {"recommendedOptions", QJsonObject{{"defaultPromptTranslator", true}}}}}}},
                QJsonObject{{"category", "animation"}, {"models", animationModels}}}}});
        };
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this, [&](const QString& text) {
                const auto request = QJsonDocument::fromJson(text.toUtf8()).object();
                if (request.value("type") == "authenticate") {
                    send({{"type", "authenticated"}});
                    sendCapabilities();
                    send({{"type", "model-routing.get.completed"}, {"routing", Nukleus::Protocol::modelRoutingToJson(routing)}});
                }
                if (request.value("type") == "models.list") sendModels();
                if (request.value("type") != "submit") return;
                submissions.append(request);
                if (request.value("operation") == "image.animate")
                    send({{"type", "upload-required"}, {"jobId", request.value("jobId")}, {"inputIndex", 0}});
            });
            // The service asks for the start image, then for the end image when there is one.
            connect(peer, &QWebSocket::binaryMessageReceived, this, [&](const QByteArray& bytes) {
                const auto& job = submissions.last();
                auto& images = uploads[job.value("jobId").toString()];
                images.append(bytes);
                if (images.size() == 1 && job.value("options").toObject().value("hasEndImage").toBool())
                    send({{"type", "upload-required"}, {"jobId", job.value("jobId")}, {"inputIndex", 1}});
            });
        });
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        auto* client = window.findChild<CanvasServiceController*>();
        auto* workspace = window.findChild<GenerationWorkspaceWidget*>();
        auto* history = window.findChild<GenerationHistoryView*>();
        auto* model = workspace->findChild<GenerationHistoryWidget*>()->model();
        auto* animate = window.findChild<QToolButton*>("imageAnimate");
        auto* newImage = window.findChild<QToolButton*>("imageNew");
        auto* form = workspace->animateForm();
        auto* card = window.findChild<GenerationCard*>();
        QVERIFY(animate && newImage && form && card);
        client->setConfiguration(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), {}, false);
        client->connectToService();
        QTRY_VERIFY(client->canEdit());
        QImage start(96, 160, QImage::Format_RGB32);
        start.fill(Qt::darkGreen);
        workspace->appendImage(start, "Start");
        // R1: each missing piece disables Animate with its reason.
        QTRY_COMPARE(animate->toolTip(), QString("The service cannot animate images — update Nukleus."));
        QVERIFY(!animate->isEnabled());
        operations << "image.animate";
        sendCapabilities();
        QTRY_COMPARE(animate->toolTip(), QString("The service cannot make MP4 videos — install ffmpeg where Nukleus runs."));
        operations << "image.animate.mp4" << "image.animate.pingPong";
        sendCapabilities();
        QTRY_COMPARE(animate->toolTip(),
            QString("No animation model with all its files is installed — install one in Nukleus Manager."));
        QVERIFY(!animate->isEnabled());
        animationModels.append(ready);
        sendModels();
        QTRY_VERIFY(animate->isEnabled());
        QCOMPARE(animate->toolTip(), QString("Animate"));
        // R2: the form, the edit bubble and the New image card never coexist.
        newImage->click();
        QVERIFY(card->isActive());
        animate->click();
        QVERIFY(form->isActive() && !card->isActive() && !newImage->isChecked());
        QVERIFY(!workspace->findChild<EditBubble*>()->isActive());
        newImage->click();
        QVERIFY(card->isActive() && !form->isActive() && !animate->isChecked());
        animate->click();
        // R4, R5: the broken model is not offered; the only usable one starts Loop at its default.
        QVERIFY(form->modelSelector()->isHidden());
        QCOMPARE(form->modelId(), QString("animation:wan"));
        QVERIFY(!form->loopSwitch()->isHidden() && form->loopSwitch()->isChecked());
        // R7: translated prompt, start and end images, model, mp4, computed size and pingPong only.
        QImage end(96, 160, QImage::Format_RGB32);
        end.fill(Qt::yellow);
        QVERIFY(form->setEndImage(end));
        form->prompt()->setPlainText("Elle respire lentement");
        QTRY_VERIFY(form->submitButton()->isEnabled());
        form->submitButton()->click();
        QVERIFY(!form->isActive() && form->prompt()->toPlainText().isEmpty() && form->endImage().isNull());
        // R9: the pending entry appears at once, without changing the selection.
        QCOMPARE(model->rowCount(), 2);
        const QUuid first = model->index(1).data(GenerationHistoryModel::IdRole).toUuid();
        QCOMPARE(model->state(first), GenerationHistoryModel::State::Pending);
        QCOMPARE(workspace->currentImage(), start);
        QTRY_COMPARE(submissions.size(), 1);
        auto translation = submissions.takeFirst();
        QCOMPARE(translation.value("operation").toString(), QString("prompt.enhance"));
        QCOMPARE(translation.value("options").toObject().value("prompt").toString(), QString("Elle respire lentement"));
        QCOMPARE(model->index(1).data(GenerationHistoryModel::StatusRole).toString(), QString("Translating the prompt…"));
        send({{"type", "result"}, {"jobId", translation.value("jobId")}, {"operation", "prompt.enhance"},
            {"prompt", "She breathes slowly"}});
        QTRY_COMPARE(submissions.size(), 1);
        const auto submission = submissions.first();
        const auto options = submission.value("options").toObject();
        QCOMPARE(submission.value("operation").toString(), QString("image.animate"));
        QCOMPARE(options.value("modelId").toString(), QString("animation:wan"));
        QCOMPARE(options.value("prompt").toString(), QString("She breathes slowly"));
        QVERIFY(options.value("hasEndImage").toBool());
        QVERIFY(!options.contains("negativePrompt"));
        // R8: the 3:5 start image at the model's 640 × 640 area, in multiples of 16.
        QCOMPARE(options.value("parameters").toObject(), (QJsonObject{{"container", "mp4"}, {"width", 496},
            {"height", 832}, {"pingPong", true}}));
        const QString firstJob = submission.value("jobId").toString();
        QTRY_COMPARE(uploads.value(firstJob).size(), 2);
        QCOMPARE(QImage::fromData(uploads.value(firstJob).at(0)), start);
        QCOMPARE(QImage::fromData(uploads.value(firstJob).at(1)), end);
        // R9: progress shows in the entry; the application stays usable and other jobs queue meanwhile.
        send({{"type", "progress"}, {"jobId", firstJob}, {"progress", QJsonObject{{"stage", "sampling"},
            {"percentage", 40}, {"currentStep", 2}, {"totalSteps", 4}}}});
        QTRY_COMPARE(model->index(1).data(GenerationHistoryModel::ProgressRole).toInt(), 40);
        QCOMPARE(model->index(1).data(GenerationHistoryModel::StatusRole).toString(), QString("Sampling: 2 / 4"));
        QVERIFY(!client->jobActive() && client->canEdit());
        QVERIFY(animate->isEnabled());
        animate->click();
        form->prompt()->setPlainText("Elle respire lentement"); // The cached translation goes straight out.
        form->submitButton()->click();
        QTRY_COMPARE(submissions.size(), 2);
        const auto second = submissions.last();
        QCOMPARE(second.value("operation").toString(), QString("image.animate"));
        QVERIFY(!second.value("options").toObject().value("hasEndImage").toBool());
        QTRY_COMPARE(uploads.value(second.value("jobId").toString()).size(), 1);
        const QUuid secondTile = model->index(2).data(GenerationHistoryModel::IdRole).toUuid();
        QCOMPARE(model->state(secondTile), GenerationHistoryModel::State::Pending);
        // R10: the video becomes an animation entry, shown by its first frame with a video mark.
        send({{"type", "result"}, {"jobId", firstJob}, {"operation", "image.animate"}, {"mimeType", "video/mp4"},
            {"frames", 6}, {"fps", 8}});
        peer->sendBinaryMessage(video);
        QTRY_COMPARE(model->state(first), GenerationHistoryModel::State::Ready);
        QCOMPARE(model->kind(first), GenerationHistoryModel::Kind::Animation);
        QCOMPARE(model->animation(first).video, video);
        QCOMPARE(model->animation(first).size, QSize(64, 48));
        QCOMPARE(model->animation(first).frames, 6);
        QCOMPARE(model->animation(first).fps, 8);
        QVERIFY(model->image(first).isNull());
        QCOMPARE(model->prompt(first), QString("She breathes slowly"));
        const QImage thumbnail = model->index(1).data(Qt::DecorationRole).value<QIcon>().pixmap(QSize(128, 96)).toImage();
        const QColor corner = thumbnail.pixelColor(thumbnail.width() - 3, 2);
        QVERIFY2(corner.red() > 200 && corner.green() < 60 && corner.blue() < 60, qPrintable(corner.name()));
        const QColor mark = thumbnail.pixelColor(thumbnail.width() / 8, thumbnail.height() * 7 / 8);
        QVERIFY2(mark.red() < 200 || mark.green() > 60 || mark.blue() > 60, qPrintable(mark.name()));
        QCOMPARE(workspace->currentImage(), start); // Not selected on arrival.
        // R10: a failure keeps its message.
        send({{"type", "error"}, {"jobId", second.value("jobId")}, {"code", "generation-failed"},
            {"message", "ErrorOutOfDeviceMemory"}});
        QTRY_COMPARE(model->state(secondTile), GenerationHistoryModel::State::Failed);
        QVERIFY(model->index(2).data(GenerationHistoryModel::StatusRole).toString().contains("ErrorOutOfDeviceMemory"));
        // R10: so does an animation cut off by a lost connection.
        animate->click();
        form->prompt()->setPlainText("Elle respire lentement");
        form->submitButton()->click();
        QTRY_COMPARE(submissions.size(), 3);
        const QUuid third = model->index(3).data(GenerationHistoryModel::IdRole).toUuid();
        peer->close();
        QTRY_COMPARE(model->state(third), GenerationHistoryModel::State::Failed);
        QVERIFY(!model->index(3).data(GenerationHistoryModel::StatusRole).toString().isEmpty());
        // R11: selecting the animation plays it in a loop at once; a click pauses and resumes it.
        history->setCurrentIndex(model->index(1));
        QCOMPARE(workspace->currentAnimation(), first);
        auto* preview = workspace->preview();
        QVERIFY(preview->hasAnimation());
        QTRY_VERIFY(preview->isPlaying());
        QTRY_VERIFY(!preview->currentFrame().isNull());
        QCOMPARE(preview->currentFrame().size(), QSize(64, 48));
        QVERIFY(preview->accessibleDescription().startsWith("64 × 48 px · 6 frames · 8 fps · "));
        QTest::mouseClick(preview->viewport(), Qt::LeftButton, {}, preview->viewport()->rect().center());
        QTRY_VERIFY(!preview->isPlaying());
        QTest::mouseClick(preview->viewport(), Qt::LeftButton, {}, preview->viewport()->rect().center());
        QTRY_VERIFY(preview->isPlaying());
        // R12: the actions that need a still image are disabled; viewing and saving stay.
        for (const char* name : {"imageCrop", "imageUpscale", "imageDownscale", "imageSharpen", "imageEditWithIdentity",
                 "imageAnimate"})
            QVERIFY2(!window.findChild<QToolButton*>(name)->isEnabled(), name);
        QTRY_VERIFY(!workspace->findChild<EditBubble*>()->isActive());
        QVERIFY(window.findChild<QToolButton*>("imageSaveAs")->isEnabled());
        QVERIFY(window.findChild<QToolButton*>("imageZoomIn")->isEnabled());
        // R13: Save as offers MP4 first and writes the received bytes unchanged.
        const bool nativeDialogsDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restoreDialogs = qScopeGuard([&] {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogsDisabled);
        });
        QTemporaryDir directory;
        const QString path = directory.filePath("breathing");
        QStringList filters;
        QTimer::singleShot(0, &window, [&] {
            auto* chooser = window.findChild<QFileDialog*>();
            QVERIFY(chooser);
            filters = chooser->nameFilters();
            chooser->selectFile(path);
            QMetaObject::invokeMethod(chooser, "accept", Qt::QueuedConnection);
        });
        window.findChild<QToolButton*>("imageSaveAs")->click();
        QCOMPARE(filters.value(0), QString("MP4 video (*.mp4)"));
        QFile saved(path + ".mp4");
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), video);
        // Selecting a still image again stops the video.
        history->setCurrentIndex(model->index(0));
        QVERIFY(!preview->hasAnimation());
        QVERIFY(workspace->currentAnimation().isNull());
        QCOMPARE(workspace->currentImage(), start);
    }

    void cleanupTestCase() { QSettings().clear(); }
};
QTEST_MAIN(CanvasTest)
#include "CanvasTest.moc"
