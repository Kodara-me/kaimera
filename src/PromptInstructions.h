#pragma once
#include "ShotPicker.h"
#include <QHash>
#include <QRegularExpression>
#include <QSize>
#include <QString>
#include <QStringList>
#include <algorithm>

// Fixed LLM instructions for prompt translation and improvement (Sparkles). They are
// model input, not UI text, so they are never passed through tr().
namespace PromptInstructions
{
// The kind of text being improved; each kind gets its own structure.
enum class Kind { Image, Avoid, Edit, Face, Hair };
enum class Language { Unknown, English, French };

// How the generated image will be composed: its camera shot and format. An image prompt's
// improvement uses it only to decide where details matter, never to write it.
// How the image model reads prompts, from the service's promptFormat model option:
// natural sentences (Flux), short phrases (SDXL) or tags (Illustrious).
enum class Format { Natural, Phrases, Tags };

inline Format formatFromName(const QString& name)
{
    if (name == QLatin1String("natural")) return Format::Natural;
    if (name == QLatin1String("tags")) return Format::Tags;
    return Format::Phrases;
}

struct Composition
{
    ShotPicker::Shot shot = ShotPicker::Shot::Free;
    QSize size;
    QString ratio; // Target ratio, for example "16:9".
    Format format = Format::Phrases;
};

// The composition as context for an image prompt's improvement, or empty when nothing is known.
inline QString compositionContext(const Composition& composition, Language language)
{
    using Shot = ShotPicker::Shot;
    const bool french = language == Language::French;
    QStringList facts;
    switch (composition.shot) {
    case Shot::Free:
        break;
    case Shot::ExtremeCloseUp:
        facts.append(french ? QStringLiteral("seul un petit détail du sujet remplit l'image")
                            : QStringLiteral("only a small detail of the subject fills the image"));
        break;
    case Shot::CloseUp:
        facts.append(french ? QStringLiteral("seule la tête entière du sujet, avec le cou, remplit l'image")
                            : QStringLiteral("only the subject's whole head, with the neck, fills the image"));
        break;
    case Shot::Portrait:
        facts.append(french ? QStringLiteral("seules la tête et les épaules du sujet sont visibles")
                            : QStringLiteral("only the subject's head and shoulders are visible"));
        break;
    case Shot::Medium:
        facts.append(french ? QStringLiteral("le sujet est visible jusqu'à la taille")
                            : QStringLiteral("the subject is visible down to the waist"));
        break;
    case Shot::Cowboy:
        facts.append(french ? QStringLiteral("le sujet est visible jusqu'à mi-cuisse")
                            : QStringLiteral("the subject is visible down to mid-thigh"));
        break;
    case Shot::FullBody:
        facts.append(french ? QStringLiteral("le sujet est visible en entier, de la tête aux pieds")
                            : QStringLiteral("the whole subject is visible, from head to feet"));
        break;
    case Shot::Wide:
        facts.append(french ? QStringLiteral("le sujet est vu en entier, de loin, petit, et l'environnement occupe presque toute l'image")
                            : QStringLiteral("the whole subject is seen from far away, small, and the surroundings fill most of the image"));
        break;
    }
    const QSize size = composition.size;
    if (size.isValid() && !size.isEmpty()) {
        const QString ratio = composition.ratio.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(composition.ratio);
        if (size.width() == size.height())
            facts.append((french ? QStringLiteral("l'image est carrée") : QStringLiteral("the image is square")) + ratio);
        else if (size.width() < size.height())
            facts.append((french ? QStringLiteral("l'image est verticale") : QStringLiteral("the image is vertical")) + ratio);
        else
            facts.append((french ? QStringLiteral("l'image est horizontale") : QStringLiteral("the image is horizontal"))
                + ratio);
    }
    if (facts.isEmpty())
        return {};
    return french
        ? QStringLiteral(" Contexte de l'image, à ne jamais écrire dans ta réponse : %1. Sers-t'en seulement pour "
                         "choisir où mettre les détails : développe davantage ce qui sera bien visible, reste bref "
                         "sur ce qui sera petit et n'ajoute aucun détail sur ce qui sera hors champ, sans rien "
                         "supprimer du texte. N'ajoute aucune mention du cadrage ni du format.")
              .arg(facts.join(QStringLiteral(" ; ")))
        : QStringLiteral(" Image context, never to be written in your answer: %1. Use it only to decide where details "
                         "matter: elaborate more on what will be clearly visible, stay brief about what will be "
                         "small and add no detail about what will be out of frame, without removing anything from "
                         "the text. Add no mention of the framing or the format.")
              .arg(facts.join(QStringLiteral("; ")));
}

// Asks an image prompt's improvement to pin down the pose of each visible person or animal,
// part by part, so the image model does not drift. Only the parts the shot shows are listed.
inline QString poseGuidance(ShotPicker::Shot shot, Language language)
{
    using Shot = ShotPicker::Shot;
    const bool french = language == Language::French;
    QStringList parts;
    if (shot == Shot::ExtremeCloseUp) return {};
    const bool body = shot != Shot::CloseUp && shot != Shot::Portrait;
    if (body)
        parts.append(french ? QStringLiteral("la posture d'ensemble (debout, assis, couché, à genoux, etc.)")
                            : QStringLiteral("the overall posture (standing, sitting, lying, kneeling, etc.)"));
    parts.append(french ? QStringLiteral("la position de la tête et la direction du regard")
                        : QStringLiteral("the position of the head and the direction of the gaze"));
    if (shot == Shot::Portrait)
        parts.append(french ? QStringLiteral("les épaules") : QStringLiteral("the shoulders"));
    if (body)
        parts.append(french ? QStringLiteral("le torse, les bras et les mains")
                            : QStringLiteral("the torso, the arms and the hands"));
    if (shot == Shot::Cowboy)
        parts.append(french ? QStringLiteral("les hanches et les jambes") : QStringLiteral("the hips and the legs"));
    else if (body && shot != Shot::Medium)
        parts.append(french ? QStringLiteral("les jambes et les pieds") : QStringLiteral("the legs and the feet"));
    return french
        ? QStringLiteral(" Pour chaque personne ou animal, décris précisément sa position, partie par partie, en "
                         "omettant ce qui ne s'applique pas : %1. Précise ce que le texte en dit ; pour ce qu'il "
                         "ne dit pas, choisis une position naturelle, cohérente avec l'action et le lieu. Chaque "
                         "partie n'a qu'une position, jamais deux qui se contredisent.")
              .arg(parts.join(QStringLiteral(", ")))
        : QStringLiteral(" For each person or animal, describe its position precisely, part by part, skipping what "
                         "does not apply: %1. Make what the text says about them more precise; for what it does "
                         "not say, choose a natural position consistent with the action and the setting. Give each "
                         "part a single position, never two that contradict each other.")
              .arg(parts.join(QStringLiteral(", ")));
}

// Tells French from English with accents and common words. Short or ambiguous texts
// (a single tag, names) are Unknown.
inline Language detectLanguage(const QString& text)
{
    static const QStringList french{"le", "la", "les", "un", "une", "des", "du", "de", "et", "avec", "sur",
        "dans", "pour", "au", "aux", "est", "sont", "qui", "que", "en", "son", "sa", "ses", "ou", "pas",
        "cheveux", "visage", "yeux", "peau", "femme", "homme", "ajoute", "ajouter", "enlève", "remplace"};
    static const QStringList english{"the", "a", "an", "and", "with", "on", "in", "of", "for", "is", "are",
        "his", "her", "their", "or", "not", "to", "from", "hair", "face", "eyes", "skin", "woman", "man",
        "add", "remove", "replace", "make"};
    static const QRegularExpression wordPattern(QStringLiteral("[\\p{L}']+"));
    static const QRegularExpression accents(QStringLiteral("[àâçéèêëîïôûùüÿœæ]"));
    int frenchScore = 2 * int(text.toLower().count(accents));
    int englishScore = 0;
    for (auto it = wordPattern.globalMatch(text.toLower()); it.hasNext();) {
        QString word = it.next().captured();
        // Elisions such as l'homme or d'une count as French.
        if (word.size() > 2 && word.at(1) == QLatin1Char('\'')) { frenchScore += 2; word = word.mid(2); }
        if (french.contains(word)) frenchScore += 2;
        if (english.contains(word)) englishScore += 2;
    }
    if (frenchScore >= englishScore + 2) return Language::French;
    if (englishScore >= frenchScore + 2) return Language::English;
    return Language::Unknown;
}

// Removes attention weights such as "(word:1.2)" or "word:1.2" that a rewrite added when its
// source had none: small models imitate the weighting syntax common in image prompts.
inline QString withoutAddedWeights(const QString& source, const QString& rewritten)
{
    static const QRegularExpression weight(QStringLiteral(":\\s*\\d+(?:\\.\\d+)?\\s*\\)?"));
    if (source.contains(weight)) return rewritten;
    static const QRegularExpression wrapped(QStringLiteral("\\(([^():]*):\\s*\\d+(?:\\.\\d+)?\\s*\\)"));
    // Bare weights are decimals, so ratios such as 16:9 survive.
    static const QRegularExpression bare(QStringLiteral("(?<=\\S):\\d+\\.\\d+(?=[\\s,;)]|\\.?$)"));
    QString cleaned = rewritten;
    cleaned.replace(wrapped, QStringLiteral("\\1"));
    cleaned.remove(bare);
    return cleaned;
}

// Lowercase without accents, so "mèches" and "meches" compare equal.
inline QString foldWord(const QString& word)
{
    QString folded;
    for (const QChar c : word.toLower().normalized(QString::NormalizationForm_D))
        if (c.category() != QChar::Mark_NonSpacing) folded.append(c);
    return folded;
}

// Content words of source (at least four letters, not a function word) that rewritten no
// longer contains. A word counts as kept when its first five letters appear within a word of
// the answer, so plurals, agreement, conjugation and derived verbs ("noire"/"noires",
// "regarde"/"regardant", "sombre"/"assombris") still count. Generic verbs are ignored.
inline QStringList missingWords(const QString& source, const QString& rewritten)
{
    static const QStringList functionWords{
        "avec", "dans", "pour", "sous", "vers", "chez", "elle", "elles", "ils", "leur", "leurs", "cette",
        "ces", "mais", "donc", "tandis", "quelques", "quelque", "tres", "plus", "moins", "sont", "etre",
        "avoir", "fait", "faire", "comme", "aussi", "tout", "tous", "toute", "toutes", "celui", "celle",
        "dont", "entre", "pendant", "alors", "puis", "ainsi", "notre", "votre", "nous", "vous", "leurs",
        "rends", "rendre", "rendez", "fais", "faites", "mets", "mettre", "make", "render", "turn",
        "with", "from", "into", "onto", "that", "this", "these", "those", "there", "their", "they", "them",
        "while", "have", "very", "more", "some", "also", "each", "which", "where", "when", "being", "been",
        "were", "will", "would", "should", "could", "about", "then", "than", "your"};
    static const QRegularExpression wordPattern(QStringLiteral("\\p{L}+"));
    QStringList kept;
    for (auto it = wordPattern.globalMatch(rewritten); it.hasNext();)
        kept.append(foldWord(it.next().captured()));
    QStringList missing;
    for (auto it = wordPattern.globalMatch(source); it.hasNext();) {
        const QString word = it.next().captured();
        const QString folded = foldWord(word);
        if (folded.size() < 4 || functionWords.contains(folded)) continue;
        const QString stem = folded.left(5);
        if (std::none_of(kept.cbegin(), kept.cend(), [&](const QString& k) { return k.contains(stem); })
            && !missing.contains(word, Qt::CaseInsensitive))
            missing.append(word);
    }
    return missing;
}

// Concrete elements that rewritten introduces although source never mentions them: people,
// body parts, colours and clothing, the inventions a model makes when it guesses an image,
// plus framing and image quality terms, which the application owns. Without details, body
// parts, colours and clothing may be added (an image prompt may enrich what will be visible).
// An element counts as mentioned when a source word shares its first four letters.
inline QStringList inventedWords(const QString& source, const QString& rewritten, bool details = true)
{
    static const QStringList frenchPeople{
        "homme", "hommes", "femme", "femmes", "fille", "filles", "garcon", "garcons", "enfant", "enfants",
        "personne", "personnes", "bebe"};
    static const QStringList englishPeople{
        "man", "men", "woman", "women", "girl", "girls", "boy", "boys", "child", "children", "person",
        "people", "baby"};
    static const QStringList frenchFraming{
        "cadrage", "camera", "objectif", "plongee", "qualite", "resolution", "nettete"};
    static const QStringList englishFraming{
        "framing", "framed", "shot", "camera", "lens", "angle", "quality", "resolution", "masterpiece"};
    static const QStringList frenchDetails{
        "yeux", "oeil", "cheveux", "visage", "peau", "bouche", "levres",
        "nez", "mains", "bras", "jambes", "corps", "barbe", "sourire", "bleu", "bleus", "bleue",
        "bleues", "rouge", "rouges", "vert", "verts", "verte", "vertes", "jaune", "jaunes", "noir", "noirs",
        "noire", "noires", "blanc", "blancs", "blanche", "blanches", "rose", "roses", "violet", "violets",
        "violette", "violettes", "orange", "oranges", "gris", "grise", "grises", "marron", "brun", "bruns",
        "brune", "brunes", "blond", "blonds", "blonde", "blondes", "dore", "doree", "dores", "dorees",
        "roux", "rousse", "rousses", "robe", "chemise", "veste", "pantalon", "jupe", "chapeau", "lunettes",
        "bijoux", "collier"};
    static const QStringList englishDetails{
        "eye", "eyes", "hair", "face", "skin", "mouth", "lips", "nose", "hand", "hands",
        "arm", "arms", "legs", "body", "beard", "smile", "blue", "red", "green", "yellow", "black", "white",
        "pink", "purple", "orange", "grey", "gray", "brown", "blonde", "blond", "golden", "silver", "dress",
        "shirt", "jacket", "pants", "skirt", "hat", "glasses", "jewelry", "necklace"};
    // The answer keeps the source's language, so only its words count ("de face" is not a face).
    const Language language = detectLanguage(source);
    QStringList concrete;
    if (language != Language::English)
        concrete += frenchPeople + frenchFraming + (details ? frenchDetails : QStringList());
    if (language != Language::French)
        concrete += englishPeople + englishFraming + (details ? englishDetails : QStringList());
    static const QRegularExpression wordPattern(QStringLiteral("\\p{L}+"));
    QStringList mentioned;
    for (auto it = wordPattern.globalMatch(source); it.hasNext();)
        mentioned.append(foldWord(it.next().captured()));
    QStringList invented;
    for (auto it = wordPattern.globalMatch(rewritten); it.hasNext();) {
        const QString word = it.next().captured();
        const QString folded = foldWord(word);
        if (!concrete.contains(folded)) continue;
        const QString stem = folded.left(4);
        if (std::none_of(mentioned.cbegin(), mentioned.cend(), [&](const QString& m) { return m.startsWith(stem); })
            && !invented.contains(word, Qt::CaseInsensitive))
            invented.append(word);
    }
    return invented;
}

// Framing and image quality tags that a Danbooru tag answer adds although source never mentions
// them: the application owns framing and the presets own quality tags. The answer is English
// whatever the source's language, so only these words are checked, by their first four letters.
inline QStringList inventedTagWords(const QString& source, const QString& rewritten)
{
    static const QStringList framing{"framing", "framed", "shot", "camera", "lens", "angle", "quality",
        "resolution", "masterpiece", "absurdres", "highres", "aesthetic", "portrait", "closeup"};
    static const QRegularExpression wordPattern(QStringLiteral("\\p{L}+"));
    QStringList mentioned;
    for (auto it = wordPattern.globalMatch(source); it.hasNext();)
        mentioned.append(foldWord(it.next().captured()));
    QStringList invented;
    for (auto it = wordPattern.globalMatch(rewritten); it.hasNext();) {
        const QString word = it.next().captured();
        const QString folded = foldWord(word);
        if (!framing.contains(folded)) continue;
        const QString stem = folded.left(4);
        if (std::none_of(mentioned.cbegin(), mentioned.cend(), [&](const QString& m) { return m.startsWith(stem); })
            && !invented.contains(word, Qt::CaseInsensitive))
            invented.append(word);
    }
    return invented;
}

// Whether an answer is caught in a loop, repeating a phrase until its token limit (seen with
// abliterated models in French): some three-word sequence comes back eight times or more. Real
// prompts repeat one at most about five times.
inline bool repeatsItself(const QString& text)
{
    static const QRegularExpression wordPattern(QStringLiteral("\\p{L}+|\\d+"));
    QStringList words;
    for (auto it = wordPattern.globalMatch(text.toLower()); it.hasNext();)
        words.append(it.next().captured());
    QHash<QString, int> counts;
    for (qsizetype i = 0; i + 2 < words.size(); ++i)
        if (++counts[words.mid(i, 3).join(QLatin1Char(' '))] >= 8) return true;
    return false;
}

// How every Danbooru tag answer is written: Illustrious-style models know tags only in their exact
// Danbooru spelling.
inline QString danbooruTagForm()
{
    return QStringLiteral(
        "Answer only with English Danbooru tags: lowercase, comma-separated, with spaces instead of "
        "underscores, each in its exact Danbooru spelling. No sentences, weights, parentheses, title, "
        "explanation, note or quotation marks.");
}

// Converts a prompt in any language into Danbooru tags at submission, for models whose prompt
// format is tags. Faithful like translation(): it adds and removes nothing.
inline QString danbooruTranslation()
{
    return QStringLiteral(
        "Convert the text that follows these instructions, which may be in any language, into a prompt "
        "for an illustration model trained on Danbooru tags. ") + danbooruTagForm() + QStringLiteral(
        " Be faithful: give each element of the text its tags, in the text's order; add nothing the text "
        "does not say and remove nothing. Add a subject count tag only when the text clearly states who is "
        "in the image. Leave text that already consists of English Danbooru tags unchanged. Keep names, "
        "numbers and existing weights unchanged.");
}

// Common Danbooru pose tags for the body parts the shot shows, most posted first (checked against
// the January 2025 tagcomplete danbooru.csv: each exists with at least 1,000 posts), so tag answers
// pin down the pose with tags the model learned well. Empty for an extreme close-up.
inline QString danbooruPoseVocabulary(ShotPicker::Shot shot)
{
    using Shot = ShotPicker::Shot;
    if (shot == Shot::ExtremeCloseUp) return {};
    const bool body = shot != Shot::CloseUp && shot != Shot::Portrait;
    const bool feet = body && shot != Shot::Medium && shot != Shot::Cowboy;
    QStringList groups;
    if (body)
        groups.append(QStringLiteral(
            "posture: sitting, standing, lying, on back, kneeling, leaning forward, on side, squatting, wariza, "
            "standing on one leg, crossed legs, on stomach, bent over, all fours, knees up, walking, seiza, "
            "running, contrapposto, jumping, arched back, leaning back, indian style, stretching, "
            "hugging own legs, on one knee, leaning to the side, reclining"));
    groups.append(QStringLiteral(
        "head and gaze: looking at viewer, closed eyes, looking back, looking to the side, head tilt, "
        "looking down, looking up, facing viewer, head rest, facing away, looking afar, turning head, "
        "head back, head down"));
    if (body)
        groups.append(QStringLiteral(
            "arms and hands: hand up, arm up, arms up, hands up, hand on own hip, arm support, arms behind back, "
            "crossed arms, own hands together, outstretched arms, outstretched arm, pointing, hand on own chest, "
            "arms behind head, hand on own face, hand in pocket, hand in own hair, hands on own hips, reaching, "
            "arms at sides, waving, hand on own cheek, hands in pockets, hand on own chin, own hands clasped, "
            "thumbs up, hands on own knees"));
    if (body && shot != Shot::Medium)
        groups.append(QStringLiteral("legs") + (feet ? QStringLiteral(" and feet") : QString())
            + QStringLiteral(": spread legs, leg up, legs up, legs apart, knees together feet apart, legs together")
            + (feet ? QStringLiteral(", tiptoes, feet up, crossed ankles") : QString()));
    return QStringLiteral(" Danbooru pose tags, by body part, most common first: ") + groups.join(QStringLiteral("; "))
        + QStringLiteral(". Prefer them for the pose, choosing only those that match the text and the action and "
                         "never two that contradict each other; other existing Danbooru tags remain allowed.");
}

// Sparkles for models whose prompt format is tags: the image prompt or the things to avoid, from
// a text in any language, become English Danbooru tags. For a retry, unknown names the tags a
// previous answer wrote that the Danbooru dictionary does not know, and invented the framing or
// quality tags it made up.
inline QString danbooruImprovement(Kind kind, const QStringList& unknown, const QStringList& invented,
    const Composition& composition)
{
    QString instructions;
    if (kind == Kind::Image) {
        instructions = QStringLiteral(
            "You turn the text that follows \"Text:\", an image idea in any language, into a complete prompt "
            "for an illustration model trained on Danbooru tags. ") + danbooruTagForm() + QStringLiteral(
            " Order: subject count tags, then the subject's appearance (hair, eyes, body, clothing or "
            "nudity), posture and body position, action, setting and background, lighting and atmosphere, "
            "style. Use a subject count tag consistent with the text, and the tag for no people when "
            "nobody is in the image.")
            + poseGuidance(composition.shot, Language::English)
            + QStringLiteral(" Give each part its own precise tags.")
            + danbooruPoseVocabulary(composition.shot)
            + compositionContext(composition, Language::English)
            + QStringLiteral(
            " Rules: every element of the text must appear in your tags. You may add plausible details "
            "consistent with the text and with what will be visible (appearance, clothing, materials, "
            "posture, gestures, expression, atmosphere), without changing what the text specifies. Always "
            "respect the clothing or nudity the text describes; never dress a nude subject or change a "
            "described outfit. Add no new subject and no other person.");
    } else {
        instructions = QStringLiteral(
            "You turn the text that follows \"Text:\", a list of things to avoid in an image, in any "
            "language, into a negative prompt for an illustration model trained on Danbooru tags. ")
            + danbooruTagForm() + QStringLiteral(
            " Group the tags by theme. Every element of the text must appear in your tags; add no element "
            "the text does not mention.");
    }
    return instructions
        + QStringLiteral(
        " Add no framing, shot, camera angle, lens, format, resolution or image quality tags that the text "
        "does not contain: the application handles them.")
        + (unknown.isEmpty() ? QString()
            : QStringLiteral(" Your previous answer wrote tags that do not exist on Danbooru: replace ")
                + unknown.join(QStringLiteral("; ")) + QStringLiteral(" with existing Danbooru tags.")
                + (std::any_of(unknown.cbegin(), unknown.cend(), [](const QString& u) { return u.endsWith(')'); })
                    ? QStringLiteral(" In parentheses are the closest existing tags, most common first; use one only "
                                     "if it fits the text.")
                    : QString()))
        + (invented.isEmpty() ? QString()
            : QStringLiteral(" Your previous answer added tags the text does not call for: do not write ")
                + invented.join(QStringLiteral(", ")) + QLatin1Char('.'))
        + QStringLiteral("\n\nText:");
}

inline QString translation()
{
    return QStringLiteral(
        "Translate the text that follows these instructions into English. Output only the English "
        "translation and nothing else: no explanations, notes, labels, quotation marks or alternatives. "
        "Translate faithfully: keep the meaning, details, order and comma-separated structure; do not "
        "add, remove, improve or embellish anything. Leave text that is already in English unchanged. "
        "Keep names and numbers unchanged.");
}

// Instructions written in the text's own language, naming that language explicitly:
// small models follow the language of their instructions more than a "same language" rule.
// For a retry, missing names the source words a previous answer dropped and invented the
// elements it made up. An image prompt also gets its composition as context.
inline QString improvement(Kind kind, Language language, const QStringList& missing = {},
    const QStringList& invented = {}, const Composition& composition = {})
{
    // Tag models get Danbooru tags in English for both generation prompts, whatever the language;
    // there, missing names the unknown tags of a previous answer.
    if (composition.format == Format::Tags && (kind == Kind::Image || kind == Kind::Avoid))
        return danbooruImprovement(kind, missing, invented, composition);
    const QString context = kind == Kind::Image
        ? poseGuidance(composition.shot, language) + compositionContext(composition, language)
        : QString();
    if (language == Language::French) {
        QString structure;
        switch (kind) {
        case Kind::Image:
            structure = QStringLiteral(
                "C'est un prompt de génération d'image : fais de l'idée du texte un prompt complet. Structure : ")
                + (composition.format == Format::Natural
                    ? QStringLiteral("des phrases descriptives fluides et naturelles, comme la description d'une "
                                     "image")
                    : QStringLiteral("des groupes de mots descriptifs séparés par des virgules"))
                + QStringLiteral(
                ", dans cet ordre, en omettant ce qui ne s'applique pas : sujet principal, apparence et détails du "
                "sujet, posture et position du corps, action, lieu et arrière-plan, éclairage et ambiance, style.");
            break;
        case Kind::Avoid:
            structure = QStringLiteral(
                "C'est la liste de ce qu'il faut éviter dans l'image. Structure : une liste d'éléments courts et "
                "précis séparés par des virgules, regroupés par thème. N'ajoute aucun élément non mentionné.");
            break;
        case Kind::Edit:
            structure = QStringLiteral(
                "C'est une instruction de modification d'une image existante, que tu ne vois pas. Rends-la plus "
                "précise, pas plus longue. Structure : une ou deux phrases claires à l'impératif qui disent quoi "
                "changer, où et dans quelle mesure, uniquement avec ce que le texte dit. Ne nomme aucune personne, "
                "partie du corps, objet, couleur ou détail que le texte ne cite pas. N'ajoute rien sur ce qui doit "
                "rester inchangé, sauf si le texte le demande.");
            break;
        case Kind::Face:
            structure = QStringLiteral(
                "C'est la description du visage d'une personne pour un portrait. Structure : des groupes de mots "
                "séparés par des virgules, dans cet ordre, en omettant ce que le texte ne mentionne pas : âge et "
                "genre, peau, forme du visage, yeux, sourcils, nez, bouche et lèvres, signes distinctifs.");
            break;
        case Kind::Hair:
            structure = QStringLiteral(
                "C'est la description des cheveux d'une personne. Structure : des groupes de mots séparés par "
                "des virgules, dans cet ordre, en omettant ce que le texte ne mentionne pas : longueur, couleur, "
                "texture, coiffure et raie.");
            break;
        }
        return QStringLiteral(
            "Tu améliores un texte qui sert à créer des images. Réécris le texte qui suit « Texte : » pour le "
            "rendre plus détaillé, plus précis et mieux structuré, sans changer son sens. ") + structure + context
            + QStringLiteral(
            " Règles : ne supprime rien ; chaque nom, lieu, objet, couleur, vêtement et action du texte "
            "d'origine doit se retrouver dans ta réponse, même s'il semble redondant avec un autre. ")
            + (kind == Kind::Image
                ? QStringLiteral(
                    "Tu peux enrichir l'image de détails plausibles, cohérents avec le texte et avec ce qui sera "
                    "visible (apparence, vêtements, matières, posture, gestes, expression, ambiance), sans changer ce que le "
                    "texte précise. Respecte toujours les vêtements ou la nudité que le texte décrit ; n'habille "
                    "pas un sujet nu et ne change pas une tenue décrite. N'ajoute aucun nouveau sujet ni aucune "
                    "autre personne. ")
                : QStringLiteral(
                    "N'invente rien : aucun nouveau sujet, objet, personne, couleur, vêtement, attribut, style, lieu, "
                    "éclairage ou détail que le texte ne dit pas ; tu peux seulement expliciter ce que le texte "
                    "implique clairement et le réorganiser. "))
            + QStringLiteral(
            "N'ajoute aucun terme de cadrage, de plan, d'angle de caméra, d'objectif, de "
            "format, de résolution ni de qualité d'image que le texte ne contient pas : l'application s'en "
            "charge. Réponds uniquement en français ; ne traduis jamais en anglais. Garde les noms "
            "et les nombres tels quels. Réponds seulement avec le texte réécrit : pas de titre, d'explication, "
            "de note, de guillemets ni d'alternative.")
            + (missing.isEmpty() ? QString()
                : QStringLiteral(" Ta réponse précédente a omis des éléments : elle doit aussi contenir ")
                    + missing.join(QStringLiteral(", ")) + QLatin1Char('.'))
            + (invented.isEmpty() ? QString()
                : QStringLiteral(" Ta réponse précédente a inventé des éléments absents du texte : n'écris pas ")
                    + invented.join(QStringLiteral(", ")) + QLatin1Char('.'))
            + QStringLiteral("\n\nTexte :");
    }
    QString structure;
    switch (kind) {
    case Kind::Image:
        structure = QStringLiteral(
            "It is an image-generation prompt: turn the idea of the text into a complete prompt. Structure: ")
            + (composition.format == Format::Natural
                ? QStringLiteral("fluent, natural descriptive sentences, like the description of an image,")
                : QStringLiteral("comma-separated descriptive phrases"))
            + QStringLiteral(
            " in this order, skipping what does not apply: main subject, the subject's appearance and details, "
            "posture and body position, action, setting and background, lighting and atmosphere, style.");
        break;
    case Kind::Avoid:
        structure = QStringLiteral(
            "It is a list of things to avoid in the image. Structure: a comma-separated list of short, precise "
            "items grouped by theme. Do not add items that are not mentioned.");
        break;
    case Kind::Edit:
        structure = QStringLiteral(
            "It is an instruction for editing an existing image, which you cannot see. Make it more precise, "
            "not longer. Structure: one or two clear imperative sentences stating what to change, where and by "
            "how much, using only what the text says. Name no person, body part, object, colour or detail that "
            "the text does not mention. Add nothing about what must stay unchanged unless the text asks for it.");
        break;
    case Kind::Face:
        structure = QStringLiteral(
            "It describes a person's face for a portrait. Structure: comma-separated phrases in this order, "
            "skipping anything the text does not mention: age and gender, skin, face shape, eyes, eyebrows, "
            "nose, mouth and lips, distinctive features.");
        break;
    case Kind::Hair:
        structure = QStringLiteral(
            "It describes a person's hair. Structure: comma-separated phrases in this order, skipping anything "
            "the text does not mention: length, colour, texture, style and parting.");
        break;
    }
    return QStringLiteral(
        "You improve a text used to create images. Rewrite the text that follows \"Text:\" so it is more "
        "detailed, more precise and better structured, without changing its meaning. ") + structure + context
        + QStringLiteral(
        " Rules: remove nothing; every noun, place, object, colour, piece of clothing and action of the "
        "original text must appear in your answer, even if it seems redundant with another. ")
        + (kind == Kind::Image
            ? QStringLiteral(
                "You may enrich the image with plausible details consistent with the text and with what will be "
                "visible (appearance, clothing, materials, posture, gestures, expression, atmosphere), without changing what "
                "the text specifies. Always respect the clothing or nudity the text describes; never dress a nude "
                "subject or change a described outfit. Add no new subject and no other person. ")
            : QStringLiteral(
                "Do not invent anything: no new subject, object, person, colour, clothing, attribute, style, "
                "setting, lighting or detail that the text does not state; you may only make explicit what the "
                "text clearly implies and reorganize it. "))
        + QStringLiteral(
        "Add no framing, shot, camera angle, lens, format, resolution or image quality terms "
        "that the text does not contain: the application handles them. ")
        + (language == Language::English ? QStringLiteral("Answer only in English. ")
                                         : QStringLiteral("Answer in the same language as the text; never translate it. "))
        + QStringLiteral(
        "Keep names and numbers unchanged. Output only the rewritten text: no "
        "title, explanation, note, quotation marks or alternatives.")
        + (missing.isEmpty() ? QString()
            : QStringLiteral(" Your previous answer left out elements: it must also contain ")
                + missing.join(QStringLiteral(", ")) + QLatin1Char('.'))
        + (invented.isEmpty() ? QString()
            : QStringLiteral(" Your previous answer invented elements the text does not mention: do not write ")
                + invented.join(QStringLiteral(", ")) + QLatin1Char('.'))
        + QStringLiteral("\n\nText:");
}
}
