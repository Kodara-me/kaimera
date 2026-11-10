#include "LoraDataset.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

namespace LoraDataset
{
namespace
{
bool isNameCharacter(char32_t c)
{
    return (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') || (c >= U'0' && c <= U'9') || c == U'_' || c == U'-';
}
} // namespace

QStringList scanImages(const QString& folder)
{
    const QDir dir(folder);
    QStringList images;
    for (const QFileInfo& file : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        const QString suffix = file.suffix().toLower();
        if (suffix == QLatin1String("png") || suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
            images.append(file.absoluteFilePath());
    }
    std::sort(images.begin(), images.end(), [](const QString& a, const QString& b) {
        return QFileInfo(a).fileName() < QFileInfo(b).fileName();
    });
    return images;
}

QString deriveName(const QString& folder)
{
    const QString folderName = QDir(folder).dirName();
    QString name;
    for (const char32_t c : folderName.toUcs4()) {
        if (name.size() >= MaxNameLength) break;
        name += isNameCharacter(c) ? QChar(char16_t(c)) : QLatin1Char('_');
    }
    return name;
}

bool isValidName(const QString& name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    return pattern.match(name).hasMatch();
}

QString captionFileText(const QString& imagePath)
{
    const QFileInfo image(imagePath);
    QFile file(image.dir().filePath(image.completeBaseName() + QStringLiteral(".txt")));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);
    return text.trimmed();
}

QString caption(const QString& triggerWord, const QString& fileText)
{
    return fileText.isEmpty() ? triggerWord : triggerWord + QStringLiteral(", ") + fileText;
}

int captionedCount(const QStringList& images)
{
    return int(std::count_if(images.cbegin(), images.cend(),
        [](const QString& image) { return !captionFileText(image).isEmpty(); }));
}

QList<Nukleus::ModelInfo> baseModels(const QList<Nukleus::ModelInfo>& imageModels)
{
    QList<Nukleus::ModelInfo> models;
    for (const auto& model : imageModels)
        if (model.modelType == QLatin1String("checkpoint")
            && model.fileName.endsWith(QLatin1String(".safetensors"), Qt::CaseInsensitive))
            models.append(model);
    return models;
}

QStringList companionNames(const QList<Nukleus::ModelInfo>& companions)
{
    QStringList names;
    for (const auto& model : companions) names.append(QFileInfo(model.fileName).completeBaseName());
    return names;
}
} // namespace LoraDataset
