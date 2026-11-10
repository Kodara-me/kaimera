#include "LoraPicker.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/Theme.h>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

LoraPicker::LoraPicker(QWidget* parent) : QWidget(parent)
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics.spacing6);
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("LoRAs:"), this);
    title->setObjectName(QStringLiteral("loraPickerTitle"));
    header->addWidget(title);
    header->addStretch();
    m_add = new QPushButton(tr("Add a LoRA"), this);
    m_add->setObjectName(QStringLiteral("loraAdd"));
    m_menu = new QMenu(m_add);
    m_menu->setObjectName(QStringLiteral("loraMenu"));
    m_add->setMenu(m_menu);
    header->addWidget(m_add);
    layout->addLayout(header);
    m_rowsLayout = new QVBoxLayout;
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(metrics.spacing6);
    layout->addLayout(m_rowsLayout);
    refresh();
}

void LoraPicker::setModels(const QList<Nukleus::ModelInfo>& companions)
{
    QList<Nukleus::ModelInfo> offered;
    for (const auto& model : companions)
        if (model.category == Nukleus::ModelCategory::Companion
            && model.recommendedOptions.value(QStringLiteral("subtype")).toString() == QLatin1String("lora"))
            offered.append(model);
    const auto same = std::equal(offered.cbegin(), offered.cend(), m_installed.cbegin(), m_installed.cend(),
        [](const auto& a, const auto& b) { return a.id == b.id && a.displayLabel() == b.displayLabel(); });
    if (same) return;
    m_installed = offered;
    const auto installed = [this](const QString& id) {
        return std::any_of(m_installed.cbegin(), m_installed.cend(), [&](const auto& model) { return model.id == id; });
    };
    bool dropped = false;
    for (int i = int(m_rows.size()) - 1; i >= 0; --i)
        if (!installed(m_rows.at(i).id)) {
            delete m_rows.at(i).widget;
            m_rows.removeAt(i);
            dropped = true;
        }
    refresh();
    if (dropped) emit choiceChanged();
}

void LoraPicker::setSupported(bool supported)
{
    if (m_supported == supported) return;
    m_supported = supported;
    refresh();
}

QString LoraPicker::issue() const
{
    if (!m_supported)
        return tr("The service cannot generate with LoRAs. Connect to a service that does, or update Nukleus.");
    if (m_installed.isEmpty())
        return tr("No LoRA is installed. Train one with Train a LoRA, or install one in Available Models.");
    return {};
}

QList<Nukleus::LoraWeight> LoraPicker::chosen() const
{
    QList<Nukleus::LoraWeight> chosen;
    for (const auto& row : m_rows) {
        Nukleus::LoraWeight lora;
        lora.modelId = row.id;
        lora.weight = row.weight->value();
        chosen.append(lora);
    }
    return chosen;
}

void LoraPicker::clear()
{
    if (m_rows.isEmpty()) return;
    for (const auto& row : m_rows) delete row.widget;
    m_rows.clear();
    refresh();
    emit choiceChanged();
}

void LoraPicker::add(const QString& id)
{
    if (m_rows.size() >= MaximumLoras) return;
    for (const auto& row : m_rows)
        if (row.id == id) return;
    QString label;
    for (const auto& model : m_installed)
        if (model.id == id) label = model.displayLabel();
    auto* widget = new QWidget(this);
    widget->setObjectName(QStringLiteral("loraRow"));
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* name = new QLabel(widget);
    name->setObjectName(QStringLiteral("loraLabel"));
    name->setText(name->fontMetrics().elidedText(label, Qt::ElideRight, 260));
    name->setToolTip(label);
    layout->addWidget(name, 1);
    auto* weight = new QDoubleSpinBox(widget);
    weight->setObjectName(QStringLiteral("loraWeight"));
    weight->setDecimals(2);
    weight->setRange(MinimumWeight, MaximumWeight);
    weight->setSingleStep(WeightStep);
    weight->setValue(DefaultWeight);
    weight->setAccessibleName(tr("Weight of %1").arg(label));
    connect(weight, &QDoubleSpinBox::valueChanged, this, &LoraPicker::choiceChanged);
    layout->addWidget(weight);
    auto* removeButton = new Ardoise::IconButton(Ardoise::Icon::Close, tr("Remove %1").arg(label), widget);
    removeButton->setObjectName(QStringLiteral("loraRemove"));
    removeButton->setToolTip(tr("Remove %1").arg(label));
    removeButton->setAccessibleName(tr("Remove %1").arg(label));
    connect(removeButton, &QAbstractButton::clicked, this, [this, id] { remove(id); });
    layout->addWidget(removeButton);
    m_rowsLayout->addWidget(widget);
    m_rows.append({id, label, widget, weight});
    refresh();
    emit choiceChanged();
}

void LoraPicker::remove(const QString& id)
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).id == id) {
            // Deleted later: the button that asked for it is still handling its click.
            m_rows.at(i).widget->hide();
            m_rows.at(i).widget->deleteLater();
            m_rows.removeAt(i);
            refresh();
            emit choiceChanged();
            return;
        }
}

void LoraPicker::refresh()
{
    const QString reason = issue();
    setEnabled(reason.isEmpty());
    setToolTip(reason);
    m_add->setToolTip(reason);
    m_menu->clear();
    for (const auto& model : m_installed) {
        const bool chosen = std::any_of(m_rows.cbegin(), m_rows.cend(), [&](const Row& row) { return row.id == model.id; });
        if (chosen) continue;
        const QString id = model.id;
        m_menu->addAction(model.displayLabel(), this, [this, id] { add(id); });
    }
    const bool full = m_rows.size() >= MaximumLoras;
    m_add->setEnabled(!full && !m_menu->isEmpty());
    if (reason.isEmpty() && full)
        m_add->setToolTip(tr("At most %n LoRA(s) can be chosen.", nullptr, MaximumLoras));
}
