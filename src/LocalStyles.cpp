#include "LocalStyles.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSet>
#include <algorithm>

namespace
{
QString settingsKey(const QString& modelId)
{
    return QStringLiteral("LocalStyles/%1").arg(QString::fromLatin1(
        modelId.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
}
}

LocalStyleData loadLocalStyles(const QString& modelId)
{
    if (modelId.isEmpty()) return {};
    const auto object = QJsonDocument::fromJson(QSettings().value(settingsKey(modelId)).toByteArray()).object();
    return {Nukleus::Protocol::promptStylesFromJson(object.value("styles")).value_or(QList<Nukleus::PromptStyle>{}),
        Nukleus::Protocol::promptStylesFromJson(object.value("overrides")).value_or(QList<Nukleus::PromptStyle>{})};
}

bool saveLocalStyles(const QString& modelId, const LocalStyleData& data, QString* error)
{
    if (modelId.isEmpty()) return false;
    auto combined = data.styles;
    combined.append(data.overrides);
    if (!Nukleus::Protocol::validatePromptStyles(combined, error)) return false;
    QSettings settings;
    const QJsonObject object{{"styles", Nukleus::Protocol::promptStylesToJson(data.styles)},
        {"overrides", Nukleus::Protocol::promptStylesToJson(data.overrides)}};
    settings.setValue(settingsKey(modelId), QJsonDocument(object).toJson(QJsonDocument::Compact));
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QList<EffectiveStyle> effectiveStyles(const QList<Nukleus::PromptStyle>& server, const LocalStyleData& data)
{
    QList<EffectiveStyle> result;
    QSet<QString> seen;
    for (const auto& style : server) {
        const auto override = std::find_if(data.overrides.cbegin(), data.overrides.cend(),
            [&](const auto& candidate) { return candidate.id == style.id; });
        result.append({override == data.overrides.cend() ? style : *override,
            override != data.overrides.cend(), override != data.overrides.cend()});
        seen.insert(style.id);
    }
    // A saved override remains usable even if its server style is later removed.
    for (const auto& style : data.overrides)
        if (!seen.contains(style.id)) { result.append({style, true, true}); seen.insert(style.id); }
    for (const auto& style : data.styles)
        if (!seen.contains(style.id)) { result.append({style, true, false}); seen.insert(style.id); }
    return result;
}
