#include "TrainLoraDialog.h"
#include "LoraDataset.h"
#include <Ardoise/PrimaryButton.h>
#include <Ardoise/Theme.h>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QVBoxLayout>

TrainLoraDialog::TrainLoraDialog(const QList<Nukleus::ModelInfo>& baseModels, const QString& preferredModelId,
    const QStringList& companionNames, bool jobActive, QWidget* parent)
    : Ardoise::Dialog(parent)
    , m_companionNames(companionNames)
    , m_jobActive(jobActive)
{
    setObjectName(QStringLiteral("trainLoraDialog"));
    setWindowTitle(tr("Train a LoRA"));
    if (parent) setWindowIcon(parent->windowIcon());
    setMinimumWidth(520);
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setSpacing(metrics.spacing12);

    auto* intro = new QLabel(tr("Pick a folder of images of a character or a style. Kaimera trains a LoRA from "
                                "them, which you can then use to generate images."), content);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setVerticalSpacing(metrics.spacing8);

    auto* folderRow = new QVBoxLayout;
    auto* folderButtons = new QHBoxLayout;
    m_folderButton = new QPushButton(tr("Choose folder…"), content);
    m_folderButton->setObjectName(QStringLiteral("loraFolderButton"));
    folderButtons->addWidget(m_folderButton);
    m_folderPath = new QLabel(tr("No folder chosen"), content);
    m_folderPath->setObjectName(QStringLiteral("loraFolderPath"));
    m_folderPath->setTextFormat(Qt::PlainText);
    m_folderPath->setWordWrap(true);
    folderButtons->addWidget(m_folderPath, 1);
    folderRow->addLayout(folderButtons);
    m_summary = new QLabel(content);
    m_summary->setObjectName(QStringLiteral("loraFolderSummary"));
    folderRow->addWidget(m_summary);
    form->addRow(tr("Images"), folderRow);

    m_name = new QLineEdit(content);
    m_name->setObjectName(QStringLiteral("loraName"));
    m_name->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[A-Za-z0-9_-]{0,%1}").arg(LoraDataset::MaxNameLength)), m_name));
    m_name->setToolTip(tr("Letters, digits, _ and - only, 64 characters at most"));
    form->addRow(tr("Name"), m_name);

    m_trigger = new QLineEdit(content);
    m_trigger->setObjectName(QStringLiteral("loraTriggerWord"));
    m_trigger->setMaxLength(LoraDataset::MaxTriggerWordLength);
    m_trigger->setToolTip(tr("The word that calls the LoRA in a prompt"));
    form->addRow(tr("Trigger word"), m_trigger);

    m_baseModel = new QComboBox(content);
    m_baseModel->setObjectName(QStringLiteral("loraBaseModel"));
    for (const auto& model : baseModels) m_baseModel->addItem(model.displayLabel(), model.id);
    if (const int index = m_baseModel->findData(preferredModelId); index >= 0) m_baseModel->setCurrentIndex(index);
    form->addRow(tr("Base model"), m_baseModel);
    auto* note = new QLabel(tr("The base model must be an SDXL checkpoint."), content);
    note->setObjectName(QStringLiteral("loraBaseModelNote"));
    note->setWordWrap(true);
    form->addRow(QString(), note);

    m_steps = new QSpinBox(content);
    m_steps->setObjectName(QStringLiteral("loraSteps"));
    m_steps->setRange(LoraDataset::MinSteps, LoraDataset::MaxSteps);
    m_steps->setValue(LoraDataset::DefaultSteps);
    m_steps->setToolTip(tr("More steps take longer: a training can last tens of minutes"));
    form->addRow(tr("Training steps"), m_steps);
    layout->addLayout(form);

    m_status = new QLabel(content);
    m_status->setObjectName(QStringLiteral("loraStatus"));
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);

    auto* buttons = new QDialogButtonBox(content);
    auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
    cancel->setObjectName(QStringLiteral("loraCancel"));
    m_train = new Ardoise::PrimaryButton(tr("Train"), content);
    m_train->setObjectName(QStringLiteral("loraTrain"));
    buttons->addButton(m_train, QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    setContentWidget(content);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_train, &QPushButton::clicked, this, [this] {
        QString error;
        if (!request(&error)) {
            m_status->setText(error);
            return;
        }
        accept();
    });
    connect(m_folderButton, &QPushButton::clicked, this, &TrainLoraDialog::chooseFolder);
    connect(m_name, &QLineEdit::textEdited, this, [this] { m_nameEdited = true; });
    connect(m_name, &QLineEdit::textChanged, this, [this] {
        // The trigger word follows the name until the user edits it.
        if (!m_triggerEdited) m_trigger->setText(m_name->text());
        update();
    });
    connect(m_trigger, &QLineEdit::textEdited, this, [this] { m_triggerEdited = true; });
    connect(m_trigger, &QLineEdit::textChanged, this, &TrainLoraDialog::update);
    connect(m_baseModel, &QComboBox::currentIndexChanged, this, &TrainLoraDialog::update);
    update();
}

void TrainLoraDialog::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose the image folder"), m_folder);
    if (!folder.isEmpty()) setFolder(folder);
}

void TrainLoraDialog::setFolder(const QString& folder)
{
    m_folder = folder;
    m_images = LoraDataset::scanImages(folder);
    m_captionTexts.clear();
    for (const QString& image : m_images) m_captionTexts.append(LoraDataset::captionFileText(image));
    m_folderPath->setText(QDir::toNativeSeparators(folder));
    // The name follows the folder unless the user typed one.
    if (!m_nameEdited) m_name->setText(LoraDataset::deriveName(folder));
    update();
}

void TrainLoraDialog::setJobActive(bool active)
{
    m_jobActive = active;
    update();
}

QString TrainLoraDialog::name() const { return m_name->text(); }
QString TrainLoraDialog::triggerWord() const { return m_trigger->text().trimmed(); }
QString TrainLoraDialog::baseModelId() const { return m_baseModel->currentData().toString(); }
int TrainLoraDialog::steps() const { return m_steps->value(); }
QString TrainLoraDialog::status() const { return problem(); }

QString TrainLoraDialog::problem() const
{
    if (m_folder.isEmpty()) return tr("Choose a folder of PNG or JPEG images.");
    if (m_images.isEmpty()) return tr("This folder holds no PNG or JPEG image.");
    if (m_images.size() > LoraDataset::MaxImages)
        return tr("A LoRA is trained from %1 images at most; this folder holds %2.")
            .arg(LoraDataset::MaxImages).arg(m_images.size());
    const QString name = m_name->text();
    if (name.isEmpty()) return tr("Enter a name for the LoRA.");
    if (!LoraDataset::isValidName(name))
        return tr("The name can hold only letters, digits, _ and -, up to %1 characters.")
            .arg(LoraDataset::MaxNameLength);
    for (const QString& companion : m_companionNames)
        if (companion.compare(name, Qt::CaseInsensitive) == 0)
            return tr("A model named %1 is already installed. Choose another name.").arg(companion);
    const QString trigger = triggerWord();
    if (trigger.isEmpty()) return tr("Enter a trigger word.");
    if (trigger.size() > LoraDataset::MaxTriggerWordLength)
        return tr("The trigger word can hold %1 characters at most.").arg(LoraDataset::MaxTriggerWordLength);
    if (trigger.contains(QLatin1Char('<')) || trigger.contains(QLatin1Char('>')) || trigger.contains(QLatin1Char(',')))
        return tr("The trigger word cannot hold <, > or a comma.");
    for (qsizetype i = 0; i < m_images.size(); ++i)
        if (LoraDataset::caption(trigger, m_captionTexts.at(i)).size() > LoraDataset::MaxCaptionLength)
            return tr("The caption of %1 holds more than %2 characters. Shorten its .txt file.")
                .arg(QFileInfo(m_images.at(i)).fileName()).arg(LoraDataset::MaxCaptionLength);
    if (baseModelId().isEmpty()) return tr("No SDXL checkpoint is installed.");
    if (m_jobActive) return tr("Wait for the current job to finish.");
    return {};
}

void TrainLoraDialog::update()
{
    if (m_folder.isEmpty()) {
        m_summary->clear();
    } else {
        int captions = 0;
        for (const QString& text : m_captionTexts) captions += text.isEmpty() ? 0 : 1;
        m_summary->setText(tr("%1, %2").arg(tr("%n image(s)", nullptr, int(m_images.size())),
            tr("%n caption(s)", nullptr, captions)));
    }
    const QString issue = problem();
    m_status->setText(issue);
    m_train->setEnabled(issue.isEmpty());
}

std::optional<Nukleus::LoraTrainingRequest> TrainLoraDialog::request(QString* error) const
{
    if (const QString issue = problem(); !issue.isEmpty()) {
        if (error) *error = issue;
        return std::nullopt;
    }
    Nukleus::LoraTrainingRequest request;
    request.baseModelId = baseModelId();
    request.outputName = name();
    request.maxTrainSteps = steps();
    for (qsizetype i = 0; i < m_images.size(); ++i) {
        QFile file(m_images.at(i));
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = tr("Could not read %1: %2").arg(QFileInfo(m_images.at(i)).fileName(), file.errorString());
            return std::nullopt;
        }
        request.images.append({file.readAll(), LoraDataset::caption(triggerWord(), m_captionTexts.at(i))});
    }
    return request;
}
