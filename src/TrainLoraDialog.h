#pragma once
#include <Ardoise/Dialog.h>
#include <nukleus/ImageTypes.h>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

// The form that asks for what training a LoRA needs: the image folder, the name of the LoRA, its
// trigger word, the base model and the number of steps. Train is enabled only when no condition
// is unmet (status() gives the first one); the request is built from the files when Train is clicked.
class TrainLoraDialog final : public Ardoise::Dialog
{
    Q_OBJECT
public:
    // baseModels: the candidates, by display label. preferredModelId is selected when listed,
    // else the first one. companionNames: file names without extension of the installed
    // companions, which the name must differ from. jobActive: a single job runs.
    TrainLoraDialog(const QList<Nukleus::ModelInfo>& baseModels, const QString& preferredModelId,
        const QStringList& companionNames, bool jobActive, QWidget* parent = nullptr);
    // Chooses the folder as the folder button does, without the file dialog.
    void setFolder(const QString& folder);
    void setJobActive(bool active);
    QString folder() const { return m_folder; }
    QStringList images() const { return m_images; }
    QString name() const;
    QString triggerWord() const;
    QString baseModelId() const;
    int steps() const;
    // The first unmet condition of the form, or empty when Train is enabled.
    QString status() const;
    // The request to submit: the images as stored on disk, in order, with their captions, the name,
    // the base model and the steps; nothing else. Empty, with the reason in error, when a condition
    // is unmet or a file cannot be read.
    std::optional<Nukleus::LoraTrainingRequest> request(QString* error = nullptr) const;

private:
    void chooseFolder();
    void update();
    QString problem() const;
    QString m_folder;
    QStringList m_images;
    // The trimmed text of each image's caption file, empty without one.
    QStringList m_captionTexts;
    QStringList m_companionNames;
    bool m_jobActive = false;
    bool m_nameEdited = false;
    bool m_triggerEdited = false;
    QPushButton* m_folderButton;
    QLabel* m_folderPath;
    QLabel* m_summary;
    QLineEdit* m_name;
    QLineEdit* m_trigger;
    QComboBox* m_baseModel;
    QSpinBox* m_steps;
    QLabel* m_status;
    QPushButton* m_train;
};
