#pragma once

#include <QString>

// Paths in these UI requests are client paths. They are uploaded as bytes and
// are never exposed to the service as filesystem paths.
struct ImageGenerationRequest
{
    QString prompt;
    QString negativePrompt;
    QString sampler;
    QString scheduler;
    int width = 1024;
    int height = 1024;
    int steps = 28;
    double cfgScale = 7.0;
    int seed = -1;
    QString baseImagePath;
    double baseImageStrength = 0.55;
};

struct ImageUpscaleRequest
{
    QString sourceImagePath;
    int scale = 2;
};
