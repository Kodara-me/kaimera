#include "AvailableModelsDialog.h"
#include "CanvasServiceController.h"
#include <Ardoise/IconProvider.h>
#include <Ardoise/Panel.h>
#include <Ardoise/TreeView.h>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
constexpr int ModelIdRole = Qt::UserRole + 1;
constexpr int CategoryRole = Qt::UserRole + 2;
QString valueText(const QJsonValue& value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QLocale().toString(value.toDouble(), 'g', 12);
    if (value.isBool())
        return value.toBool() ? QObject::tr("Yes") : QObject::tr("No");
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    return QStringLiteral("—");
}
} // namespace
AvailableModelsDialog::AvailableModelsDialog(CanvasServiceController* client, QWidget* parent)
    : Ardoise::Dialog(parent), m_client(client)
{
    setWindowTitle(tr("Available Models"));
    if (parent)
        setWindowIcon(parent->windowIcon());
    resize(900, 560);
    setMinimumSize(700, 420);
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    setContentWidget(content);
    m_status = new QLabel(content);
    m_status->setWordWrap(true);
    m_status->setObjectName("modelsServiceStatus");
    layout->addWidget(m_status);
    auto* splitter = new QSplitter(Qt::Horizontal, content);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter, 1);
    auto* library = new Ardoise::Panel(splitter);
    auto* left = new QVBoxLayout(library);
    m_tree = new Ardoise::TreeView(library);
    m_tree->setObjectName("availableModelsTree");
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_model = new QStandardItemModel(m_tree);
    m_tree->setModel(m_model);
    left->addWidget(m_tree);
    auto* configuration = new Ardoise::Panel(splitter);
    auto* right = new QVBoxLayout(configuration);
    m_details = new QLabel(configuration);
    m_details->setObjectName("availableModelDetails");
    m_details->setWordWrap(true);
    m_details->setTextFormat(Qt::PlainText);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    right->addWidget(m_details);
    right->addWidget(new QLabel(tr("Server configuration (read-only)"), configuration));
    m_empty = new QLabel(configuration);
    m_empty->setObjectName("modelConfigurationPlaceholder");
    m_empty->setWordWrap(true);
    right->addWidget(m_empty);
    m_options = new QTableWidget(configuration);
    m_options->setObjectName("modelRecommendedOptions");
    m_options->setColumnCount(2);
    m_options->setHorizontalHeaderLabels({tr("Option"), tr("Value")});
    m_options->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_options->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_options->setShowGrid(false);
    m_options->verticalHeader()->hide();
    m_options->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_options->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    right->addWidget(m_options, 1);
    splitter->setSizes({280, 580});
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, content);
    m_refresh = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    m_refresh->setObjectName("refreshModels");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_refresh, &QPushButton::clicked, m_client, &CanvasServiceController::requestModels);
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &AvailableModelsDialog::showSelection);
    connect(m_client, &CanvasServiceController::modelsChanged, this, &AvailableModelsDialog::populateModels);
    connect(m_client, &CanvasServiceController::serviceStateChanged, this,
            [this]
            {
                updateConnection();
                showSelection();
            });
    populateModels();
    m_client->requestModels();
}
void AvailableModelsDialog::updateConnection()
{
    m_refresh->setEnabled(m_client->isAuthenticated());
    m_status->setText(m_client->isAuthenticated() && !m_client->modelListAvailable()
                          ? tr("Loading models from the service…")
                          : m_client->serviceMessage());
}
void AvailableModelsDialog::populateModels()
{
    const QString selectedId = m_tree->currentIndex().data(ModelIdRole).toString();
    const int selectedCategory = m_tree->currentIndex().data(CategoryRole).toInt();
    QSignalBlocker blocker(m_tree->selectionModel());
    m_model->clear();
    const QList<QPair<Nukleus::ModelCategory, QString>> categories{
        {Nukleus::ModelCategory::ImageGeneration, tr("Image generation")},
        {Nukleus::ModelCategory::Upscaler, tr("Upscaler")},
        {Nukleus::ModelCategory::LlmChat, tr("LLM")},
        {Nukleus::ModelCategory::Companion, tr("Companion")}};
    QModelIndex selected;
    for (const auto& category : categories)
    {
        auto* branch = new QStandardItem(Ardoise::IconProvider::icon(Ardoise::Icon::Folder), category.second);
        branch->setEditable(false);
        branch->setData(int(category.first), CategoryRole);
        m_model->appendRow(branch);
        if (int(category.first) == selectedCategory)
            selected = branch->index();
        for (const auto& model : m_client->models(category.first))
        {
            auto* item = new QStandardItem(Ardoise::IconProvider::icon(Ardoise::Icon::File), model.displayLabel());
            item->setEditable(false);
            item->setToolTip(model.fileName);
            item->setData(model.id, ModelIdRole);
            item->setData(int(category.first), CategoryRole);
            branch->appendRow(item);
            if (!selectedId.isEmpty() && model.id == selectedId)
                selected = item->index();
        }
    }
    m_tree->expandAll();
    m_tree->setCurrentIndex(selected.isValid() ? selected : m_model->index(0, 0));
    updateConnection();
    showSelection();
}
void AvailableModelsDialog::showSelection()
{
    m_options->setRowCount(0);
    m_options->hide();
    m_empty->show();
    m_details->setText(tr("Select a model to view its configuration."));
    if (!m_client->isAuthenticated())
    {
        m_empty->setText(tr("Models are unavailable while the service is disconnected."));
        return;
    }
    if (!m_client->modelListAvailable())
    {
        m_empty->setText(tr("Loading models from the service…"));
        return;
    }
    const QString id = m_tree->currentIndex().data(ModelIdRole).toString();
    if (id.isEmpty())
    {
        const auto category = Nukleus::ModelCategory(m_tree->currentIndex().data(CategoryRole).toInt());
        m_empty->setText(m_client->models(category).isEmpty() ? tr("No models available in this category.")
                                                              : tr("Select a model in this category."));
        return;
    }
    for (const auto& model : m_client->models())
    {
        if (model.id != id)
            continue;
        QString details = tr("%1\nFile: %2\nSize: %3 MiB")
                              .arg(model.displayLabel(), model.fileName,
                                   QLocale().toString(model.fileSize / (1024.0 * 1024.0), 'f', 1));
        if (model.category != Nukleus::ModelCategory::Upscaler && model.category != Nukleus::ModelCategory::Companion)
            details += tr("\nExplicit content: %1").arg(model.explicitContent ? tr("Yes") : tr("No"));
        if (!model.mmprojModelId.isEmpty())
            for (const auto& companion : m_client->models(Nukleus::ModelCategory::Companion))
                if (companion.id == model.mmprojModelId)
                    details += tr("\nCompanion (%1): %2").arg("MMPROJ", companion.displayLabel());
        auto files = model.companionFiles;
        if (!model.companionFileName.isEmpty())
            files.insert(model.companionRole, model.companionFileName);
        for (auto it = files.begin(); it != files.end(); ++it) {
            if (model.category == Nukleus::ModelCategory::ImageGeneration
                && (it.key() == "clip-l" || it.key() == "clip-g" || it.key() == "mmproj")) continue;
            details += tr("\nCompanion (%1): %2")
                           .arg(it.key(), QFileInfo(it.value().toString()).fileName());
        }
        m_details->setText(details);
        const auto& options = model.recommendedOptions;
        if (options.isEmpty())
        {
            m_empty->setText(model.category == Nukleus::ModelCategory::Upscaler
                ? tr("This model has no options.") : tr("No configuration has been saved for this model on the server."));
            return;
        }
        const auto row = [this](const QString& label, const QString& value)
        {
            const int index = m_options->rowCount();
            m_options->insertRow(index);
            for (int column = 0; column < 2; ++column)
            {
                auto* item = new QTableWidgetItem(column == 0 ? label : value);
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                m_options->setItem(index, column, item);
            }
        };
        if (options.contains("width") && options.contains("height"))
            row(tr("Resolution"),
                tr("%1 × %2").arg(valueText(options.value("width")), valueText(options.value("height"))));
        const QList<QPair<QString, QString>> fields{{"width", tr("Width")},
                                                    {"height", tr("Height")},
                                                    {"steps", tr("Steps")},
                                                    {"cfgScale", tr("CFG scale")},
                                                    {"sampler", tr("Sampler")},
                                                    {"scheduler", tr("Scheduler")},
                                                    {"canEdit", tr("Can Edit")},
                                                    {"canGenerate", tr("Can Generate")},
                                                    {"vaeModelId", tr("VAE")},
                                                    {"textEncoderModelId", tr("Text encoder")},
                                                    {"canBeTextEncoder", tr("Can be used as text encoder")},
                                                    {"defaultPromptEnhancer", tr("Default prompt enhancer")},
                                                    {"defaultPromptTranslator", tr("Default prompt translator")},
                                                    {"temperature", tr("Temperature")},
                                                    {"topP", tr("Top-p")},
                                                    {"contextLength", tr("Context length")},
                                                    {"maxTokens", tr("Maximum tokens")},
                                                    {"subtype", tr("Type")}};
        QStringList handled{"clipSkip"};
        for (const auto& field : fields)
        {
            handled.append(field.first);
            if (!options.contains(field.first))
                continue;
            if ((field.first == "width" || field.first == "height") && options.contains("width") &&
                options.contains("height"))
                continue;
            const auto value = options.value(field.first);
            if (field.first == "vaeModelId" || field.first == "textEncoderModelId") {
                QString name = value.toString().isEmpty() ? tr("None") : value.toString();
                for (const auto& target : m_client->models())
                    if (target.id == value.toString()) { name = target.displayLabel(); break; }
                row(field.second, name);
                continue;
            }
            const bool automatic = ((field.first == "sampler" || field.first == "scheduler") &&
                                    value.isString() && value.toString().isEmpty());
            row(field.second, automatic ? tr("Automatic") : valueText(value));
        }
        for (auto it = options.constBegin(); it != options.constEnd(); ++it)
            if (!handled.contains(it.key()))
                row(it.key(), valueText(it.value()));
        m_empty->hide();
        m_options->show();
        return;
    }
}
