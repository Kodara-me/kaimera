#pragma once
#include <QString>

// Untranslated model instructions saved with an identity: how to use its references.
// They also stand in for the prompts of identity packages saved without them.
namespace IdentityPrompts
{
constexpr auto DefaultGenerationPrompt = "Use the reference images for the face and hair of the character.";
constexpr auto DefaultEditPrompt = "Edit the face and hair of the source image to match the reference images.";

// Appends clause to text as a comma-separated phrase, unless text already contains it.
inline QString appendClause(const QString& text, const QString& clause)
{
    QString result = text.trimmed();
    while (result.endsWith(QLatin1Char(',')))
        result = result.chopped(1).trimmed();
    const QString added = clause.trimmed();
    if (added.isEmpty() || result.contains(added, Qt::CaseInsensitive))
        return result;
    return result.isEmpty() ? added : result + QStringLiteral(", ") + added;
}
}
