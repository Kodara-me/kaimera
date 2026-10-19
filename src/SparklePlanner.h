#pragma once
#include "PromptInstructions.h"
#include "ShotPicker.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSize>
#include <QString>
#include <QStringList>
#include <optional>

// Kira Sparkle's first answer: the LLM reads a free request with the workspace's context and
// chooses one action (answer, ask a follow-up question, write a prompt, create an image, edit
// an image or have the image describer look at the images), answering with a JSON plan that
// Canvas validates. The user chooses the preset and editing quality. The instructions are
// model input, not UI text, so they are never passed through tr().
namespace SparklePlanner
{
struct Preset
{
    QString id;
    QString name; // English name.
    QString style; // The preset's positive prefix, which reveals its style.
    PromptInstructions::Format format = PromptInstructions::Format::Phrases;
    bool references = true; // Its model uses reference images.
};

struct Context
{
    // The image displayed in the workspace, if any.
    QSize imageSize;
    // The final prompt the displayed image was generated from, when known.
    QString imagePrompt;
    // The attached base image (the image to change) and the number of reference images.
    bool base = false;
    int references = 0;
    // The prompt of a base image Kira Sparkle created, when it is still the base.
    QString basePrompt;
    // Kira Sparkle's last answer is a creation proposal waiting for the user's answer.
    bool proposal = false;
    // The preset the user chose, when it can generate now.
    std::optional<Preset> preset;
    // The editing quality the user chose, when it can edit now.
    QString qualityId;
    // An image describer can look at the images.
    bool canLook = false;
    // The request's language, for the texts written to the user.
    PromptInstructions::Language language = PromptInstructions::Language::Unknown;
    bool hasImage() const { return imageSize.isValid() && !imageSize.isEmpty(); }
    bool canGenerate() const { return preset.has_value(); }
    // An edit is offered whenever the quality can run, so a request to change an image is
    // recognized even when none can be changed; Canvas then asks for a base image.
    bool canEdit() const { return !qualityId.isEmpty(); }
    // An edit changes the base image, or the displayed image without one.
    bool hasEditTarget() const { return base || hasImage(); }
    // The images the describer can be shown, as the plan names them.
    QStringList images() const
    {
        QStringList names;
        if (base) names.append(QStringLiteral("base"));
        for (int i = 1; i <= references; ++i) names.append(QStringLiteral("reference %1").arg(i));
        if (hasImage()) names.append(QStringLiteral("displayed"));
        return names;
    }
};

struct Plan
{
    // Generate is a proposal the user accepts (its button, or isAcceptance) before it runs.
    enum class Action { Reply, Ask, Prompt, Generate, Edit, Look };
    Action action = Action::Reply;
    // Generate's description of the image and what it excludes, in the user's language.
    QString description;
    QString avoid;
    // Reply's answer, or Prompt's short introduction, in the user's language.
    QString message;
    // Ask's follow-up question in the user's language, or Look's question for the image describer.
    QString question;
    ShotPicker::Shot shot = ShotPicker::Shot::Free;
    QString ratio; // Target ratio such as "16:9", as the resolution choices name them.
    QString prompt;
    QString negativePrompt;
    QString qualityId;
    QString instruction;
    // The images Look shows the describer, in Context::images() names.
    QStringList images;
};

struct ShotName
{
    ShotPicker::Shot shot;
    const char* id;
    const char* meaning;
};

inline const QList<ShotName>& shots()
{
    using Shot = ShotPicker::Shot;
    static const QList<ShotName> names{
        {Shot::Free, "free", "no imposed framing; objects, food, patterns, abstract art, or when unsure"},
        {Shot::ExtremeCloseUp, "extreme-close-up", "a small detail fills the frame (an eye, a hand, a texture, a tiny object)"},
        {Shot::CloseUp, "close-up", "a whole head or face fills the frame (expression, makeup, face details)"},
        {Shot::Portrait, "portrait", "head and shoulders of a person or animal"},
        {Shot::Medium, "medium", "a person down to the waist (gestures, upper outfit, interaction)"},
        {Shot::Cowboy, "cowboy", "a person down to mid-thigh (stance, holding something, hips)"},
        {Shot::FullBody, "full-body", "a whole person from head to feet (full outfit, standing or dancing pose)"},
        {Shot::Wide, "wide", "a small subject in large surroundings (landscapes, cities, interiors, crowds)"}};
    return names;
}

// The ratios the resolution choices offer, landscape first in each pair.
inline QStringList ratios()
{
    return {QStringLiteral("1:1"), QStringLiteral("4:3"), QStringLiteral("3:4"), QStringLiteral("3:2"),
        QStringLiteral("2:3"), QStringLiteral("5:3"), QStringLiteral("3:5"), QStringLiteral("16:9"),
        QStringLiteral("9:16"), QStringLiteral("21:9"), QStringLiteral("9:21"), QStringLiteral("2:1"),
        QStringLiteral("1:2"), QStringLiteral("4:1"), QStringLiteral("1:4")};
}

inline QString formatName(PromptInstructions::Format format)
{
    using Format = PromptInstructions::Format;
    switch (format) {
    case Format::Natural: return QStringLiteral("natural sentences");
    case Format::Tags: return QStringLiteral("Danbooru tags");
    case Format::Phrases: break;
    }
    return QStringLiteral("phrases");
}

inline QString languageName(PromptInstructions::Language language)
{
    switch (language) {
    case PromptInstructions::Language::French: return QStringLiteral("French");
    case PromptInstructions::Language::English: return QStringLiteral("English");
    case PromptInstructions::Language::Unknown: break;
    }
    return QStringLiteral("the language of the request");
}

// The request's language; short requests such as "merci !" carry no clue, so the application's
// language is the best guess for them.
inline PromptInstructions::Language requestLanguage(const QString& request, const QLocale& locale = QLocale())
{
    using Language = PromptInstructions::Language;
    const Language language = PromptInstructions::detectLanguage(request);
    if (language != Language::Unknown) return language;
    if (locale.language() == QLocale::French) return Language::French;
    if (locale.language() == QLocale::English) return Language::English;
    return Language::Unknown;
}

// The actions the context allows, in the order the instructions present them.
inline QStringList actions(const Context& context)
{
    QStringList names{QStringLiteral("reply"), QStringLiteral("ask"), QStringLiteral("prompt")};
    if (context.canGenerate()) names.append(QStringLiteral("generate"));
    // A change to a waiting proposal ("yes but at night") reads like an edit, yet its image does
    // not exist: while one waits, editing is not offered.
    if (context.canEdit() && !(context.proposal && context.canGenerate())) names.append(QStringLiteral("edit"));
    if (context.canLook && !context.images().isEmpty()) names.append(QStringLiteral("look"));
    return names;
}

// The JSON schema the reply must follow: one object per allowed action, with only its fields.
inline QJsonObject schema(const Context& context)
{
    const auto choice = [](const QStringList& values) {
        return QJsonObject{{"type", "string"}, {"enum", QJsonArray::fromStringList(values)}};
    };
    // Bounded texts: the grammar then closes a rambling field instead of running to maxTokens.
    const auto text = [](const QString& key) {
        const int length = key == QLatin1String("prompt") ? 1200
            : key == QLatin1String("message") ? 800
            : key == QLatin1String("description") ? 600
                                                  : 400;
        return QJsonObject{{"type", "string"}, {"maxLength", length}};
    };
    QStringList shotIds;
    for (const auto& shot : shots()) shotIds.append(QString::fromLatin1(shot.id));
    QJsonArray branches;
    for (const auto& action : actions(context)) {
        // The action comes first, so the decision is made before any text is written.
        QJsonObject properties{{"action", QJsonObject{{"const", action}}}};
        QStringList keys;
        if (action == QLatin1String("reply")) keys = {QStringLiteral("message")};
        else if (action == QLatin1String("ask")) keys = {QStringLiteral("question")};
        else if (action == QLatin1String("prompt"))
            keys = {QStringLiteral("message"), QStringLiteral("prompt"), QStringLiteral("negativePrompt")};
        else if (action == QLatin1String("generate"))
            // The texts in the user's language, then their English versions.
            keys = {QStringLiteral("shot"), QStringLiteral("ratio"), QStringLiteral("description"),
                QStringLiteral("avoid"), QStringLiteral("prompt"), QStringLiteral("negativePrompt")};
        else if (action == QLatin1String("edit")) keys = {QStringLiteral("instruction")};
        // Named apart from ask's question, which is for the user.
        else keys = {QStringLiteral("images"), QStringLiteral("describerQuestion")};
        for (const auto& key : keys) {
            if (key == QLatin1String("shot")) properties.insert(key, choice(shotIds));
            else if (key == QLatin1String("ratio")) properties.insert(key, choice(ratios()));
            else if (key == QLatin1String("images"))
                properties.insert(key, QJsonObject{{"type", "array"}, {"items", choice(context.images())},
                    {"minItems", 1}, {"maxItems", int(context.images().size())}});
            else properties.insert(key, text(key));
        }
        branches.append(QJsonObject{{"type", "object"}, {"properties", properties},
            {"required", QJsonArray::fromStringList(QStringList{QStringLiteral("action")} + keys)},
            {"additionalProperties", false}});
    }
    return {{"anyOf", branches}};
}

// The system instructions describing the workspace and how to decide.
inline QString instructions(const Context& context)
{
    const QStringList allowed = actions(context);
    const QString language = languageName(context.language);
    QStringList lines{QStringLiteral(
        "You are Kira Sparkle, the assistant of an image generation and editing application. The user writes "
        "a request in any language. Choose the one action that best fulfils it and answer only with the JSON "
        "object the schema describes.")};
    lines.append(QString());
    lines.append(QStringLiteral("Workspace:"));
    if (context.hasImage()) {
        lines.append(QStringLiteral("- An image of %1 × %2 pixels is displayed (\"displayed\").")
                .arg(context.imageSize.width()).arg(context.imageSize.height()));
        if (!context.imagePrompt.trimmed().isEmpty())
            lines.append(QStringLiteral("- It was generated from this prompt: %1").arg(context.imagePrompt.simplified()));
    } else {
        lines.append(QStringLiteral("- No image is displayed."));
    }
    if (context.base)
        lines.append(QStringLiteral("- The user attached a base image (\"base\"): the image to change.%1")
                .arg(context.basePrompt.trimmed().isEmpty()
                        ? QString()
                        : QStringLiteral(" You created it from your accepted proposal, with this prompt: %1")
                              .arg(context.basePrompt.simplified())));
    if (context.references > 0)
        lines.append(QStringLiteral("- The user attached %1 reference image(s) (\"reference 1\" and so on): "
                                    "inspiration such as a person, an outfit, a style or an object.")
                .arg(context.references));
    if (!context.base && context.references == 0)
        lines.append(QStringLiteral("- No image is attached."));
    lines.append(QStringLiteral("- You cannot see any image. %1")
            .arg(allowed.contains(QStringLiteral("look"))
                    ? QStringLiteral("An image describer can look at them for you (action \"look\").")
                    : QStringLiteral("Nothing can look at them for you now.")));
    lines.append(QString());
    lines.append(QStringLiteral("Actions, from the most to the least specific:"));
    if (allowed.contains(QStringLiteral("look")))
        lines.append(QStringLiteral(
            "- \"look\": the answer depends on what an image shows, which you cannot see: a question about an "
            "image's content (\"is she wearing glasses?\", \"what is in the background?\"), a comparison, an "
            "opinion, a critique, ideas or suggestions for improving or changing an image (\"what would you "
            "improve?\", \"give me ideas to change it\"), or a description of an image. images: the images to "
            "show; describerQuestion: the question the image describer answers for the user, self-contained, in "
            "%1. Do not look for what the workspace already tells you, such as the displayed image's prompt, nor "
            "before or instead of creating, editing or asking: the image models see the attached images "
            "themselves.").arg(language));
    if (allowed.contains(QStringLiteral("edit")))
        lines.append(QStringLiteral(
            "- \"edit\": the request changes, adds to, removes from, fixes or restyles an existing image (\"make "
            "the sky violet\", \"remove the hat\", \"put the outfit of reference 1 on her\"). instruction: one "
            "short, precise English edit instruction saying only what changes; name \"reference 1\" and so on "
            "when references should be used. %1")
                .arg(context.base ? QStringLiteral("The edit changes the base image.")
                    : context.hasImage() ? QStringLiteral("The edit changes the displayed image.")
                    : QStringLiteral("No image can be changed yet: still choose \"edit\" for such a request, "
                                     "even one naming a reference image, and the application asks the user to "
                                     "attach the image to change as the base image.")));
    if (allowed.contains(QStringLiteral("generate"))) {
        const auto& preset = *context.preset;
        lines.append(QStringLiteral(
            "- \"generate\": the request describes a new picture to create, or asks for an image idea, even a "
            "variation of an existing one; references go with it as inspiration. The answer is a proposal: the "
            "user reads it and accepts it before the image is created. It uses the preset the user chose, "
            "\"%1\"%2. shot: the framing that best shows what the request is about. ratio: portrait ratios (2:3, "
            "3:4, 9:16) for a single person or a tall subject, landscape ratios (3:2, 4:3, 16:9) for scenes, "
            "landscapes and groups, 1:1 for objects, icons and symmetric subjects, 21:9 or 2:1 for panoramas; "
            "follow any ratio or orientation the request names. description: what the image will show, for the "
            "user, in two or three natural sentences in %4. avoid: in %4, each thing the user's words explicitly "
            "exclude (\"without X\", \"no X\", \"sans X\", \"pas de X\"), named without \"no\" or \"without\" and "
            "separated by commas; most requests exclude nothing, so it is usually \"\". Never invent an "
            "exclusion: what the image shows is never excluded. prompt: the complete prompt written as %3, "
            "always in English whatever the language of the request or of an earlier prompt, saying the same as "
            "the description, never mentioning what is excluded. negativePrompt: exactly the avoid terms "
            "translated into English, or \"\" when avoid is \"\"; never add quality, anatomy or deformity "
            "terms: the preset adds its own.")
                .arg(preset.name,
                    preset.style.trimmed().isEmpty() ? QString()
                                                     : QStringLiteral(" (style: %1)").arg(preset.style.simplified()),
                    formatName(preset.format), language));
        if (context.references > 0 && !preset.references)
            lines.append(QStringLiteral(
                "  This preset cannot use reference images: a new image ignores them, so its prompt describes "
                "everything itself and never names a reference."));
        if (context.proposal)
            lines.append(QStringLiteral(
                "  Your last answer is a proposal the user has not accepted: a reply changing it (\"yes but at "
                "night\", \"no, a dog instead\") is a new generate proposal with the change, keeping the rest."));
        lines.append(QStringLiteral("  Camera shots:"));
        for (const auto& shot : shots())
            lines.append(QStringLiteral("  - %1: %2").arg(QString::fromLatin1(shot.id), QString::fromLatin1(shot.meaning)));
    }
    lines.append(QStringLiteral(
        "- \"prompt\": the user wants a prompt written or improved as text, not an image (\"write me a prompt "
        "for…\", \"give me a prompt idea about…\", \"improve this prompt: …\"); never answer such a request with "
        "reply, which cannot hold a prompt; never answer the acceptance of a proposal with it. prompt: the "
        "complete prompt written as %1; negativePrompt: only what the user's words explicitly exclude, as short "
        "terms separated by commas, else \"\"; message: one short sentence in %2 introducing the prompt, without "
        "repeating it.")
            .arg(context.preset && context.preset->format == PromptInstructions::Format::Tags
                    ? QStringLiteral("English Danbooru tags")
                    : QStringLiteral("%1 in %2").arg(formatName(context.preset ? context.preset->format
                                                                               : PromptInstructions::Format::Phrases),
                        language),
                language));
    lines.append(QStringLiteral(
        "- \"ask\": the request is too vague to act on (no subject, \"change it\" without saying how), mixes "
        "incompatible wishes, or needs an image that is missing. question: one short follow-up question in %1 "
        "offering two or three concrete options. Never ask when the user wants ideas, suggestions or advice, "
        "even ideas to change an image: give them (reply, or prompt for a prompt idea), or look at the image "
        "they are about.").arg(language));
    lines.append(QStringLiteral(
        "- \"reply\": anything else: questions about the application or the workspace, the displayed image's "
        "prompt (quote it exactly as written above), ideas and creative advice that need no image (give them "
        "directly; a prompt idea is a prompt answer, never a reply), greetings and thanks.%2 message: a short, friendly answer in %1. Bring unrelated topics "
        "politely back to images. Never claim to see an image or invent a feature, a button or a setting.")
            .arg(language, allowed.contains(QStringLiteral("look"))
                    ? QString()
                    : QStringLiteral(" A question about what an image shows: say that you cannot see images now.")));
    lines.append(QString());
    lines.append(QStringLiteral(
        "The application: the engine button, left of Send, shows the creation preset's initials and the "
        "editing quality's bars; it chooses the preset that creates new images and the editing quality (Fast, "
        "Balanced or Quality: quicker or finer edits). The + button attaches a base image (the image to "
        "change) and up to four reference images, from the computer, the displayed image or a saved identity. "
        "The New image card offers the detailed generation settings, and the toolbar left of the preview "
        "crops, upscales, downscales, sharpens and saves the displayed image."));
    lines.append(QStringLiteral(
        "The conversation: your earlier answers are the JSON objects you wrote. A generate answer is only a "
        "proposal until the user accepts it; the image it creates then becomes the base image. Edit and look "
        "answers were not carried out. When the request refers to one of them (\"generate it\", \"do it\", \"the first one\", "
        "\"same but darker\", \"ça\"), continue from it: reuse its prompt, instruction or idea with only the "
        "changes asked for, translated into English where an English text is needed, instead of inventing a "
        "new one. Changing an earlier prompt or generate answer (\"same in black and white\", \"add a driver\") "
        "repeats that action with the changed prompt; it is never an edit, which only changes an image that "
        "exists in the workspace. A request that does not refer to them stands on its own."));
    lines.append(QStringLiteral(
        "English prompts are concise, about 40 to 80 words, and say each detail once. They and the instructions "
        "never contain the preset's name, a ratio, framing, shot, camera, lens, resolution or image quality terms "
        "(the application adds them) nor what to avoid (negativePrompt holds it); they keep everything the "
        "request asks for and add no new subject or person."));
    return lines.join(QLatin1Char('\n'));
}

// Whether the request only accepts a proposal ("oui", "ok go", "vas-y !"), which needs no LLM.
inline bool isAcceptance(const QString& request)
{
    // Accepting words, then words that only accompany them.
    static const QStringList accepting{QStringLiteral("oui"), QStringLiteral("ouais"), QStringLiteral("ok"),
        QStringLiteral("okay"), QStringLiteral("yes"), QStringLiteral("yep"), QStringLiteral("yeah"),
        QStringLiteral("go"), QStringLiteral("vas-y"), QStringLiteral("allez-y"), QStringLiteral("allons-y"),
        QStringLiteral("allez"), QStringLiteral("d'accord"), QStringLiteral("parfait"), QStringLiteral("perfect"),
        QStringLiteral("super"), QStringLiteral("génial"), QStringLiteral("top"), QStringLiteral("nickel"),
        QStringLiteral("bien"), QStringLiteral("great"), QStringLiteral("good"), QStringLiteral("fine"),
        QStringLiteral("sure"), QStringLiteral("cool"), QStringLiteral("bon"), QStringLiteral("va"),
        QStringLiteral("lance"), QStringLiteral("lance-la"), QStringLiteral("lance-le"), QStringLiteral("fais-le"),
        QStringLiteral("fais-la"), QStringLiteral("crée"), QStringLiteral("crée-la"), QStringLiteral("crée-le"),
        QStringLiteral("génère"), QStringLiteral("génère-la"), QStringLiteral("génère-le"), QStringLiteral("create"),
        QStringLiteral("generate"), QStringLiteral("make"), QStringLiteral("do"), QStringLiteral("ahead")};
    // Politeness alone is no acceptance.
    static const QStringList accompanying{QStringLiteral("merci"), QStringLiteral("thanks"),
        QStringLiteral("thank"), QStringLiteral("you"), QStringLiteral("stp"), QStringLiteral("svp"),
        QStringLiteral("please"), QStringLiteral("c'est"), QStringLiteral("ça"), QStringLiteral("me"),
        QStringLiteral("on"), QStringLiteral("y"), QStringLiteral("it"), QStringLiteral("let's"),
        QStringLiteral("lets"), QStringLiteral("très"), QStringLiteral("very"), QStringLiteral("looks"),
        QStringLiteral("l'image"), QStringLiteral("the"), QStringLiteral("image"), QStringLiteral("la"),
        QStringLiteral("le")};
    // Words only, so any other word (a change such as "mais de nuit") leaves it to the LLM.
    QString text = request.toLower();
    text.replace(QChar(0x2019), QLatin1Char('\''));
    static const QRegularExpression separators(QStringLiteral("[^\\w'-]+"),
        QRegularExpression::UseUnicodePropertiesOption); // Accented letters are word characters.
    const QStringList parts = text.split(separators, Qt::SkipEmptyParts);
    if (parts.isEmpty() || parts.size() > 6) return false;
    bool accepted = false;
    for (const auto& part : parts) {
        if (accepting.contains(part)) accepted = true;
        else if (!accompanying.contains(part)) return false;
    }
    return accepted;
}

// The comma-separated terms once each (ignoring case), as LLMs sometimes repeat a list.
inline QString uniqueTerms(const QString& text)
{
    QStringList terms;
    QStringList seen;
    for (const auto& part : text.split(QLatin1Char(','))) {
        const QString term = part.simplified();
        if (term.isEmpty() || seen.contains(term.toCaseFolded())) continue;
        seen.append(term.toCaseFolded());
        terms.append(term);
    }
    return terms.join(QStringLiteral(", "));
}

// The validated plan, or nothing with error explaining what the reply lacks.
inline std::optional<Plan> parse(const QJsonObject& reply, const Context& context, QString* error = nullptr)
{
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return std::optional<Plan>();
    };
    const auto field = [&reply](const char* key) { return reply.value(QLatin1String(key)).toString().trimmed(); };
    const QString action = field("action");
    if (!actions(context).contains(action))
        return fail(QCoreApplication::translate("SparklePlanner", "Kira Sparkle chose an action that cannot run now."));
    Plan plan;
    if (action == QLatin1String("reply") || action == QLatin1String("ask")) {
        plan.action = action == QLatin1String("reply") ? Plan::Action::Reply : Plan::Action::Ask;
        (plan.action == Plan::Action::Reply ? plan.message : plan.question) =
            field(plan.action == Plan::Action::Reply ? "message" : "question");
        if ((plan.action == Plan::Action::Reply ? plan.message : plan.question).isEmpty())
            return fail(QCoreApplication::translate("SparklePlanner", "Kira Sparkle wrote an empty answer."));
        return plan;
    }
    if (action == QLatin1String("edit")) {
        plan.action = Plan::Action::Edit;
        plan.instruction = field("instruction");
        if (plan.instruction.isEmpty())
            return fail(QCoreApplication::translate("SparklePlanner", "Kira Sparkle wrote no edit instruction."));
        plan.qualityId = context.qualityId;
        return plan;
    }
    if (action == QLatin1String("look")) {
        plan.action = Plan::Action::Look;
        plan.question = field("describerQuestion");
        for (const auto& image : reply.value("images").toArray())
            if (context.images().contains(image.toString()) && !plan.images.contains(image.toString()))
                plan.images.append(image.toString());
        if (plan.question.isEmpty() || plan.images.isEmpty())
            return fail(QCoreApplication::translate("SparklePlanner", "Kira Sparkle did not say what to look at."));
        return plan;
    }
    plan.action = action == QLatin1String("prompt") ? Plan::Action::Prompt : Plan::Action::Generate;
    plan.message = field("message");
    plan.prompt = field("prompt");
    if (plan.prompt.isEmpty())
        return fail(QCoreApplication::translate("SparklePlanner", "Kira Sparkle wrote no image prompt."));
    // Without a description, the user still sees what would be created.
    plan.description = field("description");
    if (plan.description.isEmpty()) plan.description = plan.prompt;
    plan.negativePrompt = uniqueTerms(field("negativePrompt"));
    if (plan.action == Plan::Action::Generate) {
        // LLMs invent exclusions, sometimes the very subject: a negative prompt needs an exclusion the
        // user sees, and never names what the prompt shows ("de nuit" excluding the night).
        plan.avoid = uniqueTerms(field("avoid"));
        QStringList kept;
        for (const auto& term : plan.negativePrompt.split(QStringLiteral(", "), Qt::SkipEmptyParts))
            if (!plan.prompt.contains(term, Qt::CaseInsensitive)) kept.append(term);
        plan.negativePrompt = plan.avoid.isEmpty() ? QString() : kept.join(QStringLiteral(", "));
        if (plan.negativePrompt.isEmpty()) plan.avoid.clear();
    }
    for (const auto& shot : shots())
        if (QString::fromLatin1(shot.id) == field("shot")) plan.shot = shot.shot;
    const QString ratio = field("ratio");
    if (ratios().contains(ratio)) plan.ratio = ratio;
    return plan;
}
}
