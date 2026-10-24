#include "CanvasServiceController.h"
#include "IdentityPrompts.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <nukleus/ServiceProtocol.h>

#include <QSettings>
#include <QStandardPaths>
#include <QBuffer>
#include <QJsonObject>

namespace
{
const QString EndpointKey = QStringLiteral("Service/Endpoint");
const QString TokenKey = QStringLiteral("Service/Token");
}

CanvasServiceController::CanvasServiceController(QObject *parent)
    : QObject(parent)
    , m_client(new Nukleus::ServiceClient(this))
{
    m_serviceMessage = tr("Not connected to service.");
    QSettings settings;
    m_endpoint = QUrl(settings.value(EndpointKey, QStringLiteral("ws://127.0.0.1:%1").arg(Nukleus::Protocol::DefaultPort)).toString());
    m_token = settings.value(TokenKey).toString();
    m_client->setConfiguration(m_endpoint, m_token);

    connect(m_client, &Nukleus::ServiceClient::modelsChanged, this, &CanvasServiceController::generationModelsChanged);
    connect(m_client, &Nukleus::ServiceClient::modelsChanged, this, &CanvasServiceController::modelsChanged);
    connect(m_client, &Nukleus::ServiceClient::modelRoutingChanged, this, &CanvasServiceController::modelRoutingChanged);
    connect(m_client, &Nukleus::ServiceClient::modelRoutingChanged, this, &CanvasServiceController::generationAvailabilityChanged);
    connect(m_client, &Nukleus::ServiceClient::capabilitiesChanged, this, &CanvasServiceController::modelRoutingChanged);
    connect(m_client, &Nukleus::ServiceClient::serviceStateChanged, this, &CanvasServiceController::handleState);
    connect(m_client, &Nukleus::ServiceClient::modelsChanged, this, &CanvasServiceController::generationAvailabilityChanged);
    connect(m_client, &Nukleus::ServiceClient::capabilitiesChanged, this, &CanvasServiceController::generationAvailabilityChanged);
    connect(m_client, &Nukleus::ServiceClient::promptEnhancementFinished, this,
        [this](const Nukleus::PromptEnhancementResult& result) {
            if (result.jobId != m_generationJobId || !m_textsDone) return;
            m_generationJobId.clear();
            const QString rewritten = PromptInstructions::withoutAddedWeights(
                m_passTexts.value(m_passIndex), result.prompt).trimmed();
            if (!result.succeeded || rewritten.isEmpty()) {
                finishTextPass(false, !result.succeeded
                    ? (m_enhancing ? tr("Prompt improvement failed: %1") : tr("Prompt translation failed: %1"))
                          .arg(result.message)
                    : m_enhancing ? tr("The prompt enhancer returned an empty prompt.")
                                  : tr("The translator returned an empty prompt."));
                return;
            }
            // A looping answer repeats a phrase until its token limit: never keep it. An improvement
            // retries once, since sampling may not loop again.
            if (PromptInstructions::repeatsItself(rewritten)) {
                if (m_enhancing && !m_passRetried) {
                    m_passRetried = true;
                    m_passMissing.clear();
                    m_passInvented.clear();
                    if (!continueTextPass()) finishTextPass(false, tr("Could not submit the prompt improvement."));
                    return;
                }
                finishTextPass(false, m_enhancing ? tr("The prompt enhancer repeated itself in a loop. Try again.")
                                                  : tr("The translator repeated itself in a loop. Try again."));
                return;
            }
            // Improvements must stay in the text's language; never replace a draft with a translation.
            // Danbooru tags are English by design, so neither this nor the kept-words check applies.
            using PromptInstructions::Language;
            const bool tags = passWritesTags();
            const auto source = PromptInstructions::detectLanguage(m_passTexts.at(m_passIndex));
            const auto answer = PromptInstructions::detectLanguage(rewritten);
            if (m_enhancing && !tags && source != Language::Unknown && answer != Language::Unknown && source != answer) {
                finishTextPass(false, tr("The prompt enhancer answered in another language. Try again."));
                return;
            }
            // Improvements keep every element and invent none (an image prompt may add visible
            // details, but no people): one retry names what was dropped or made up, then it fails
            // and the draft is kept.
            // With the user's Danbooru dictionary, tag answers take their exact Danbooru spelling;
            // for improvements, the unknown tags take the place of the dropped words in the retry.
            QString written = rewritten;
            QStringList unknownTags;
            if (tags) {
                if (const auto* dictionary = danbooru()) {
                    const auto normalized = dictionary->normalize(rewritten);
                    written = normalized.prompt;
                    // The retry names each unknown tag with its closest existing tags.
                    for (const QString& unknown : normalized.unknown) {
                        QStringList closest;
                        for (const auto* tag : dictionary->suggestions(unknown))
                            closest.append(DanbooruDictionary::promptForm(tag->name));
                        unknownTags.append(closest.isEmpty() ? unknown
                            : QStringLiteral("%1 (%2)").arg(unknown, closest.join(QStringLiteral(", "))));
                    }
                }
            }
            if (m_enhancing) {
                const QString& source = m_passTexts.at(m_passIndex);
                const auto missing = tags ? unknownTags : PromptInstructions::missingWords(source, rewritten);
                const auto invented = tags ? PromptInstructions::inventedTagWords(source, rewritten)
                    : PromptInstructions::inventedWords(source, rewritten,
                          m_passKinds.value(m_passIndex) != PromptInstructions::Kind::Image);
                if ((!missing.isEmpty() || !invented.isEmpty()) && !m_passRetried) {
                    m_passRetried = true;
                    m_passMissing = missing;
                    m_passInvented = invented;
                    if (!continueTextPass()) finishTextPass(false, tr("Could not submit the prompt improvement."));
                    return;
                }
                if (!invented.isEmpty()) {
                    finishTextPass(false, tr("The prompt enhancer invented: %1. Try again.")
                                              .arg(invented.join(QStringLiteral(", "))));
                    return;
                }
                // Unknown tags after the retry are kept: the model may still read them.
                if (!missing.isEmpty() && !tags) {
                    finishTextPass(false, tr("The prompt enhancer left out: %1. Try again.")
                                              .arg(missing.join(QStringLiteral(", "))));
                    return;
                }
                m_passRetried = false;
                m_passMissing.clear();
                m_passInvented.clear();
            }
            QString& text = m_passTexts[m_passIndex++];
            if (!m_enhancing) m_translations.insert(translationKey(m_passModel, text, m_passComposition.format), written);
            text = written;
            if (!continueTextPass())
                finishTextPass(false, m_enhancing ? tr("Could not submit the prompt improvement.")
                                                  : tr("Could not submit the prompt translation."));
        });
    connect(m_client, &Nukleus::ServiceClient::chatFinished, this,
        [this](const Nukleus::TextChatResult& result) {
            if (result.jobId != m_generationJobId || !m_planDone) return;
            // The plan may start the next job, so the guard is released first.
            m_generationJobId.clear();
            const auto done = std::exchange(m_planDone, nullptr);
            if (!result.succeeded)
                done(false, {}, tr("Kira Sparkle could not understand the request: %1").arg(result.message));
            else if (!result.json.isObject())
                done(false, {}, tr("Kira Sparkle returned an invalid plan."));
            else
                done(true, result.json.toObject(), {});
            emit generationAvailabilityChanged();
        });
    connect(m_client, &Nukleus::ServiceClient::jobOutput, this,
        [this](const QString &jobId, const QString &stream, const QString &text)
        {
            const auto queued = m_queued.find(jobId);
            if ((queued == m_queued.end() && jobId != m_generationJobId) || text.trimmed().isEmpty())
                return;
            if (stream == QStringLiteral("stderr") || text.contains(QStringLiteral("[ERROR"), Qt::CaseInsensitive))
                (queued != m_queued.end() ? queued->diagnostic : m_generationDiagnostic) = text.trimmed().left(512);
        });
    connect(m_client, &Nukleus::ServiceClient::jobStateChanged, this,
        [this](const QString &jobId, Nukleus::JobState state, int position)
        {
            const auto queued = m_queued.constFind(jobId);
            if (queued == m_queued.cend() && jobId != m_generationJobId)
                return;
            QString message;
            if (state == Nukleus::JobState::Queued)
                message = position >= 0 ? tr("Queued (position %1)").arg(position) : tr("Queued…");
            else if (state == Nukleus::JobState::Running)
                message = queued != m_queued.cend() ? (queued->animation ? tr("Animating…") : tr("Generating…"))
                    : m_upscaling ? tr("Upscaling…")
                    : m_planDone ? tr("Understanding your request…")
                    : m_textsDone ? passStatus() : tr("Generating…");
            else
                return;
            if (queued != m_queued.cend())
                emit queuedGenerationStatusChanged(queued->ticket, message, -1);
            else if (!m_passTicket.isNull())
                emit queuedGenerationStatusChanged(m_passTicket, message, -1);
            else
                emit generationStatusChanged(message, -1);
        });
    connect(m_client, &Nukleus::ServiceClient::generationProgressChanged, this,
        [this](const QString &jobId, const Nukleus::GenerationProgress &progress)
        {
            if (const auto queued = m_queued.constFind(jobId); queued != m_queued.cend()) {
                emit queuedGenerationStatusChanged(queued->ticket, progressMessage(progress, queued->animation),
                    qBound(0, progress.percentage, 100));
                return;
            }
            if (jobId != m_generationJobId)
                return;
            // LLM jobs report no percentage, so the ring spins.
            if (m_planDone) {
                emit generationStatusChanged(tr("Understanding your request…"), -1);
                return;
            }
            if (m_textsDone) {
                emitPassStatus();
                return;
            }
            emit generationStatusChanged(progressMessage(progress), qBound(0, progress.percentage, 100));
        });
    connect(m_client, &Nukleus::ServiceClient::imageJobFinished, this,
        [this](const Nukleus::ImageJobResult &result)
        {
            if (const auto queued = m_queued.constFind(result.jobId); queued != m_queued.cend()) {
                const auto job = *queued;
                m_queued.erase(queued);
                auto canvasResult = result;
                if (!result.succeeded && !job.diagnostic.isEmpty())
                    canvasResult.message += QLatin1Char('\n') + job.diagnostic;
                resizeResult(canvasResult, job.outputSize);
                emit queuedGenerationFinished(job.ticket, canvasResult);
                return;
            }
            if (result.jobId != m_generationJobId)
                return;
            auto canvasResult = result;
            if (!result.succeeded && !m_generationDiagnostic.isEmpty())
                canvasResult.message += QLatin1Char('\n') + m_generationDiagnostic;
            resizeResult(canvasResult, m_outputSize);
            m_upscaling = false;
            m_outputSize = {};
            m_generationDiagnostic.clear();
            m_generationJobId.clear();
            emit generationFinished(canvasResult);
            emit generationAvailabilityChanged();
        });
    // Animations only ever queue; a lost connection fails them here too.
    connect(m_client, &Nukleus::ServiceClient::animationFinished, this,
        [this](const Nukleus::AnimationResult& result) {
            const auto queued = m_queued.constFind(result.jobId);
            if (queued == m_queued.cend()) return;
            const auto job = *queued;
            m_queued.erase(queued);
            auto canvasResult = result;
            if (!result.succeeded && !job.diagnostic.isEmpty())
                canvasResult.message += QLatin1Char('\n') + job.diagnostic;
            emit queuedAnimationFinished(job.ticket, canvasResult, job.prompt);
        });
}

void CanvasServiceController::resizeResult(Nukleus::ImageJobResult& result, const QSize& size)
{
    if (!result.succeeded || size.isEmpty()) return;
    QImage image = QImage::fromData(result.imageData);
    if (image.isNull() || image.size() == size) return;
    image = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG")) {
        result.imageData = bytes;
    } else {
        result.succeeded = false;
        result.message = tr("Could not encode the result image.");
    }
}

QUrl CanvasServiceController::endpoint() const { return m_endpoint; }
QString CanvasServiceController::token() const { return m_token; }
void CanvasServiceController::setConfiguration(const QUrl &endpoint, const QString &token, bool persist)
{
    m_endpoint = endpoint;
    m_token = token;
    m_client->setConfiguration(endpoint, token);
    if (persist) {
        QSettings settings;
        settings.setValue(EndpointKey, endpoint.toString());
        settings.setValue(TokenKey, token);
    }
}
void CanvasServiceController::connectToService() { m_client->connectToService(); }
void CanvasServiceController::handleState(Nukleus::ServiceConnectionState state, const QString &code, const QString &detail)
{
    QString message = detail;
    QString canvasCode = code;
    if (code == QStringLiteral("authentication-failed")) {
        message = tr("Service rejected the shared token. Check Service Preferences and save the exact token again.");
        canvasCode = QStringLiteral("authentication-failed");
    } else if (code == QStringLiteral("connection-error")) {
        message = tr("Could not connect to %1: %2").arg(m_endpoint.toString(), detail);
        canvasCode = QStringLiteral("connection-error");
    } else if (code == QStringLiteral("unavailable")) {
        message = tr("Connected, but the service backend is unavailable. Check the service journal for sd-cli errors.");
        canvasCode = QStringLiteral("unavailable");
    }
    switch (state) {
    case Nukleus::ServiceConnectionState::Connecting: message = tr("Connecting to service…"); canvasCode = QStringLiteral("connecting"); break;
    case Nukleus::ServiceConnectionState::Authenticated:
        if (code != QStringLiteral("unavailable")) {
            message = tr("Connected to service");
            canvasCode = QStringLiteral("connected");
        }
        break;
    case Nukleus::ServiceConnectionState::Ready: message = tr("Service is ready"); canvasCode = QStringLiteral("ready"); break;
    case Nukleus::ServiceConnectionState::Disconnected:
        if (code == QStringLiteral("disconnected") || message.isEmpty()) message = tr("Service connection was closed");
        break;
    case Nukleus::ServiceConnectionState::Error: if (message.isEmpty()) message = tr("Could not connect to service"); break;
    }
    m_serviceMessage = message;
    emit serviceStateChanged(canvasCode, message);
    emit generationAvailabilityChanged();
}

QList<Nukleus::ModelInfo> CanvasServiceController::generationModels() const
{
    auto models = m_client->models(Nukleus::ModelCategory::ImageGeneration);
    models.removeIf([](const auto& model) { return !model.recommendedOptions.value("canGenerate").toBool(true); });
    return models;
}

bool CanvasServiceController::allowsExplicitModels() const
{
    return QSettings().value(QStringLiteral("Service/AllowExplicitModels"), false).toBool();
}

void CanvasServiceController::setAllowsExplicitModels(bool allowed)
{
    if (allowed == allowsExplicitModels())
        return;
    QSettings().setValue(QStringLiteral("Service/AllowExplicitModels"), allowed);
    emit generationModelsChanged();
    emit generationAvailabilityChanged();
}

QList<Nukleus::ModelInfo> CanvasServiceController::models(Nukleus::ModelCategory category) const { return m_client->models(category); }
QString CanvasServiceController::defaultPromptEnhancerModelId() const { return m_client->defaultPromptEnhancerModelId(); }
QString CanvasServiceController::defaultPromptTranslatorModelId() const { return m_client->defaultPromptTranslatorModelId(); }
bool CanvasServiceController::isAuthenticated() const { return m_client->isAuthenticated(); }
bool CanvasServiceController::modelListAvailable() const { return m_client->modelListAvailable(); }
QString CanvasServiceController::serviceMessage() const { return m_serviceMessage; }
void CanvasServiceController::requestModels() { m_client->requestModels(); }

bool CanvasServiceController::canGenerate(const QString &modelId) const
{
    const auto available = generationModels();
    if (std::none_of(available.cbegin(), available.cend(), [&](const auto& model) { return model.id == modelId; }))
        return false;
    if (!m_generationJobId.isEmpty() || !m_client->canSubmit(Nukleus::ImageOperation::Generate, modelId))
        return false;
    return true;
}

QString CanvasServiceController::translatorId() const
{
    const QString id = m_client->defaultPromptTranslatorModelId();
    return !id.isEmpty() && m_client->canEnhancePrompt(id) ? id : QString();
}

QString CanvasServiceController::translationKey(const QString& translator, const QString& text,
    PromptInstructions::Format format)
{
    const bool tags = format == PromptInstructions::Format::Tags;
    return translator + QChar(0) + (tags ? QStringLiteral("tags") : QString()) + QChar(0) + text.trimmed();
}

std::optional<QStringList> CanvasServiceController::immediateTranslation(const QStringList& texts,
    PromptInstructions::Format format) const
{
    const QString translator = translatorId();
    QStringList translated = texts;
    for (QString& text : translated) {
        if (translator.isEmpty() || text.trimmed().isEmpty()) continue;
        const auto cached = m_translations.constFind(translationKey(translator, text, format));
        if (cached == m_translations.cend()) return std::nullopt;
        text = *cached;
    }
    return translated;
}

bool CanvasServiceController::translate(const QStringList& texts, TextsDone done, PromptInstructions::Format format)
{
    m_passComposition = {};
    m_passComposition.format = format;
    return startTextPass(translatorId(), false, texts, std::move(done));
}

PromptInstructions::Format CanvasServiceController::promptFormat(const QString& modelId) const
{
    for (const auto& model : m_client->models(Nukleus::ModelCategory::ImageGeneration))
        if (model.id == modelId) return PromptInstructions::formatFromName(model.promptFormat());
    return PromptInstructions::Format::Phrases;
}

const DanbooruDictionary* CanvasServiceController::danbooru()
{
    const QString path = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("danbooru.csv"));
    if (path.isEmpty()) return nullptr;
    if (path != m_danbooruPath) {
        m_danbooruPath = path;
        if (!m_danbooru.load(path, 100)) m_danbooru = {};
    }
    return m_danbooru.isEmpty() ? nullptr : &m_danbooru;
}

bool CanvasServiceController::passWritesTags() const
{
    using PromptInstructions::Kind;
    if (m_passComposition.format != PromptInstructions::Format::Tags) return false;
    const Kind kind = m_passKinds.value(m_passIndex);
    return !m_enhancing || kind == Kind::Image || kind == Kind::Avoid;
}

QString CanvasServiceController::enhancerId() const
{
    const QString id = m_client->defaultPromptEnhancerModelId();
    return !id.isEmpty() && m_client->canEnhancePrompt(id) ? id : QString();
}

QString CanvasServiceController::enhancementIssue() const
{
    if (jobActive()) return tr("Wait for the current job to finish.");
    const QString id = m_client->defaultPromptEnhancerModelId();
    if (id.isEmpty())
        return tr("No LLM is marked as the default prompt enhancer — choose one in Service Manager.");
    if (!m_client->canEnhancePrompt(id)) return tr("The default prompt enhancer is unavailable.");
    return {};
}

QString CanvasServiceController::planIssue() const
{
    if (jobActive()) return tr("Wait for the current job to finish.");
    const QString id = m_client->defaultPromptEnhancerModelId();
    if (id.isEmpty())
        return tr("No LLM is marked as the default prompt enhancer — choose one in Service Manager.");
    if (!m_client->canChat(id))
        return tr("The default prompt enhancer cannot answer requests — check Nukleus and its model.");
    return {};
}

bool CanvasServiceController::plan(const QString& instructions, const QList<Nukleus::TextChatMessage>& history,
    const QString& request, const QJsonObject& schema, PlanDone done)
{
    if (!planIssue().isEmpty() || m_textsDone || m_planDone || request.trimmed().isEmpty()) return false;
    // The schema constrains the reply to one JSON plan. The enhancer's saved sampling applies, and
    // the repetition penalty stays off so the request's words survive into the prompts.
    QList<Nukleus::TextChatMessage> messages{{QStringLiteral("system"), instructions}};
    messages += history;
    messages.append({QStringLiteral("user"), request});
    m_generationJobId = m_client->submitChat({m_client->defaultPromptEnhancerModelId(), messages,
        {{"jsonSchema", schema}, {"penalizeRepeats", false}}, {}});
    if (m_generationJobId.isEmpty()) return false;
    m_generationDiagnostic.clear();
    m_upscaling = false;
    m_planDone = std::move(done);
    emit generationStatusChanged(tr("Understanding your request…"), -1);
    emit generationAvailabilityChanged();
    return true;
}

bool CanvasServiceController::enhance(const QStringList& texts, const QList<PromptInstructions::Kind>& kinds,
    TextsDone done, const PromptInstructions::Composition& composition)
{
    const QString id = enhancerId();
    if (id.isEmpty() || kinds.size() != texts.size()
        || std::all_of(texts.cbegin(), texts.cend(), [](const QString& t) { return t.trimmed().isEmpty(); }))
        return false;
    m_passKinds = kinds;
    m_passComposition = composition;
    return startTextPass(id, true, texts, std::move(done));
}

bool CanvasServiceController::startTextPass(const QString& model, bool enhancing, const QStringList& texts,
    TextsDone done)
{
    if (jobActive() || m_textsDone) return false;
    m_passModel = model;
    m_enhancing = enhancing;
    m_passTexts = texts;
    m_passIndex = 0;
    m_textsDone = std::move(done);
    m_generationDiagnostic.clear();
    m_upscaling = false;
    if (continueTextPass()) return true;
    m_passTexts.clear();
    m_textsDone = nullptr;
    return false;
}

QString CanvasServiceController::passStatus() const
{
    return m_enhancing ? tr("Improving the prompt…") : tr("Translating the prompt…");
}

void CanvasServiceController::emitPassStatus()
{
    if (m_passTicket.isNull())
        emit generationStatusChanged(passStatus(), -1);
    else
        emit queuedGenerationStatusChanged(m_passTicket, passStatus(), -1);
}

QString CanvasServiceController::progressMessage(const Nukleus::GenerationProgress& progress, bool video)
{
    using Stage = Nukleus::GenerationProgressStage;
    if (video && progress.stage == Stage::Decoding) return tr("Decoding video…");
    if (video && (progress.stage == Stage::Saving || progress.stage == Stage::Complete)) return tr("Receiving video…");
    switch (progress.stage)
    {
    case Stage::EnhancingPrompt: return tr("Preparing the prompt…");
    case Stage::Preparing: return tr("Preparing…");
    case Stage::Starting: return tr("Starting…");
    case Stage::LoadingModel: return tr("Loading model…");
    case Stage::Sampling:
        return progress.currentStep && progress.totalSteps
            ? tr("Sampling: %1 / %2").arg(*progress.currentStep).arg(*progress.totalSteps)
            : tr("Sampling…");
    case Stage::Decoding: return tr("Decoding image…");
    case Stage::Saving:
    case Stage::Complete: return tr("Receiving image…");
    case Stage::Failed: return tr("Generation failed.");
    }
    return {};
}

void CanvasServiceController::finishTextPass(bool succeeded, const QString& message)
{
    m_passTicket = {};
    m_passRetried = false;
    m_passMissing.clear();
    m_passInvented.clear();
    const auto texts = std::exchange(m_passTexts, {});
    std::exchange(m_textsDone, nullptr)(succeeded, succeeded ? texts : QStringList(), message);
    emit generationAvailabilityChanged();
}

bool CanvasServiceController::continueTextPass()
{
    for (; m_passIndex < m_passTexts.size(); ++m_passIndex) {
        const QString& text = m_passTexts.at(m_passIndex);
        if (m_passModel.isEmpty() || text.trimmed().isEmpty()) continue;
        if (!m_enhancing) {
            if (const auto cached = m_translations.constFind(translationKey(m_passModel, text, m_passComposition.format));
                cached != m_translations.cend()) {
                m_passTexts[m_passIndex] = *cached;
                continue;
            }
        }
        // Greedy decoding keeps translations literal and repeatable. Improvements use the enhancer's
        // own saved temperature: models loop or drift at temperatures far from their publisher's.
        // Both reuse the input's words, so the service's prompt-wide repetition penalty (which
        // drifts toward synonyms or English) stays off.
        const QString instructions = m_enhancing
            ? PromptInstructions::improvement(m_passKinds.value(m_passIndex), PromptInstructions::detectLanguage(text),
                                              m_passMissing, m_passInvented, m_passComposition)
            : passWritesTags() ? PromptInstructions::danbooruTranslation() : PromptInstructions::translation();
        QJsonObject parameters{{"systemPrompt", instructions}, {"penalizeRepeats", false}};
        if (!m_enhancing) parameters.insert("temperature", 0.0);
        m_generationJobId = m_client->enhancePrompt({m_passModel, text, parameters});
        if (m_generationJobId.isEmpty()) return false;
        emitPassStatus();
        emit generationAvailabilityChanged();
        return true;
    }
    finishTextPass(true, {});
    return true;
}

void CanvasServiceController::failJob(const QString& message, Nukleus::ImageOperation operation)
{
    Nukleus::ImageJobResult failed;
    failed.operation = operation;
    failed.message = message;
    emit generationFinished(failed);
    emit generationAvailabilityChanged();
}

bool CanvasServiceController::submitGeneration(const Nukleus::ImageGenerationRequest &request)
{
    if (!canGenerate(request.modelId)) return false;
    m_upscaling = false;
    m_outputSize = {};
    m_generationDiagnostic.clear();
    m_generationJobId = m_client->submitGeneration(request);
    if (m_generationJobId.isEmpty()) return false;
    emit generationStatusChanged(tr("Submitting…"), -1);
    emit generationAvailabilityChanged();
    return true;
}

bool CanvasServiceController::queueGeneration(const QUuid& ticket, const Nukleus::ImageGenerationRequest &request,
    const QList<QImage>& references, const QString& identityPrompt, const ShotPrompts::Prefix& shot,
    bool translatePrompts)
{
    if (ticket.isNull() || !canGenerate(request.modelId)) return false;
    auto submitted = request;
    for (const QImage& reference : references) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly) || !reference.save(&buffer, "PNG")) return false;
        submitted.referenceImages.append(bytes);
    }
    const QStringList texts{request.prompt, request.negativePrompt};
    // Tag models receive both prompts as English Danbooru tags. Prompts made only of tags the
    // dictionary knows (as Sparkles writes them) skip the translator, in their Danbooru spelling.
    const auto format = promptFormat(request.modelId);
    if (const auto* dictionary = format == PromptInstructions::Format::Tags ? danbooru() : nullptr) {
        const auto positive = dictionary->normalize(request.prompt);
        const auto negative = dictionary->normalize(request.negativePrompt);
        if (positive.unknown.isEmpty() && negative.unknown.isEmpty()) {
            submitted.prompt = positive.prompt;
            submitted.negativePrompt = negative.prompt;
            return submitQueuedNow(ticket, submitted, identityPrompt, shot);
        }
    }
    if (!translatePrompts) return submitQueuedNow(ticket, submitted, identityPrompt, shot);
    if (const auto immediate = immediateTranslation(texts, format)) {
        submitted.prompt = immediate->at(0);
        submitted.negativePrompt = immediate->at(1);
        return submitQueuedNow(ticket, submitted, identityPrompt, shot);
    }
    // The translation holds the single-job guard; the generation then joins the service queue.
    m_passTicket = ticket;
    const bool started = translate(texts, [this, ticket, submitted, identityPrompt, shot](bool succeeded,
                                              const QStringList& english, const QString& message) mutable {
        if (!succeeded) { failQueued(ticket, message); return; }
        submitted.prompt = english.at(0);
        submitted.negativePrompt = english.at(1);
        if (!canGenerate(submitted.modelId) || !submitQueuedNow(ticket, submitted, identityPrompt, shot))
            failQueued(ticket, tr("Could not submit the generation."));
    }, format);
    if (!started) m_passTicket = {};
    return started;
}

bool CanvasServiceController::submitQueuedNow(const QUuid& ticket, Nukleus::ImageGenerationRequest request,
    const QString& identityPrompt, const ShotPrompts::Prefix& shot)
{
    // The service places the style's prefixes before these framing clauses.
    request.prompt = ShotPrompts::prepend(shot.positive, request.prompt);
    request.negativePrompt = ShotPrompts::prepend(shot.negative, request.negativePrompt);
    if (!identityPrompt.trimmed().isEmpty())
        request.prompt = IdentityPrompts::appendClause(request.prompt, identityPrompt);
    const QString jobId = m_client->submitGeneration(request);
    if (jobId.isEmpty()) return false;
    m_queued.insert(jobId, {ticket, {}, {}, false, {}});
    emit queuedGenerationStatusChanged(ticket, tr("Submitting…"), -1);
    return true;
}

void CanvasServiceController::failQueued(const QUuid& ticket, const QString& message,
    Nukleus::ImageOperation operation)
{
    Nukleus::ImageJobResult failed;
    failed.operation = operation;
    failed.message = message;
    emit queuedGenerationFinished(ticket, failed);
}

QString CanvasServiceController::defaultEditionModel() const
{
    return QSettings().value(QStringLiteral("Service/DefaultEditionModel")).toString();
}

QString CanvasServiceController::defaultUpscalerModel() const
{
    const auto available = models(Nukleus::ModelCategory::Upscaler);
    return available.isEmpty() ? QString() : available.first().id;
}

QString CanvasServiceController::defaultLlmModel() const
{
    return QSettings().value(QStringLiteral("Service/DefaultLlmModel")).toString();
}

void CanvasServiceController::setDefaultLlmModel(const QString& id)
{
    if (id == defaultLlmModel()) return;
    QSettings().setValue(QStringLiteral("Service/DefaultLlmModel"), id);
    emit defaultModelsChanged();
}

bool CanvasServiceController::canUpscale() const
{
    const QString id = defaultUpscalerModel();
    const auto available = models(Nukleus::ModelCategory::Upscaler);
    const bool eligible = std::any_of(available.cbegin(), available.cend(), [&](const auto& model) {
        return model.id == id;
    });
    return eligible && m_generationJobId.isEmpty()
        && m_client->canSubmit(Nukleus::ImageOperation::Upscale, id);
}

bool CanvasServiceController::submitUpscale(const QImage& image)
{
    if (image.isNull() || !canUpscale())
        return false;
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return false;
    m_generationDiagnostic.clear();
    m_generationJobId = m_client->submitUpscale({bytes, defaultUpscalerModel()});
    if (m_generationJobId.isEmpty())
        return false;
    m_upscaling = true;
    m_outputSize = image.size() * 2;
    emit generationStatusChanged(tr("Submitting…"), -1);
    emit generationAvailabilityChanged();
    return true;
}

std::optional<Nukleus::ModelRouting> CanvasServiceController::modelRouting() const { return m_client->modelRouting(); }
bool CanvasServiceController::supportsModelRouting() const { return m_client->capabilities().modelRouting; }
bool CanvasServiceController::jobActive() const { return !m_generationJobId.isEmpty(); }
bool CanvasServiceController::canEdit() const
{
    return supportsModelRouting() && m_generationJobId.isEmpty() && m_client->capabilities().imageEdit;
}

bool CanvasServiceController::canDescribeImages() const
{
    const QString id = m_client->defaultImageDescriberModelId();
    return !id.isEmpty() && m_client->canChatWithImages(id);
}

std::optional<Nukleus::ImageGenerationRequest> CanvasServiceController::editRequest(const QImage& image,
    const Nukleus::EditingQuality& quality, const QList<QImage>& references) const
{
    if (image.isNull() || !canEdit() || !m_client->canSubmit(Nukleus::ImageOperation::Edit, quality.model.modelId))
        return std::nullopt;
    Nukleus::ImageGenerationRequest request;
    request.modelId = quality.model.modelId;
    request.width = image.width();
    request.height = image.height();
    for (const auto& model : models(Nukleus::ModelCategory::ImageGeneration)) {
        if (model.id != request.modelId) continue;
        const auto& options = model.recommendedOptions;
        request.steps = options.value("steps").toInt(request.steps);
        request.cfgScale = options.value("cfgScale").toDouble(request.cfgScale);
        request.sampler = options.value("sampler").toString();
        request.scheduler = options.value("scheduler").toString();
    }
    if (quality.model.steps) request.steps = *quality.model.steps;
    if (quality.model.cfgScale) request.cfgScale = *quality.model.cfgScale;
    for (const QImage& input : QList<QImage>{image} + references) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly) || !input.save(&buffer, "PNG")) return std::nullopt;
        request.editImages.append(bytes);
    }
    return request;
}

bool CanvasServiceController::canSharpen() const
{
    const auto routing = modelRouting();
    const auto* quality = routing ? routing->defaultEditingQuality() : nullptr;
    return quality && canEdit() && m_client->canSubmit(Nukleus::ImageOperation::Edit, quality->model.modelId);
}

bool CanvasServiceController::submitSharpen(const QImage& image)
{
    // Complete, fixed instructions for the editing model; no LLM and no quality prefixes. The pass
    // is deliberately light: users who want more can run it again on the result.
    static const QString positive = QStringLiteral(
        "Slightly improve the sharpness and clarity of this image with a subtle, natural touch: gently "
        "sharpen soft areas and lightly reduce noise and compression artifacts, keeping the existing level "
        "of detail and texture. The result must look almost identical to the original, just a little "
        "crisper. Keep exactly the same composition, framing, subjects, faces, identities, poses, "
        "expressions, colours, lighting and background; do not add, remove or change anything.");
    static const QString negative = QStringLiteral(
        "oversharpened, overprocessed, excessive detail, added details, added texture, artificial texture, "
        "harsh edges, halos, ringing artifacts, HDR look, high contrast, plastic skin, blurry, noise, "
        "jpeg artifacts, changed composition, changed face, different person, added objects, removed "
        "objects, colour shift, distorted, watermark, text");
    const auto routing = modelRouting();
    const auto* quality = routing ? routing->defaultEditingQuality() : nullptr;
    if (!quality) return false;
    auto request = editRequest(image, *quality, {});
    if (!request) return false;
    // Without qualityId the service edits with this model directly and adds no prefixes.
    request->prompt = positive;
    request->negativePrompt = negative;
    return submitEditNow(*request, image.size());
}

bool CanvasServiceController::queueEdit(const QUuid& ticket, const QImage& image, const QString& prompt,
    const QList<QImage>& references, const QString& qualityId, bool translatePrompt)
{
    const auto routing = modelRouting();
    const auto* quality = routing ? routing->editingQuality(qualityId) : nullptr;
    if (ticket.isNull() || prompt.trimmed().isEmpty() || !quality) return false;
    auto edit = editRequest(image, *quality, references);
    if (!edit) return false;
    auto request = *edit;
    request.qualityId = quality->id;
    request.prompt = prompt;
    const QSize outputSize = image.size();
    const auto immediate = translatePrompt ? immediateTranslation({prompt}) : std::optional<QStringList>({prompt});
    if (immediate) {
        request.prompt = immediate->first();
        return submitQueuedEditNow(ticket, request, outputSize);
    }
    // The translation holds the single-job guard; the edit then joins the service queue.
    m_passTicket = ticket;
    const bool started = translate({prompt}, [this, ticket, request, outputSize](bool succeeded,
                                                 const QStringList& english, const QString& message) mutable {
        if (!succeeded) { failQueued(ticket, message, Nukleus::ImageOperation::Edit); return; }
        request.prompt = english.first();
        if (!canEdit() || !submitQueuedEditNow(ticket, request, outputSize))
            failQueued(ticket, tr("Could not submit the edit."), Nukleus::ImageOperation::Edit);
    });
    if (!started) m_passTicket = {};
    return started;
}

bool CanvasServiceController::submitQueuedEditNow(const QUuid& ticket, const Nukleus::ImageGenerationRequest& request,
    const QSize& outputSize)
{
    const QString jobId = m_client->submitEdit(request);
    if (jobId.isEmpty()) return false;
    m_queued.insert(jobId, {ticket, {}, outputSize, false, {}});
    emit queuedGenerationStatusChanged(ticket, tr("Submitting…"), -1);
    return true;
}

QList<Nukleus::ModelInfo> CanvasServiceController::animationModels() const
{
    auto models = m_client->models(Nukleus::ModelCategory::Animation);
    models.removeIf([](const auto& model) { return !model.missingFiles.isEmpty(); });
    return models;
}

bool CanvasServiceController::canPingPong() const { return m_client->canPingPong(); }

QString CanvasServiceController::animateIssue() const
{
    if (!m_client->isAuthenticated()) return tr("Connect to the service to animate images.");
    if (!m_client->canAnimate()) return tr("The service cannot animate images — update Nukleus.");
    if (!m_client->canAnimateMp4())
        return tr("The service cannot make MP4 videos — install ffmpeg where Nukleus runs.");
    if (animationModels().isEmpty())
        return tr("No animation model with all its files is installed — install one in Nukleus Manager.");
    return {};
}

QSize CanvasServiceController::animationSize(const QSize& start, const QJsonObject& modelOptions)
{
    if (start.isEmpty()) return {};
    const double area = modelOptions.value("width").toDouble(480) * modelOptions.value("height").toDouble(832);
    const double ratio = double(start.width()) / start.height();
    const auto side = [](double length) { return qMax(16, int(std::lround(length / 16)) * 16); };
    return {side(std::sqrt(area * ratio)), side(std::sqrt(area / ratio))};
}

bool CanvasServiceController::queueAnimation(const QUuid& ticket, const QImage& start, const QImage& end,
    const QString& prompt, const QString& modelId, std::optional<bool> pingPong, int frames, int fps)
{
    if (ticket.isNull() || start.isNull() || prompt.trimmed().isEmpty() || !animateIssue().isEmpty()) return false;
    const auto models = animationModels();
    const auto model = std::find_if(models.cbegin(), models.cend(), [&](const auto& m) { return m.id == modelId; });
    if (model == models.cend()) return false;
    Nukleus::AnimationRequest request;
    request.modelId = modelId;
    request.prompt = prompt;
    for (const auto& [image, bytes] : {std::pair{start, &request.startImage}, std::pair{end, &request.endImage}}) {
        if (image.isNull()) continue;
        QBuffer buffer(bytes);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return false;
    }
    // Only these parameters: the length is always the user's choice, steps and the rest come from
    // the model's defaults.
    const QSize size = animationSize(start.size(), model->recommendedOptions);
    request.parameters = {{"container", "mp4"}, {"width", size.width()}, {"height", size.height()},
        {"videoFrames", frames}, {"fps", fps}};
    if (pingPong) request.parameters.insert("pingPong", *pingPong);
    if (const auto immediate = immediateTranslation({prompt})) {
        request.prompt = immediate->first();
        return submitQueuedAnimationNow(ticket, request);
    }
    // The translation holds the single-job guard; the animation then joins the service queue.
    m_passTicket = ticket;
    const bool started = translate({prompt}, [this, ticket, request](bool succeeded,
                                                 const QStringList& english, const QString& message) mutable {
        if (!succeeded) { failQueued(ticket, message); return; }
        request.prompt = english.first();
        if (!animateIssue().isEmpty() || !submitQueuedAnimationNow(ticket, request))
            failQueued(ticket, tr("Could not submit the animation."));
    });
    if (!started) m_passTicket = {};
    return started;
}

bool CanvasServiceController::submitQueuedAnimationNow(const QUuid& ticket, const Nukleus::AnimationRequest& request)
{
    const QString jobId = m_client->submitAnimation(request);
    if (jobId.isEmpty()) return false;
    m_queued.insert(jobId, {ticket, {}, {}, true, request.prompt});
    emit queuedGenerationStatusChanged(ticket, tr("Submitting…"), -1);
    return true;
}

bool CanvasServiceController::submitEditNow(const Nukleus::ImageGenerationRequest& request, const QSize& outputSize)
{
    m_generationDiagnostic.clear();
    m_generationJobId = m_client->submitEdit(request);
    if (m_generationJobId.isEmpty()) return false;
    m_upscaling = false;
    m_outputSize = outputSize;
    emit generationStatusChanged(tr("Submitting…"), -1);
    emit generationAvailabilityChanged();
    return true;
}
