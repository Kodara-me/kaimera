#pragma once
#include "ShotPicker.h"
#include <QString>

// Fixed English framing clauses for each camera shot. Canvas places them after the
// service's style prefixes and before the user's (translated) prompts; the translator
// never sees them. Free adds nothing.
namespace ShotPrompts
{
struct Prefix
{
    QString positive;
    QString negative;
};

// Danbooru framing tags for tag models (Illustrious), which know framing only by its tags. Danbooru
// has no extreme close-up tag, so it uses close-up.
inline Prefix tagsForShot(ShotPicker::Shot shot)
{
    using Shot = ShotPicker::Shot;
    switch (shot) {
    case Shot::Free:
        return {};
    case Shot::ExtremeCloseUp:
        return { QStringLiteral("close-up"), QStringLiteral("upper body, cowboy shot, full body, wide shot") };
    case Shot::CloseUp:
        return { QStringLiteral("close-up"),
            QStringLiteral("upper body, cowboy shot, full body, wide shot, head out of frame, cropped head") };
    case Shot::Portrait:
        return { QStringLiteral("portrait"),
            QStringLiteral("upper body, cowboy shot, full body, wide shot, close-up, head out of frame") };
    case Shot::Medium:
        return { QStringLiteral("upper body"),
            QStringLiteral("cowboy shot, full body, wide shot, portrait, close-up, head out of frame") };
    case Shot::Cowboy:
        return { QStringLiteral("cowboy shot"),
            QStringLiteral("full body, upper body, portrait, close-up, wide shot, head out of frame") };
    case Shot::FullBody:
        return { QStringLiteral("full body"),
            QStringLiteral("upper body, cowboy shot, portrait, close-up, feet out of frame, head out of frame, "
                           "cropped legs") };
    case Shot::Wide:
        return { QStringLiteral("wide shot, full body"), QStringLiteral("upper body, cowboy shot, portrait, close-up") };
    }
    return {};
}

// The shot's framing clauses; tag models get Danbooru framing tags instead of phrases.
inline Prefix forShot(ShotPicker::Shot shot, bool tags = false)
{
    using Shot = ShotPicker::Shot;
    if (tags) return tagsForShot(shot);
    switch (shot) {
    case Shot::Free:
        return {};
    case Shot::ExtremeCloseUp:
        return { QStringLiteral("extreme close-up shot, a tight detail filling the entire frame, "
                                "very shallow depth of field"),
            QStringLiteral("wide shot, full body, medium shot, distant subject, zoomed out") };
    case Shot::CloseUp:
        return { QStringLiteral("close-up of the head, the entire head visible from the top of the hair to the chin, "
                                "neck and top of the shoulders visible, space above the hair"),
            QStringLiteral("full body, wide shot, visible waist, distant subject, zoomed out, extreme close-up, "
                           "cropped head, cut-off forehead, top of head out of frame") };
    case Shot::Portrait:
        return { QStringLiteral("head and shoulders portrait, framed from the chest up"),
            QStringLiteral("full body, wide shot, visible legs, extreme close-up, cropped head") };
    case Shot::Medium:
        return { QStringLiteral("medium shot, visible from the top of the head down to the waist, "
                                "waist, hips and hands visible"),
            QStringLiteral("full body, wide shot, visible legs, head and shoulders portrait, close-up, cropped head") };
    case Shot::Cowboy:
        return { QStringLiteral("framed from the top of the head to mid-thigh, hips and upper thighs visible, "
                                "knees out of frame"),
            QStringLiteral("full body, visible feet, wide shot, head and shoulders portrait, close-up, cropped head") };
    case Shot::FullBody:
        return { QStringLiteral("full body shot, the entire figure visible from head to feet"),
            QStringLiteral("close-up, cropped feet, cropped legs, cropped head, out of frame") };
    case Shot::Wide:
        return { QStringLiteral("wide shot, full figure seen from far away, small in the frame, "
                                "a vast environment filling most of the image"),
            QStringLiteral("upper body, face close-up, portrait, subject filling the frame, cropped subject") };
    }
    return {};
}

// Places prefix before text as a comma-separated phrase; either may be blank.
inline QString prepend(const QString& prefix, const QString& text)
{
    const QString added = prefix.trimmed();
    if (added.isEmpty())
        return text;
    const QString rest = text.trimmed();
    return rest.isEmpty() ? added : added + QStringLiteral(", ") + rest;
}
}
