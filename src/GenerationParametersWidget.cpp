#include "GenerationParametersWidget.h"
#include "ResolutionPicker.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/Theme.h>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLocale>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <cmath>
#include <nukleus/ModelOptions.h>
#include <limits>
#include <numeric>

namespace {
void showFormRow(QFormLayout *form, QWidget *field, bool visible)
{
    field->setVisible(visible);
    if (auto *label = form->labelForField(field)) label->setVisible(visible);
}
}

GenerationParametersWidget::GenerationParametersWidget(QWidget* parent) : QWidget(parent)
{
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setSpacing(Ardoise::Theme::tokens().metrics.spacing6);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_model = new QComboBox(this);
    m_model->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_model->setMinimumContentsLength(12);
    m_model->setObjectName(QStringLiteral("generationModel"));
    form->addRow(tr("Model:"), m_model);
    m_preset = new QComboBox(this); m_preset->setObjectName("generationPreset");
    m_preset->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_preset->setMinimumContentsLength(12);
    form->addRow(tr("Preset:"), m_preset);
    connect(m_preset, &QComboBox::currentIndexChanged, this, &GenerationParametersWidget::resolveModel);
    m_resolution = new QComboBox(this);
    m_resolution->setObjectName(QStringLiteral("resolution"));
    m_resolution->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_resolution->setMinimumContentsLength(12);
    form->addRow(tr("Resolution:"), m_resolution);
    m_resolutionPicker = new ResolutionPicker(this);
    m_resolutionPicker->setObjectName(QStringLiteral("resolutionPicker"));
    m_resolutionPicker->setAccessibleName(tr("Resolution"));
    m_resolutionPicker->hide();
    form->addRow(m_resolutionPicker);
    showFormRow(form, m_preset, false);
    // The picker is a view of the resolution combo, which keeps the selection logic.
    connect(m_resolutionPicker, &ResolutionPicker::currentIndexChanged, m_resolution, &QComboBox::setCurrentIndex);
    connect(m_resolution, &QComboBox::currentIndexChanged, m_resolutionPicker, &ResolutionPicker::setCurrentIndex);
    m_steps = new QSpinBox(this);
    m_steps->setObjectName(QStringLiteral("steps"));
    m_steps->setRange(1, 200);
    form->addRow(tr("Steps:"), m_steps);
    m_cfg = new QDoubleSpinBox(this);
    m_cfg->setObjectName(QStringLiteral("cfgScale"));
    m_cfg->setRange(0, 100);
    m_cfg->setDecimals(4);
    m_cfg->setSingleStep(0.5);
    form->addRow(tr("CFG Scale:"), m_cfg);
    auto* seedRow = new QWidget(this);
    auto* seedLayout = new QHBoxLayout(seedRow);
    seedLayout->setContentsMargins(0, 0, 0, 0);
    m_seed = new QSpinBox(seedRow);
    m_seed->setObjectName(QStringLiteral("seed"));
    m_seed->setRange(-1, std::numeric_limits<int>::max());
    seedLayout->addWidget(m_seed, 1);
    auto* random = new Ardoise::IconButton(Ardoise::Icon::Dice, tr("Randomize seed"), seedRow);
    random->setObjectName(QStringLiteral("randomizeSeed"));
    random->setToolTip(tr("Randomize seed"));
    random->setAccessibleName(tr("Randomize seed"));
    seedLayout->addWidget(random);
    form->addRow(tr("Seed:"), seedRow);
    connect(random, &QToolButton::clicked, this,
            [this]
            {
                m_seed->setValue(static_cast<int>(QRandomGenerator::global()->bounded(
                    static_cast<quint32>(std::numeric_limits<int>::max()))));
            });
    connect(m_model, &QComboBox::currentIndexChanged, this,
            [this]
            {
                applyModelRecommendations(false);
                emit parametersChanged();
            });
    connect(m_resolution, &QComboBox::currentIndexChanged, this,
            [this]
            {
                m_resolution->setToolTip(m_resolution->currentText());
                m_resolutionOverridden = true;
                emit parametersChanged();
            });
    connect(m_steps, &QSpinBox::valueChanged, this,
            [this]
            {
                m_stepsOverridden = true;
                emit parametersChanged();
            });
    connect(m_cfg, &QDoubleSpinBox::valueChanged, this,
            [this]
            {
                m_cfgOverridden = true;
                emit parametersChanged();
            });
    connect(m_seed, &QSpinBox::valueChanged, this, &GenerationParametersWidget::parametersChanged);
    setModels({});
    resetDefaults();
}

void GenerationParametersWidget::showGuidedControls(bool showResolution)
{
    auto* form = qobject_cast<QFormLayout*>(layout());
    for (auto* field : {static_cast<QWidget*>(m_resolution), static_cast<QWidget*>(m_steps),
             static_cast<QWidget*>(m_cfg), m_seed->parentWidget()})
        showFormRow(form, field, false);
    m_resolutionPicker->setVisible(showResolution);
}

void GenerationParametersWidget::setModels(const QList<Nukleus::ModelInfo>& models)
{
    const auto previous = modelId();
    {
        const QSignalBlocker blocker(m_model);
        m_model->clear();
        for (const auto& model : models)
            if (model.category == Nukleus::ModelCategory::ImageGeneration
                && model.recommendedOptions.value("canGenerate").toBool(true))
            {
                m_model->addItem(model.displayLabel(), model.id);
                m_model->setItemData(m_model->count() - 1, model.recommendedOptions, Qt::UserRole + 1);
            }
        if (m_model->count() == 0)
        {
            m_model->addItem(tr("No generation models available"));
            m_model->setEnabled(false);
        }
        else
        {
            m_model->setEnabled(true);
            const int index = m_model->findData(previous);
            if (index >= 0)
                m_model->setCurrentIndex(index);
        }
    }
    if (m_routed) {
        resolveModel();
    } else {
        applyModelRecommendations(modelId() == previous);
    }
    emit parametersChanged();
}

void GenerationParametersWidget::setRouting(const std::optional<Nukleus::ModelRouting> &routing, bool supported)
{
    const auto previous = m_preset->currentData().toString().isEmpty() ? m_selectedPreset : m_preset->currentData().toString();
    m_routed = true; m_routing = routing;
    auto *form = qobject_cast<QFormLayout *>(layout());
    showFormRow(form, m_model, false); showFormRow(form, m_preset, true);
    {
        const QSignalBlocker blocker(m_preset);
        m_preset->clear();
        if (routing) for (const auto &p : routing->presets)
            m_preset->addItem(p.displayLabel(), p.id);
        if (!m_preset->count()) m_preset->addItem(supported ? tr("No presets available — configure in Service Manager") : tr("Upgrade Nukleus to use presets"));
        const int index = m_preset->findData(previous);
        m_preset->setCurrentIndex(index >= 0 ? index : 0);
    }
    resolveModel(); setRoutingLocked(m_locked);
}

const Nukleus::GenerationPreset *GenerationParametersWidget::selectedPreset() const
{
    if (m_routing) for (const auto &p : m_routing->presets)
        if (p.id == m_preset->currentData().toString()) return &p;
    return nullptr;
}

void GenerationParametersWidget::resolveModel()
{
    const auto *preset = selectedPreset();
    if (preset) m_selectedPreset = preset->id;
    const QString previous = m_resolvedModel;
    // Steps/CFG overrides belong to one preset, even when another shares its model.
    if (m_resolvedPreset != m_preset->currentData().toString()) {
        m_stepsOverridden = false;
        m_cfgOverridden = false;
    }
    m_resolvedPreset = m_preset->currentData().toString();
    const auto id = preset ? preset->model.modelId : QString();
    {
        const QSignalBlocker blocker(m_model);
        m_model->setCurrentIndex(id.isEmpty() ? -1 : m_model->findData(id));
    }
    m_resolvedModel = modelId();
    m_routingIssue = preset ? presetIssue(preset->id) : QString();
    m_preset->setToolTip(!m_routingIssue.isEmpty() ? m_routingIssue
        : preset ? tr("Positive prefix: %1\nNegative prefix: %2").arg(preset->positivePrefix, preset->negativePrefix)
                 : QString());
    applyModelRecommendations(previous == m_resolvedModel);
    emit parametersChanged();
}

bool GenerationParametersWidget::routingAvailable() const
{
    return m_routed && selectedPreset() && !modelId().isEmpty();
}

QString GenerationParametersWidget::routingIssue() const { return m_routingIssue; }

QList<Nukleus::GenerationPreset> GenerationParametersWidget::presets() const
{
    return m_routing ? m_routing->presets : QList<Nukleus::GenerationPreset>();
}

QString GenerationParametersWidget::presetIssue(const QString &id) const
{
    for (const auto &preset : presets())
        if (preset.id == id) {
            if (preset.model.modelId.isEmpty())
                return tr("Unassigned — configure this preset in Service Manager");
            return m_model->findData(preset.model.modelId) < 0 ? tr("Assigned model is unavailable") : QString();
        }
    return tr("No presets available — configure in Service Manager");
}

bool GenerationParametersWidget::presetUsesReferences(const QString &id) const
{
    for (const auto &preset : presets())
        if (preset.id == id) {
            const int index = m_model->findData(preset.model.modelId);
            return preset.model.modelId.isEmpty() || index < 0
                || m_model->itemData(index, Qt::UserRole + 1).toJsonObject().value("canEdit").toBool(true);
        }
    return true;
}

void GenerationParametersWidget::setRoutingLocked(bool locked)
{
    m_locked = locked;
    m_preset->setEnabled(!locked && m_routing && !m_routing->presets.isEmpty());
}

void GenerationParametersWidget::applyModelRecommendations(bool preserveOverrides)
{
    if (!preserveOverrides)
    {
        m_stepsOverridden = false;
        m_cfgOverridden = false;
        m_resolutionOverridden = false;
    }
    const auto defaults = Nukleus::Protocol::defaultModelOptions(QStringLiteral("image-generation"));
    auto options = m_model->currentData(Qt::UserRole + 1).toJsonObject();
    if (const auto *preset = m_routed ? selectedPreset() : nullptr) {
        if (preset->model.steps) options.insert("steps", *preset->model.steps);
        if (preset->model.cfgScale) options.insert("cfgScale", *preset->model.cfgScale);
    }
    const int width = options.value("width").toInt(defaults.value("width").toInt());
    const int height = options.value("height").toInt(defaults.value("height").toInt());
    const QSize configured(width, height);
    const qint64 pixels = qint64(width) * height;
    const QSize previousResolution = resolution();
    const QSignalBlocker resolutionBlocker(m_resolution);
    m_resolution->clear();
    struct Ratio
    {
        QString name;
        int width;
        int height;
    };
    const QList<Ratio> ratios = {{tr("Square"), 1, 1},
                                 {tr("Landscape"), 4, 3},
                                 {tr("Portrait"), 3, 4},
                                 {tr("Photo landscape"), 3, 2},
                                 {tr("Photo portrait"), 2, 3},
                                 {tr("Classic landscape"), 5, 3},
                                 {tr("Classic portrait"), 3, 5},
                                 {tr("Widescreen"), 16, 9},
                                 {tr("Tall"), 9, 16},
                                 {tr("Ultrawide"), 21, 9},
                                 {tr("Ultra-tall"), 9, 21},
                                 {tr("Panorama"), 2, 1},
                                 {tr("Vertical panorama"), 1, 2},
                                 {tr("Wide panorama"), 4, 1},
                                 {tr("Tall panorama"), 1, 4}};
    const auto addResolution = [this](const QString& name, int ratioWidth, int ratioHeight, const QSize& size)
    {
        m_resolution->addItem(
            tr("%1 — %2:%3 — %4 × %5")
                .arg(name)
                .arg(qint64(size.width()) * ratioHeight == qint64(size.height()) * ratioWidth
                         ? QString::number(ratioWidth)
                         : QStringLiteral("≈%1").arg(ratioWidth))
                .arg(ratioHeight)
                .arg(size.width()).arg(size.height()), // Pixel sizes never use digit grouping.
            size);
        m_resolution->setItemData(m_resolution->count() - 1,
                                  m_resolution->itemText(m_resolution->count() - 1), Qt::ToolTipRole);
        // Tiles show the target ratio; the full text (with ≈ when approximate) is their tooltip.
        m_resolution->setItemData(m_resolution->count() - 1,
            QStringLiteral("%1:%2").arg(ratioWidth).arg(ratioHeight), Qt::UserRole + 1);
        m_resolution->setItemData(m_resolution->count() - 1, name, Qt::UserRole + 2);
    };
    for (const auto& ratio : ratios)
    {
        if (pixels <= 0)
            continue;
        constexpr int alignment = 64;
        const double idealWidth = std::sqrt(double(pixels) * ratio.width / ratio.height);
        const double idealHeight = std::sqrt(double(pixels) * ratio.height / ratio.width);
        const int lowerWidth = int(std::floor(idealWidth / alignment)) * alignment;
        const int lowerHeight = int(std::floor(idealHeight / alignment)) * alignment;
        QSize size;
        qint64 bestPixels = -1;
        double bestRatioError = std::numeric_limits<double>::max();
        // Consider adjacent aligned sizes around the ideal dimensions. Prefer
        // the fullest budget, breaking equal-area ties by closeness to the ratio.
        for (int candidateWidth : {lowerWidth, lowerWidth + alignment})
            for (int candidateHeight : {lowerHeight, lowerHeight + alignment})
            {
                const qint64 area = qint64(candidateWidth) * candidateHeight;
                if (candidateWidth < 64 || candidateHeight < 64 || candidateWidth > 4096 ||
                    candidateHeight > 4096 || area > pixels)
                    continue;
                if ((ratio.width < ratio.height && candidateWidth >= candidateHeight)
                    || (ratio.width > ratio.height && candidateWidth <= candidateHeight))
                    continue;
                const double ratioError = std::abs(std::log(double(candidateWidth) * ratio.height /
                                                            (double(candidateHeight) * ratio.width)));
                if (area > bestPixels || (area == bestPixels && ratioError < bestRatioError))
                {
                    bestPixels = area;
                    bestRatioError = ratioError;
                    size = QSize(candidateWidth, candidateHeight);
                }
            }
        if (!size.isValid())
            continue;
        addResolution(ratio.name, ratio.width, ratio.height, size);
    }
    if (m_resolution->findData(configured) < 0)
    {
        const int divisor = std::gcd(width, height);
        addResolution(tr("Model resolution"), width / divisor, height / divisor, configured);
    }
    const int previousIndex = preserveOverrides && m_resolutionOverridden ? m_resolution->findData(previousResolution) : -1;
    if (previousIndex < 0)
        m_resolutionOverridden = false;
    m_resolution->setCurrentIndex(previousIndex >= 0 ? previousIndex : m_resolution->findData(configured));
    m_resolution->setToolTip(m_resolution->currentText());
    QList<ResolutionPicker::Option> pickerOptions;
    for (int i = 0; i < m_resolution->count(); ++i)
        pickerOptions.append({m_resolution->itemData(i).toSize(), m_resolution->itemData(i, Qt::UserRole + 1).toString(),
            m_resolution->itemData(i, Qt::UserRole + 2).toString(), m_resolution->itemText(i)});
    m_resolutionPicker->setOptions(pickerOptions, m_resolution->currentIndex());
    const QSignalBlocker stepsBlocker(m_steps);
    const QSignalBlocker cfgBlocker(m_cfg);
    if (!m_stepsOverridden)
        m_steps->setValue(options.value("steps").toInt(defaults.value("steps").toInt()));
    if (!m_cfgOverridden)
        m_cfg->setValue(options.value("cfgScale").toDouble(defaults.value("cfgScale").toDouble()));
}

QString GenerationParametersWidget::modelId() const { return m_model->currentData().toString(); }

QString GenerationParametersWidget::modelName() const { return m_model->currentText(); }
QSize GenerationParametersWidget::resolution() const { return m_resolution->currentData().toSize(); }

QString GenerationParametersWidget::resolutionRatio() const
{
    return m_resolution->currentData(Qt::UserRole + 1).toString();
}

QSize GenerationParametersWidget::resolutionForRatio(const QString& ratio) const
{
    const int index = m_resolution->findData(ratio, Qt::UserRole + 1);
    return ratio.isEmpty() || index < 0 ? QSize() : m_resolution->itemData(index).toSize();
}

bool GenerationParametersWidget::selectPreset(const QString& id)
{
    const int index = m_preset->findData(id);
    if (id.isEmpty() || index < 0) return false;
    m_preset->setCurrentIndex(index);
    return true;
}

bool GenerationParametersWidget::selectRatio(const QString& ratio)
{
    const int index = m_resolution->findData(ratio, Qt::UserRole + 1);
    if (ratio.isEmpty() || index < 0) return false;
    m_resolution->setCurrentIndex(index);
    return true;
}

int GenerationParametersWidget::steps() const { return m_steps->value(); }
double GenerationParametersWidget::cfgScale() const { return m_cfg->value(); }

int GenerationParametersWidget::seed() const { return m_seed->value(); }

Nukleus::ImageGenerationRequest GenerationParametersWidget::generationRequest() const
{
    Nukleus::ImageGenerationRequest request;
    request.modelId = modelId();
    if (m_routed)
        request.presetId = m_preset->currentData().toString();
    request.width = resolution().width();
    request.height = resolution().height();
    request.steps = steps();
    request.cfgScale = cfgScale();
    request.seed = seed();
    const auto options = m_model->currentData(Qt::UserRole + 1).toJsonObject();
    request.sampler = options.value("sampler").toString();
    request.scheduler = options.value("scheduler").toString();
    return request;
}

void GenerationParametersWidget::resetDefaults()
{
    // Model choice and session overrides survive New Image.
    m_seed->setValue(-1);
}
