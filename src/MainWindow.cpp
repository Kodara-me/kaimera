#include "MainWindow.h"
#include "IdentityPanel.h"
#include "OutputSettingsWidget.h"
#include "CanvasServiceController.h"
#include "GenerationInspectorWidget.h"
#include "GenerationParametersWidget.h"
#include "GenerationPreviewWidget.h"
#include "GenerationWorkspaceWidget.h"
#include "JobActivityIndicator.h"
#include "KiraSparkle.h"
#include "ReferenceImageStrip.h"
#include "ServicePreferencesDialog.h"
#include <Ardoise/Dialog.h>
#include <Ardoise/IconProvider.h>
#include <Ardoise/Theme.h>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QImageReader>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent)
    : Ardoise::MainWindow(parent)
{
    using Ardoise::Icon;
    setWindowTitle(QStringLiteral("Kaimera"));
    setWindowIcon(QIcon(QStringLiteral(":/app-icon/32.png")));
    setMinimumSize(1300, 700);
    resize(1300, 700);
    const auto action = [this](const QString& text, Icon icon, QKeySequence::StandardKey shortcut)
    {
        auto* result = new QAction(Ardoise::IconProvider::icon(icon), text, this);
        if (shortcut != QKeySequence::UnknownKey)
            result->setShortcuts(shortcut);
        addAction(result);
        return result;
    };
    auto* newImage = action(tr("New Image"), Icon::New, QKeySequence::New);
    newImage->setObjectName(QStringLiteral("newImageAction"));
    auto* open = action(tr("Open…"), Icon::Open, QKeySequence::Open);
    m_save = action(tr("Save"), Icon::Save, QKeySequence::Save);
    m_save->setObjectName(QStringLiteral("saveImageAction"));
    m_save->setEnabled(false);
    struct EditCommand
    {
        QString text;
        QKeySequence::StandardKey shortcut;
        void (QPlainTextEdit::*invoke)();
    };
    const QList<EditCommand> commands = { { tr("Undo"), QKeySequence::Undo, &QPlainTextEdit::undo },
        { tr("Redo"), QKeySequence::Redo, &QPlainTextEdit::redo },
        { tr("Cut"), QKeySequence::Cut, &QPlainTextEdit::cut },
        { tr("Copy"), QKeySequence::Copy, &QPlainTextEdit::copy },
        { tr("Paste"), QKeySequence::Paste, &QPlainTextEdit::paste },
        { tr("Select All"), QKeySequence::SelectAll, &QPlainTextEdit::selectAll } };
    for (const auto& command : commands)
    {
        auto* item = new QAction(command.text, this);
        item->setShortcuts(command.shortcut);
        item->setEnabled(false);
        addAction(item);
        connect(qApp, &QApplication::focusChanged, item,
            [this, item](QWidget*, QWidget* current)
            {
                if (auto* editor = qobject_cast<QPlainTextEdit*>(current))
                    m_editTarget = editor;
                else if (current && !qobject_cast<QMenu*>(current))
                    m_editTarget = nullptr;
                item->setEnabled(!m_editTarget.isNull());
            });
        connect(item, &QAction::triggered, this,
            [this, command]
            {
                if (m_editTarget)
                    (m_editTarget.data()->*command.invoke)();
            });
    }
    auto* fullscreen = action(tr("Fullscreen"), Icon::Fullscreen, QKeySequence::FullScreen);
    fullscreen->setObjectName(QStringLiteral("fullscreenAction"));
    fullscreen->setCheckable(true);
    connect(fullscreen, &QAction::toggled, this,
        [this](bool enabled) { enabled ? showFullScreen() : showNormal(); });
    auto* preferences = action(tr("Service Preferences…"), Icon::Settings, QKeySequence::Preferences);
    connect(preferences, &QAction::triggered, this, &MainWindow::showServicePreferences);
    auto* quit = action(tr("Quit"), Icon::Close, QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    m_workspace = new GenerationWorkspaceWidget(this);
    connect(m_workspace, &GenerationWorkspaceWidget::servicePreferencesRequested, preferences, &QAction::trigger);
    m_panel = new GenerationInspectorWidget;
    m_workspace->setGenerationPanel(m_panel);
    setContentWidget(m_workspace);
    // A fresh draft in the New image card, over whatever the preview shows.
    connect(newImage, &QAction::triggered, this,
        [this]
        {
            m_panel->resetDraft();
            m_workspace->showGenerationCard();
        });
    connect(open, &QAction::triggered, this, &MainWindow::openImage);
    connect(m_save, &QAction::triggered, this, &MainWindow::saveImage);
    connect(m_workspace, &GenerationWorkspaceWidget::saveAsRequested, this, &MainWindow::saveImage);
    connect(m_workspace, &GenerationWorkspaceWidget::openRequested, open, &QAction::trigger);
    connect(m_workspace, &GenerationWorkspaceWidget::imageSelectionChanged, m_save, &QAction::setEnabled);
    const auto previewAction = [this](const QString& text, QKeySequence::StandardKey shortcut,
                                   void (GenerationPreviewWidget::*invoke)())
    {
        auto* item = new QAction(text, this);
        if (shortcut != QKeySequence::UnknownKey)
            item->setShortcuts(shortcut);
        item->setEnabled(false);
        addAction(item);
        connect(m_workspace, &GenerationWorkspaceWidget::imageSelectionChanged, item, &QAction::setEnabled);
        connect(item, &QAction::triggered, m_workspace->preview(), invoke);
    };
    previewAction(tr("Fit Image"), QKeySequence::UnknownKey, &GenerationPreviewWidget::fitImage);
    previewAction(tr("Actual Size"), QKeySequence::UnknownKey, &GenerationPreviewWidget::actualSize);
    previewAction(tr("Zoom In"), QKeySequence::ZoomIn, &GenerationPreviewWidget::zoomIn);
    previewAction(tr("Zoom Out"), QKeySequence::ZoomOut, &GenerationPreviewWidget::zoomOut);
    m_serviceClient = new CanvasServiceController(this);
    connect(m_serviceClient, &CanvasServiceController::generationModelsChanged, this,
        [this] {
            m_panel->parameters()->setModels(m_serviceClient->generationModels());

        });
    const auto updateDefaultModels = [this] {
        m_workspace->setDefaultModels(m_serviceClient->models(), {},
            m_serviceClient->defaultUpscalerModel());
        m_workspace->setEditRouting(m_serviceClient->modelRouting(), m_serviceClient->models());
        m_panel->parameters()->setRouting(m_serviceClient->modelRouting(), m_serviceClient->supportsModelRouting());
    };
    connect(m_serviceClient, &CanvasServiceController::modelRoutingChanged, this, updateDefaultModels);
    connect(m_serviceClient, &CanvasServiceController::generationModelsChanged, this, updateDefaultModels);
    connect(m_serviceClient, &CanvasServiceController::defaultModelsChanged, this, updateDefaultModels);
    updateDefaultModels();
    const auto updateGenerationAvailability = [this]
    {
        m_panel->setGenerationAvailable(m_panel->parameters()->routingAvailable()
            && m_serviceClient->canGenerate(m_panel->parameters()->modelId()));
        const QString improveIssue = m_serviceClient->enhancementIssue();
        m_panel->setPromptAssistAvailable(improveIssue.isEmpty(), improveIssue);
        m_workspace->setEditImproveIssue(improveIssue);
        m_panel->parameters()->setRoutingLocked(m_serviceClient->jobActive());
        m_workspace->setUpscaleEnabled(m_serviceClient->canUpscale());
        m_workspace->setSharpenEnabled(m_serviceClient->canSharpen());
        m_workspace->setEditEnabled(m_serviceClient->canEdit());
        m_workspace->setRoutingLocked(m_serviceClient->jobActive());
        const QString sparkleIssue = m_serviceClient->planIssue();
        m_workspace->sparkle()->setSendEnabled(sparkleIssue.isEmpty(), sparkleIssue);
        QList<AnimateForm::Model> animationModels;
        for (const auto& model : m_serviceClient->animationModels())
        {
            const auto& options = model.recommendedOptions;
            const auto count = [&](const char* key) {
                return options.contains(key) ? std::optional<int>(options.value(key).toInt()) : std::nullopt;
            };
            animationModels.append({model.id, model.displayLabel(), options.value("pingPong").toBool(),
                count("videoFrames"), count("fps")});
        }
        m_workspace->setAnimateAvailability(m_serviceClient->animateIssue(), animationModels,
            m_serviceClient->canPingPong());
        // The prompt's translation is a single job: another one running holds it back.
        m_workspace->setAnimateSubmitAvailable(!m_serviceClient->jobActive(),
            m_serviceClient->jobActive() ? tr("Wait for the current job to finish.") : QString());
    };
    connect(m_serviceClient, &CanvasServiceController::generationAvailabilityChanged, this, updateGenerationAvailability);
    connect(m_panel, &GenerationInspectorWidget::draftChanged, this, updateGenerationAvailability);
    connect(m_serviceClient, &CanvasServiceController::defaultModelsChanged, this, updateGenerationAvailability);
    connect(m_serviceClient, &CanvasServiceController::modelRoutingChanged, this, updateGenerationAvailability);
    connect(m_serviceClient, &CanvasServiceController::modelsChanged, this, updateGenerationAvailability);
    updateGenerationAvailability();
    auto* createIdentity = action(tr("Create identity from a prompt…"), Icon::Person, QKeySequence::UnknownKey);
    createIdentity->setObjectName(QStringLiteral("createPromptIdentityAction"));
    connect(createIdentity, &QAction::triggered, m_workspace, &GenerationWorkspaceWidget::showIdentityPanel);
    m_identity = new IdentityPanel(m_serviceClient);
    m_workspace->setIdentityPanel(m_identity);
    connect(m_identity, &IdentityPanel::translationRequested, this, [this](const QString& description) {
        m_identity->setAwaitingTranslation(true);
        startActivity(m_identity->activity(), tr("Generating portraits"));
        // The English description continues straight into the portraits.
        const bool started = m_serviceClient->translate({description},
            [this](bool succeeded, const QStringList& english, const QString& message) {
                m_identity->receiveTranslation(succeeded, succeeded ? english.first() : QString());
                if (!succeeded && m_activity) {
                    m_activity->fail(tr("Portrait generation failed"), message);
                    m_workspace->showIdentityPanel();
                }
            });
        if (!started) {
            m_identity->setAwaitingTranslation(false);
            m_activity->fail(tr("Portrait generation failed"), tr("The service could not accept the translation request."));
        }
    });
    connect(m_identity, &IdentityPanel::portraitRequested, this,
        [this](const Nukleus::ImageGenerationRequest& request, int view, int views) {
            const QString title = views == 1 ? tr("Generating portrait")
                                             : tr("Generating portrait %1 of %2").arg(view + 1).arg(views);
            // The description was already translated once for the whole set.
            if (m_serviceClient->submitGeneration(request)) {
                m_identity->setAwaitingPortrait(true);
                startActivity(m_identity->activity(), title);
            } else if (m_activity == m_identity->activity()) {
                m_activity->fail(tr("Generation failed"), tr("The service could not accept the portrait request."));
            }
        });
    connect(m_identity, &IdentityPanel::identityRequested, this,
        [this](const Nukleus::IdentityPackage& identity) {
            QString error;
            const auto package = Nukleus::packIdentity(identity, &error);
            if (package.isEmpty()) { showMessage(tr("Could not save identity"), error); return; }
            QFileDialog dialog(this, tr("Save identity"));
            dialog.setAcceptMode(QFileDialog::AcceptSave);
            dialog.setNameFilter(tr("Kira identity (*.kiraid)"));
            dialog.setDefaultSuffix("kiraid");
            dialog.selectFile(QStringLiteral("identity.kiraid"));
            if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
            QSaveFile file(dialog.selectedFiles().first());
            if (!file.open(QIODevice::WriteOnly)
                || file.write(package) != package.size() || !file.commit())
                showMessage(tr("Could not save identity"), file.errorString());
        });
    // The generation joins the service queue behind a progress tile in the history; the
    // ticket names the tile, null when the service refused it.
    const auto queueGeneration = [this](const Nukleus::ImageGenerationRequest& request,
                                     const QList<QImage>& references, const QString& identityPrompt,
                                     ShotPicker::Shot shot, bool translatePrompts = true) {
        const QUuid ticket = QUuid::createUuid();
        m_workspace->addPendingImage(ticket, tr("Generated image"), tr("Submitting…"));
        if (m_serviceClient->queueGeneration(ticket, request, references, identityPrompt, ShotPrompts::forShot(shot,
                m_serviceClient->promptFormat(request.modelId) == PromptInstructions::Format::Tags), translatePrompts))
            return ticket;
        m_workspace->removeImage(ticket);
        return QUuid();
    };
    // Edits queue the same way: a progress tile that the edited image replaces without selecting it.
    const auto queueEdit = [this](const QImage& image, const QString& prompt, const QList<QImage>& references,
                               const QString& qualityId, bool translatePrompt) {
        const QUuid ticket = QUuid::createUuid();
        m_workspace->addPendingImage(ticket, tr("Edited image"), tr("Submitting…"));
        if (m_serviceClient->queueEdit(ticket, image, prompt, references, qualityId, translatePrompt))
            return ticket;
        m_workspace->removeImage(ticket);
        return QUuid();
    };
    connect(m_panel, &GenerationInspectorWidget::generationRequested, this,
        [this, queueGeneration]
        {
            const auto request = m_panel->generationRequest();
            // References the preset's model cannot use stay attached but are not sent.
            const bool references = m_panel->referencesUsed();
            if (!queueGeneration(request, references ? m_panel->references()->references() : QList<QImage>(),
                    references ? m_panel->identityPrompt() : QString(), m_panel->shotPicker()->currentShot()).isNull())
                m_panel->rememberSubmittedPrompts(request.prompt, request.negativePrompt);
        });
    // Kira Sparkle carries out its creation proposal: the card's preset and parameters with the
    // plan's size, shot and English prompts (the card itself is untouched) and the references it
    // was proposed with, when the preset can use them. The delivered image becomes the base.
    const auto createProposal = [this, queueGeneration] {
        if (!m_sparkleProposal) return;
        const auto proposal = *m_sparkleProposal;
        m_sparkleProposal.reset();
        auto* sparkle = m_workspace->sparkle();
        auto request = m_panel->parameters()->generationRequest();
        if (const QSize size = m_panel->parameters()->resolutionForRatio(proposal.plan.ratio); size.isValid()) {
            request.width = size.width();
            request.height = size.height();
        }
        request.prompt = proposal.plan.prompt;
        request.negativePrompt = proposal.plan.negativePrompt;
        const QUuid ticket = queueGeneration(request,
            m_panel->referencesUsed() ? proposal.references : QList<QImage>(), QString(), proposal.plan.shot, false);
        if (ticket.isNull()) {
            sparkle->addReply(tr("The service could not accept the generation."));
            return;
        }
        sparkle->closeProposal(proposal.id, tr("Created"));
        m_sparkleCreations.insert(ticket, proposal.plan.prompt);
        m_workspace->showSparkleResult(ticket, QSize(request.width, request.height));
    };
    connect(m_workspace->sparkle(), &KiraSparkle::proposalAccepted, this, [this, createProposal](const QUuid& id) {
        if (m_sparkleProposal && m_sparkleProposal->id == id) createProposal();
    });
    // Kira Sparkle's first answer: the default prompt enhancer chooses one action for the request.
    // Answers, follow-up questions and written prompts show in the chat; a new image is proposed
    // first, then created once accepted (its button, or a written acceptance); editing and looking
    // at the images are only announced for now, without running.
    connect(m_workspace, &GenerationWorkspaceWidget::sparkleRequested, this,
        [this, createProposal](const QString& text, const QImage& base, const QList<QImage>& references) {
            auto* sparkle = m_workspace->sparkle();
            // Accepting the latest proposal in words works like its button, without the LLM (which
            // takes changes such as "yes but at night" for acceptances).
            if (m_sparkleProposal && m_sparkleProposal->last && SparklePlanner::isAcceptance(text)) {
                createProposal();
                return;
            }
            auto context = sparkleContext(m_workspace->currentImage(), !base.isNull(), int(references.size()));
            context.language = SparklePlanner::requestLanguage(text);
            context.proposal = m_sparkleProposal && m_sparkleProposal->last;
            if (!base.isNull() && base.cacheKey() == m_sparkleBaseKey) context.basePrompt = m_sparkleBasePrompt;
            const auto fail = [this, sparkle, text](const QString& message) {
                m_sparklePlanning = false;
                sparkle->stopThinking();
                sparkle->addReply(message);
                sparkle->restoreRequest(text);
            };
            // A failure left in the pill belongs to a finished job.
            if (m_activity) m_activity->dismiss();
            m_activity = nullptr;
            m_sparklePlanning = true;
            const bool started = m_serviceClient->plan(SparklePlanner::instructions(context), m_sparkleHistory, text,
                SparklePlanner::schema(context),
                [this, sparkle, context, text, references, fail](bool succeeded,
                    const QJsonObject& reply, const QString& message) {
                    using Action = SparklePlanner::Plan::Action;
                    QString error = message;
                    const auto plan = succeeded ? SparklePlanner::parse(reply, context, &error) : std::nullopt;
                    if (!plan) { fail(error); return; }
                    m_sparklePlanning = false;
                    sparkle->stopThinking();
                    // Remembered only once answered, so a failed request leaves no trace.
                    rememberSparkleExchange(text, reply);
                    if (m_sparkleProposal) m_sparkleProposal->last = false;
                    if (plan->action != Action::Generate) {
                        sparkle->addReply(sparkleAnswer(*plan, context));
                        return;
                    }
                    // A new proposal replaces the one still waiting.
                    if (m_sparkleProposal) sparkle->closeProposal(m_sparkleProposal->id, tr("Replaced"));
                    QString details = tr("Prompt: %1").arg(plan->prompt);
                    if (!plan->negativePrompt.isEmpty())
                        details += QStringLiteral("\n") + tr("Negative prompt: %1").arg(plan->negativePrompt);
                    m_sparkleProposal = SparkleProposal{
                        sparkle->addProposal(sparkleAnswer(*plan, context), details, tr("Create the image")), *plan,
                        references};
                });
            if (!started)
                fail(tr("Kira Sparkle could not start planning your request."));
        });
    connect(m_workspace->sparkle(), &KiraSparkle::conversationCleared, this, [this] {
        m_sparkleHistory.clear();
        m_sparkleProposal.reset();
        m_sparkleCreations.clear();
        m_sparkleBaseKey = 0;
        m_sparkleBasePrompt.clear();
    });
    connect(m_serviceClient, &CanvasServiceController::queuedGenerationStatusChanged, m_workspace,
        &GenerationWorkspaceWidget::setPendingProgress);
    connect(m_serviceClient, &CanvasServiceController::queuedGenerationFinished, this,
        [this](const QUuid& ticket, const Nukleus::ImageJobResult& result) {
            const auto image = result.succeeded ? QImage::fromData(result.imageData) : QImage();
            const QString sparklePrompt = m_sparkleCreations.take(ticket);
            if (image.isNull()) {
                m_workspace->failPendingImage(ticket,
                    result.succeeded ? tr("The service returned an unreadable image.") : result.message);
                return;
            }
            m_workspace->completePendingImage(ticket, image, result.prompt, result.negativePrompt);
            // Kira Sparkle keeps working on what it just created: it replaces the base image.
            if (!sparklePrompt.isEmpty()) {
                m_workspace->sparkle()->setBaseImage(image);
                m_sparkleBaseKey = m_workspace->sparkle()->baseImage().cacheKey();
                m_sparkleBasePrompt = sparklePrompt;
            }
        });
    // Sparkles: the default prompt enhancer rewrites the texts in place, sharing the job pill.
    using Kind = PromptInstructions::Kind;
    const auto improve = [this](JobActivityIndicator* indicator, const QString& title, const QStringList& texts,
                             const QList<Kind>& kinds, std::function<void(const QStringList&)> apply,
                             const PromptInstructions::Composition& composition = {}) {
        const bool started = m_serviceClient->enhance(texts, kinds,
            [this, apply](bool succeeded, const QStringList& improved, const QString& message) {
                if (!succeeded) {
                    if (m_activity) m_activity->fail(tr("Prompt improvement failed"), message);
                    return;
                }
                apply(improved);
                if (m_activity) m_activity->finish();
            }, composition);
        if (started) startActivity(indicator, title);
    };
    connect(m_panel, &GenerationInspectorWidget::promptAssistRequested, this, [this, improve] {
        const auto request = m_panel->generationRequest();
        // The preset's model sets how the prompt is written: sentences, phrases or Danbooru tags.
        improve(m_workspace->activity(), tr("Improving the prompts"), {request.prompt, request.negativePrompt},
            {Kind::Image, Kind::Avoid}, [this](const QStringList& improved) { m_panel->setPrompts(improved.at(0), improved.at(1)); },
            // The selected shot and format tell the enhancer where details matter.
            {m_panel->shotPicker()->currentShot(), {request.width, request.height},
                m_panel->parameters()->resolutionRatio(), m_serviceClient->promptFormat(request.modelId)});
    });
    connect(m_workspace, &GenerationWorkspaceWidget::editImproveRequested, this,
        [this, improve](const QString& instruction) {
            improve(m_workspace->activity(), tr("Improving the instruction"), {instruction}, {Kind::Edit},
                [this](const QStringList& improved) { m_workspace->setEditInstruction(improved.first()); });
        });
    connect(m_identity, &IdentityPanel::improveRequested, this,
        [this, improve](const QString& face, const QString& hair) {
            improve(m_identity->activity(), tr("Improving the descriptions"), {face, hair},
                {Kind::Face, Kind::Hair}, [this](const QStringList& improved) { m_identity->setDescriptions(improved.at(0), improved.at(1)); });
        });
    connect(m_workspace, &GenerationWorkspaceWidget::sharpenRequested, this,
        [this](const QImage& image) {
            if (m_serviceClient->submitSharpen(image))
                startActivity(m_workspace->activity(), tr("Improving sharpness"));
        });
    connect(m_workspace, &GenerationWorkspaceWidget::upscaleRequested, this,
        [this](const QImage& image) {
            if (m_serviceClient->submitUpscale(image))
                startActivity(m_workspace->activity(), tr("Upscaling image"));
        });
    connect(m_workspace, &GenerationWorkspaceWidget::editRequested, this,
        queueEdit);
    // Animations queue like edits: a progress tile that the animation replaces without selecting it.
    connect(m_workspace, &GenerationWorkspaceWidget::animateRequested, this,
        [this](const QImage& start, const QImage& end, const QString& prompt, const QString& modelId,
            bool sendPingPong, bool pingPong, int frames, int fps) {
            const QUuid ticket = QUuid::createUuid();
            m_workspace->addPendingImage(ticket, tr("Animation"), tr("Submitting…"));
            if (!m_serviceClient->queueAnimation(ticket, start, end, prompt, modelId,
                    sendPingPong ? std::optional<bool>(pingPong) : std::nullopt, frames, fps))
                m_workspace->removeImage(ticket);
        });
    connect(m_serviceClient, &CanvasServiceController::queuedAnimationFinished, m_workspace,
        &GenerationWorkspaceWidget::completePendingAnimation);
    connect(m_serviceClient, &CanvasServiceController::generationStatusChanged, this,
        [this](const QString& message, int percentage)
        {
            if (m_sparklePlanning)
                m_workspace->sparkle()->setThinking(message, percentage);
            else if (m_activity)
                m_activity->setProgress(message, percentage);
        });
    connect(m_serviceClient, &CanvasServiceController::generationFinished, this,
        [this](const Nukleus::ImageJobResult& result)
        {
            const auto image = result.succeeded ? QImage::fromData(result.imageData) : QImage();
            const bool fromPrompt = m_identity->awaitingPortrait();
            if (fromPrompt)
                m_identity->setAwaitingPortrait(false);
            if (!result.succeeded || image.isNull())
            {
                if (m_activity)
                    m_activity->fail(result.operation == Nukleus::ImageOperation::Upscale ? tr("Upscale failed")
                        : result.operation == Nukleus::ImageOperation::Edit ? tr("Edit failed")
                        : tr("Generation failed"),
                        result.succeeded ? tr("The service returned an unreadable image.") : result.message);
                if (fromPrompt)
                    m_workspace->showIdentityPanel();
                return;
            }
            if (fromPrompt) {
                if (m_identity->receivePortrait(image)) return;
                // Bring a finished set back into view if the panel was closed meanwhile.
                m_workspace->showIdentityPanel();
            } else {
                m_workspace->appendImage(image, result.operation == Nukleus::ImageOperation::Upscale
                    ? tr("Upscaled image") : result.operation == Nukleus::ImageOperation::Edit
                        ? tr("Edited image") : tr("Generated image"), result.prompt, result.negativePrompt);
            }
            if (m_activity)
                m_activity->finish();
        });
    m_serviceClient->connectToService();
    restoreWindowState();
}

SparklePlanner::Context MainWindow::sparkleContext(const QImage& image, bool base, int references) const
{
    SparklePlanner::Context context;
    context.base = base;
    context.references = references;
    context.canLook = m_serviceClient->canDescribeImages();
    if (!image.isNull()) {
        context.imageSize = image.size();
        context.imagePrompt = m_workspace->currentImagePrompt();
    }
    // Only the preset and editing quality the user chose, when they can run now.
    const auto routing = m_serviceClient->modelRouting();
    if (!routing) return context;
    const auto* parameters = m_panel->parameters();
    if (const auto* preset = parameters->selectedPreset();
        preset && parameters->routingAvailable() && m_serviceClient->canGenerate(preset->model.modelId))
        context.preset = SparklePlanner::Preset{preset->id, preset->name, preset->positivePrefix,
            m_serviceClient->promptFormat(preset->model.modelId), parameters->presetUsesReferences(preset->id)};
    if (!m_serviceClient->canEdit()) return context;
    const auto models = m_serviceClient->models(Nukleus::ModelCategory::ImageGeneration);
    for (const auto& quality : routing->editingQualities)
        if (quality.id == m_workspace->editQualityId())
            for (const auto& model : models)
                if (!quality.model.modelId.isEmpty() && model.id == quality.model.modelId
                    && model.recommendedOptions.value("canEdit").toBool(true))
                    context.qualityId = quality.id;
    return context;
}

void MainWindow::rememberSparkleExchange(const QString& request, const QJsonObject& answer)
{
    m_sparkleHistory.append({QStringLiteral("user"), request.trimmed()});
    m_sparkleHistory.append({QStringLiteral("assistant"),
        QString::fromUtf8(QJsonDocument(answer).toJson(QJsonDocument::Compact))});
    while (m_sparkleHistory.size() > 2 * SparkleMemoryExchanges) m_sparkleHistory.removeFirst();
}

QString MainWindow::sparkleAnswer(const SparklePlanner::Plan& plan, const SparklePlanner::Context& context) const
{
    using Action = SparklePlanner::Plan::Action;
    const auto avoid = [&plan] {
        return plan.negativePrompt.isEmpty() ? QString() : QStringLiteral("\n") + tr("Avoid: %1").arg(plan.negativePrompt);
    };
    switch (plan.action) {
    case Action::Reply: return plan.message;
    case Action::Ask: return plan.question;
    case Action::Prompt:
        return (plan.message.isEmpty() ? QString() : plan.message + QStringLiteral("\n\n")) + plan.prompt + avoid();
    case Action::Generate: {
        // The proposal in the user's language; the English prompts are its tooltip.
        const auto* parameters = m_panel->parameters();
        const auto* preset = parameters->selectedPreset();
        return plan.description + QStringLiteral("\n\n")
            + tr("Shot: %1 · Ratio: %2")
                  .arg(m_panel->shotPicker()->shotName(plan.shot),
                      parameters->resolutionForRatio(plan.ratio).isValid() ? plan.ratio : parameters->resolutionRatio())
            + (plan.avoid.isEmpty() ? QString() : QStringLiteral("\n") + tr("Avoid: %1").arg(plan.avoid))
            + (context.references > 0 && context.preset && !context.preset->references
                    ? QStringLiteral("\n") + tr("%1 cannot use reference images: they will be left out.")
                                                 .arg(preset ? preset->displayLabel() : context.preset->name)
                    : QString());
    }
    case Action::Edit:
        if (!context.hasEditTarget())
            return tr("Attach the image to change as the base image with the + button, then send your request again.");
        return (context.base ? tr("Next step, not run yet: edit the base image.")
                             : tr("Next step, not run yet: edit the displayed image."))
            + QStringLiteral("\n") + tr("Instruction: %1").arg(plan.instruction);
    case Action::Look: {
        QStringList images;
        for (const auto& image : plan.images)
            images.append(image == QLatin1String("base") ? tr("the base image")
                    : image == QLatin1String("displayed") ? tr("the displayed image")
                    : tr("reference %1").arg(image.section(QLatin1Char(' '), 1)));
        return tr("Next step, not run yet: show %1 to the image describer.").arg(images.join(QStringLiteral(", ")))
            + QStringLiteral("\n") + tr("Question: %1").arg(plan.question);
    }
    }
    return {};
}

void MainWindow::startActivity(JobActivityIndicator* indicator, const QString& title)
{
    // A failure left on the other surface belongs to a finished job.
    if (m_activity && m_activity != indicator)
        m_activity->dismiss();
    m_activity = indicator;
    indicator->start(title);
}
void MainWindow::showMessage(const QString& title, const QString& message)
{
    Ardoise::Dialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setWindowIcon(windowIcon());
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    auto* label = new QLabel(message, content);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    layout->addWidget(label);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, content);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.setContentWidget(content);
    dialog.exec();
}
void MainWindow::closeEvent(QCloseEvent* event)
{
    Ardoise::MainWindow::closeEvent(event);
    if (event->isAccepted())
        saveWindowState();
}

void MainWindow::changeEvent(QEvent* event)
{
    Ardoise::MainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
    {
        if (auto* action = findChild<QAction*>(QStringLiteral("fullscreenAction")))
        {
            const QSignalBlocker blocker(action);
            action->setChecked(isFullScreen());
        }
    }
}

void MainWindow::showServicePreferences()
{
    ServicePreferencesDialog dialog(m_serviceClient, this);
    if (dialog.exec() == QDialog::Accepted)
        m_serviceClient->connectToService();
}

void MainWindow::restoreWindowState()
{
    QSettings settings;
    const auto geometry = settings.value(QStringLiteral("MainWindow/Geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
}

void MainWindow::saveWindowState()
{
    QSettings settings;
    settings.setValue(QStringLiteral("MainWindow/Geometry"), saveGeometry());
}

void MainWindow::openImage()
{
    QStringList patterns;
    for (const auto& format : QImageReader::supportedImageFormats())
        patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Open Image"), { }, tr("Images (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (path.isEmpty())
        return;
    QString error;
    if (!m_workspace->openImage(path, &error))
        showMessage(tr("Could not open image"), error);
}

void MainWindow::saveImage()
{
    // An animation is saved as the MP4 video the service returned, unchanged.
    if (!m_workspace->currentAnimation().isNull()) {
        QFileDialog dialog(this, tr("Save Animation"));
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setDirectory(OutputSettingsWidget::preferredDirectory());
        dialog.setNameFilters({tr("MP4 video (*.mp4)")});
        dialog.setDefaultSuffix(QStringLiteral("mp4"));
        if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
            return;
        QString error;
        if (!m_workspace->saveAnimation(dialog.selectedFiles().first(), &error))
            showMessage(tr("Could not save animation"), error);
        return;
    }
    const auto format = OutputSettingsWidget::preferredFormat();
    if (format.isEmpty())
        return;
    const auto suffix = QString::fromLatin1(format);
    QFileDialog dialog(this, tr("Save Image"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDirectory(OutputSettingsWidget::preferredDirectory());
    dialog.setNameFilter(tr("%1 image (*.%2)").arg(suffix.toUpper(), suffix));
    dialog.setDefaultSuffix(suffix);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    QString error;
    if (!m_workspace->saveImage(dialog.selectedFiles().first(), format, &error))
        showMessage(tr("Could not save image"), error);
}
