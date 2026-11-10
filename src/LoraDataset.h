#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <nukleus/ImageTypes.h>

// The pure rules of LoRA training: which files of a folder are the images, what the LoRA is named
// after the folder and what caption each image gets. They do not touch the dialog.
namespace LoraDataset
{
constexpr int MaxImages = 64;
constexpr int MaxNameLength = 64;
constexpr int MaxTriggerWordLength = 100;
constexpr int MaxCaptionLength = 1000;
constexpr int MinSteps = 1;
constexpr int MaxSteps = 20000;
constexpr int DefaultSteps = 1500;

// The PNG and JPEG files placed directly in folder (extensions .png, .jpg and .jpeg, in any letter
// case; subfolders and other files ignored), as absolute paths sorted by file name.
QStringList scanImages(const QString& folder);

// The LoRA name a folder suggests: its name with each character outside A-Z, a-z, 0-9, _ and -
// replaced by _, cut to MaxNameLength characters. Empty for a folder with no name.
QString deriveName(const QString& folder);

// Whether name is 1 to MaxNameLength characters of A-Z, a-z, 0-9, _ and -.
bool isValidName(const QString& name);

// The trimmed UTF-8 content of the caption file next to an image (same base name, extension .txt),
// or empty when there is none, it cannot be read or it is blank.
QString captionFileText(const QString& imagePath);

// The caption of an image: the trigger word, then, when its caption file text is not empty, ", " and
// that text.
QString caption(const QString& triggerWord, const QString& fileText);

// How many images have a caption file with some text.
int captionedCount(const QStringList& images);

// The installed image-generation models that can be the base of a training: checkpoints whose file
// name ends with .safetensors (in any letter case), in the order given.
QList<Nukleus::ModelInfo> baseModels(const QList<Nukleus::ModelInfo>& imageModels);

// The file names, without extension, of the installed companion models.
QStringList companionNames(const QList<Nukleus::ModelInfo>& companions);
} // namespace LoraDataset
