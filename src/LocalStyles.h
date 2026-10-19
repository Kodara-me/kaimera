#pragma once
#include <nukleus/PromptStyle.h>

struct LocalStyleData
{
    QList<Nukleus::PromptStyle> styles;
    QList<Nukleus::PromptStyle> overrides;
};

struct EffectiveStyle
{
    Nukleus::PromptStyle style;
    bool local = false;
    bool overridden = false;
};

LocalStyleData loadLocalStyles(const QString& modelId);
bool saveLocalStyles(const QString& modelId, const LocalStyleData& data, QString* error = nullptr);
QList<EffectiveStyle> effectiveStyles(const QList<Nukleus::PromptStyle>& server, const LocalStyleData& data);
