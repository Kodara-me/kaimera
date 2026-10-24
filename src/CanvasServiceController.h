#pragma once

#include "DanbooruDictionary.h"
#include "PromptInstructions.h"
#include "ShotPrompts.h"
#include <nukleus/ServiceClient.h>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QImage>
#include <QUuid>
#include <functional>
#include <optional>

class CanvasServiceController final : public QObject
{
    Q_OBJECT
public:
    explicit CanvasServiceController(QObject *parent = nullptr);
    QUrl endpoint() const;
    QString token() const;
    void setConfiguration(const QUrl &endpoint, const QString &token, bool persist = true);
    void connectToService();
    QList<Nukleus::ModelInfo> generationModels() const;
    QList<Nukleus::ModelInfo> models(Nukleus::ModelCategory category = Nukleus::ModelCategory::Unknown) const;
    bool isAuthenticated() const;
    bool modelListAvailable() const;
    QString serviceMessage() const;
    void requestModels();
    std::optional<Nukleus::ModelRouting> modelRouting() const;
    bool supportsModelRouting() const;
    bool jobActive() const;
    bool allowsExplicitModels() const;
    void setAllowsExplicitModels(bool allowed);
    QString defaultEditionModel() const;
    QString defaultUpscalerModel() const;
    QString defaultLlmModel() const;
    void setDefaultLlmModel(const QString& id);
    // Service-wide LLM roles chosen in Service Manager; empty when unassigned.
    QString defaultPromptEnhancerModelId() const;
    QString defaultPromptTranslatorModelId() const;
    bool canGenerate(const QString& modelId) const;
    // Prompts are translated into English by the default prompt translator, one LLM job per
    // nonblank text, sharing the single-job guard. Without an available translator they are
    // sent unchanged. Translations are cached for the session per translator and text.
    using TextsDone = std::function<void(bool succeeded, const QStringList& texts, const QString& message)>;
    // The texts' English versions when no LLM job is needed (blank, cached or no translator).
    // For a tags model, translation converts the texts into English Danbooru tags instead.
    std::optional<QStringList> immediateTranslation(const QStringList& texts,
        PromptInstructions::Format format = PromptInstructions::Format::Phrases) const;
    // Starts translating; done runs once every text is translated or one fails.
    bool translate(const QStringList& texts, TextsDone done,
        PromptInstructions::Format format = PromptInstructions::Format::Phrases);
    // How the image model reads prompts (its promptFormat option); phrases when unknown.
    PromptInstructions::Format promptFormat(const QString& modelId) const;
    // Sparkles: the default prompt enhancer rewrites each nonblank text, more detailed and
    // structured for its kind, in its own language and without inventing anything; blank texts
    // stay blank. An answer in another language fails. Shares the single-job guard; never cached.
    // Image prompts get composition as context for where details matter.
    bool enhance(const QStringList& texts, const QList<PromptInstructions::Kind>& kinds, TextsDone done,
        const PromptInstructions::Composition& composition = {});
    // Why prompts cannot be improved now, or empty when they can.
    QString enhancementIssue() const;
    // Kira Sparkle: the default prompt enhancer answers a request with a JSON plan following
    // schema, as the single active job, after the conversation's earlier user and assistant
    // turns. done receives the parsed reply or a failure message.
    using PlanDone = std::function<void(bool succeeded, const QJsonObject& plan, const QString& message)>;
    bool plan(const QString& instructions, const QList<Nukleus::TextChatMessage>& history, const QString& request,
        const QJsonObject& schema, PlanDone done);
    // Why Kira Sparkle cannot plan now, or empty when it can.
    QString planIssue() const;
    // Whether the service can take an edit now; each quality's model is checked on submission.
    bool canEdit() const;
    // Whether the LLM marked as the default image describer can be shown images.
    bool canDescribeImages() const;
    // References are sent after the source image, in order.
    // Edits with the editing quality's model and Steps/CFG. Like workspace generations, edits
    // queue on the service under ticket and report through the queuedGeneration signals; only
    // the instruction's translation holds the single-job guard. It is translated first unless
    // translatePrompt is false (identity instructions are already English). The result keeps
    // the source's size.
    bool queueEdit(const QUuid& ticket, const QImage& image, const QString& prompt, const QList<QImage>& references,
        const QString& qualityId, bool translatePrompt = true);
    // Sharpening edits the image with the default editing quality's model and Steps/CFG, but
    // as a direct edit (no quality prefixes) with fixed complete positive and negative prompts.
    bool canSharpen() const;
    bool submitSharpen(const QImage& image);
    bool canUpscale() const;
    bool submitUpscale(const QImage& image);
    // Sends an already English request (identity portraits) as the single active job.
    bool submitGeneration(const Nukleus::ImageGenerationRequest &request);
    // Workspace generations queue on the service: several may be pending at once, each reported
    // under ticket through the queuedGeneration signals without holding the single-job guard,
    // except while their prompts are translated. References condition the generation, sent in
    // order. Both prompts are translated first; the shot's framing clauses are then placed before
    // them and identityPrompt (English) is appended, so the LLM never rewords either. Without
    // translatePrompts (Kira Sparkle's already English prompts), only tag spelling is normalized.
    bool queueGeneration(const QUuid& ticket, const Nukleus::ImageGenerationRequest &request,
        const QList<QImage>& references = {}, const QString& identityPrompt = {}, const ShotPrompts::Prefix& shot = {},
        bool translatePrompts = true);
    int queuedGenerationCount() const { return int(m_queued.size()); }
    // Animation: image.animate with MP4 output and an installed animation model with all its
    // files. Why the selected image cannot be animated, or empty when it can.
    QString animateIssue() const;
    // The service can make ping-pong loops (its pingPong parameter).
    bool canPingPong() const;
    // Animation models with no missing file, in service order.
    QList<Nukleus::ModelInfo> animationModels() const;
    // The video size for a start image: its aspect ratio at about the pixel area of the model's
    // default width × height (480 × 832 without them), each side a multiple of 16.
    static QSize animationSize(const QSize& start, const QJsonObject& modelOptions);
    // Animations queue on the service like edits, under ticket, reporting their progress through
    // queuedGenerationStatusChanged and their video through queuedAnimationFinished; a failure
    // before submission arrives through queuedGenerationFinished. The prompt is translated first
    // (holding the single-job guard), then sent with the start and end images (end may be null),
    // the model, the MP4 container, the computed size, the length (videoFrames) and frame rate
    // (fps) and, when given, pingPong; nothing else, so that the model's defaults apply.
    bool queueAnimation(const QUuid& ticket, const QImage& start, const QImage& end, const QString& prompt,
        const QString& modelId, std::optional<bool> pingPong, int frames, int fps);

signals:
    void generationModelsChanged();
    void modelsChanged();
    void modelRoutingChanged();
    void defaultModelsChanged();
    void serviceStateChanged(const QString &state, const QString &message);
    void generationAvailabilityChanged();
    void generationStatusChanged(const QString &message, int percentage);
    void generationFinished(const Nukleus::ImageJobResult &result);
    void queuedGenerationStatusChanged(const QUuid& ticket, const QString& message, int percentage);
    void queuedGenerationFinished(const QUuid& ticket, const Nukleus::ImageJobResult& result);
    // A queued animation's video, or its failure; prompt is the English prompt it was sent with.
    void queuedAnimationFinished(const QUuid& ticket, const Nukleus::AnimationResult& result, const QString& prompt);

private:
    QString translatorId() const;
    QString enhancerId() const;
    static QString translationKey(const QString& translator, const QString& text, PromptInstructions::Format format);
    // Whether the current text is rewritten into Danbooru tags rather than in its own language.
    bool passWritesTags() const;
    // Runs one LLM job per nonblank text (translating or improving), then calls done.
    bool startTextPass(const QString& model, bool enhancing, const QStringList& texts, TextsDone done);
    // Rewrites the next text, or completes; false when its LLM job was refused.
    bool continueTextPass();
    void finishTextPass(bool succeeded, const QString& message);
    QString passStatus() const;
    // Reports the text pass's status to the queued generation it translates, or the active job.
    void emitPassStatus();
    // video: the job makes a video, decoded and received as such.
    static QString progressMessage(const Nukleus::GenerationProgress& progress, bool video = false);
    bool submitQueuedAnimationNow(const QUuid& ticket, const Nukleus::AnimationRequest& request);
    bool submitQueuedNow(const QUuid& ticket, Nukleus::ImageGenerationRequest request,
        const QString& identityPrompt, const ShotPrompts::Prefix& shot);
    void failQueued(const QUuid& ticket, const QString& message,
        Nukleus::ImageOperation operation = Nukleus::ImageOperation::Generate);
    bool submitQueuedEditNow(const QUuid& ticket, const Nukleus::ImageGenerationRequest& request,
        const QSize& outputSize);
    bool submitEditNow(const Nukleus::ImageGenerationRequest& request, const QSize& outputSize);
    // Scales a successful result back to size (an edit's source), unless size is empty.
    static void resizeResult(Nukleus::ImageJobResult& result, const QSize& size);
    // The quality's model, Steps/CFG, mapped sampler/scheduler and encoded images (source first).
    std::optional<Nukleus::ImageGenerationRequest> editRequest(const QImage& image,
        const Nukleus::EditingQuality& quality, const QList<QImage>& references) const;
    // Ends a job that failed before reaching the service (a translation or its submission).
    void failJob(const QString& message, Nukleus::ImageOperation operation);
    // The user's Danbooru dictionary (danbooru.csv in the application data folder), loaded on first
    // use with tags of at least 100 posts, or null without the file. It brings tag answers to their
    // Danbooru spelling and lets prompts of known tags skip the translator.
    const DanbooruDictionary* danbooru();
    DanbooruDictionary m_danbooru;
    QString m_danbooruPath;
    // Session cache: translator, tags or not, and source text → English text.
    QHash<QString, QString> m_translations;
    // The text pass in progress: its LLM, texts, the one being rewritten and what follows.
    QString m_passModel;
    bool m_enhancing = false;
    QList<PromptInstructions::Kind> m_passKinds;
    PromptInstructions::Composition m_passComposition;
    // An improvement that dropped or invented elements gets one retry naming them.
    bool m_passRetried = false;
    QStringList m_passMissing;
    QStringList m_passInvented;
    QStringList m_passTexts;
    qsizetype m_passIndex = 0;
    TextsDone m_textsDone;
    PlanDone m_planDone;
    // The queued generation whose prompts the text pass translates, if any.
    QUuid m_passTicket;
    // Service job ID → queued workspace generation and its last diagnostic output.
    // Queued edits keep their source's size for the result; animations keep their English prompt.
    struct QueuedGeneration
    {
        QUuid ticket;
        QString diagnostic;
        QSize outputSize;
        bool animation = false;
        QString prompt;
    };
    QHash<QString, QueuedGeneration> m_queued;
    void handleState(Nukleus::ServiceConnectionState state, const QString &code, const QString &detail);
    Nukleus::ServiceClient *m_client;
    QUrl m_endpoint;
    QString m_token;
    QString m_serviceMessage;
    QString m_generationJobId;
    QSize m_outputSize;
    bool m_upscaling = false;
    QString m_generationDiagnostic;
};
