#include "GenerationInspectorWidget.h"
#include "GenerationParametersWidget.h"
#include "PromptEditorWidget.h"
#include "ReferenceImageStrip.h"
#include "ShotPicker.h"
#include "StepHeader.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/IconProvider.h>
#include <Ardoise/PrimaryButton.h>
#include <Ardoise/Theme.h>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QVBoxLayout>

GenerationInspectorWidget::GenerationInspectorWidget(QWidget* parent)
    : QWidget(parent)
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    setMinimumWidth(metrics.inspectorMinimumWidth);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(metrics.spacing16, metrics.spacing4, metrics.spacing16, metrics.spacing16);
    layout->setSpacing(metrics.spacing12);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(scroll, 1);
    auto* content = new QWidget(scroll);
    m_content = content;
    content->installEventFilter(this);
    // The look and the prompts form two columns side by side in landscape, stacked otherwise.
    m_columns = new QBoxLayout(QBoxLayout::TopToBottom, content);
    m_columns->setContentsMargins(0, metrics.spacing8, 0, 0);
    m_columns->setSpacing(metrics.spacing20);
    auto* prompts = new QVBoxLayout;
    prompts->setSpacing(metrics.spacing8);
    auto* look = new QVBoxLayout;
    look->setSpacing(metrics.spacing8);
    m_columns->addLayout(look, 4);
    m_columns->addLayout(prompts, 5);
    scroll->setWidget(content);
    // A short guided path: pick the look, frame the shot, add references, then describe the image.
    const auto step = [&](QVBoxLayout* sections, int number, const QString& title, const QString& hint, QWidget* body,
                          QWidget* action = nullptr) {
        auto* header = StepHeader::create(number, title, hint, QStringLiteral("generationStep%1").arg(number), content);
        if (action)
            header->addWidget(action);
        if (sections->count() > 0)
            sections->addSpacing(metrics.spacing12);
        sections->addLayout(header);
        body->setSizePolicy(body->sizePolicy().horizontalPolicy(), QSizePolicy::Maximum);
        sections->addWidget(body);
    };
    m_positive = new PromptEditorWidget(content);
    m_positive->setLabel(tr("Positive prompt"));
    m_positive->setObjectName(QStringLiteral("positivePrompt"));
    m_positive->setPlaceholderText(tr("A castle on a cliff at sunset, dramatic sky, golden light…"));
    m_negative = new PromptEditorWidget(content);
    m_negative->setLabel(tr("Negative prompt"));
    m_negative->setObjectName(QStringLiteral("negativePrompt"));
    m_negative->setPlaceholderText(tr("Blurry, low quality, text, watermark…"));
    m_parameters = new GenerationParametersWidget(content);
    m_parameters->showGuidedControls();
    m_shot = new ShotPicker(content);
    m_shot->setObjectName(QStringLiteral("shotPicker"));
    // One discreet AI action for both prompts: make them more detailed and structured.
    m_assist = new Ardoise::IconButton(Ardoise::Icon::Sparkles, tr("Improve the prompts"), content);
    m_assist->setObjectName(QStringLiteral("promptAssistButton"));
    m_assist->setProperty("kiraFlat", true);
    m_assist->setFixedSize(metrics.controlHeightSmall, metrics.controlHeightSmall);
    m_assist->setEnabled(false);
    connect(m_assist, &QToolButton::clicked, this, &GenerationInspectorWidget::promptAssistRequested);
    auto* promptActions = new QWidget(content);
    auto* promptActionsLayout = new QHBoxLayout(promptActions);
    promptActionsLayout->setContentsMargins(0, 0, 0, 0);
    promptActionsLayout->setSpacing(metrics.spacing2);
    m_restore = new Ardoise::IconButton(Ardoise::Icon::Restore, tr("Restore last prompts"), promptActions);
    m_restore->setObjectName(QStringLiteral("restorePromptsButton"));
    m_restore->setProperty("kiraFlat", true);
    m_restore->setFixedSize(metrics.controlHeightSmall, metrics.controlHeightSmall);
    m_restore->setEnabled(false);
    promptActionsLayout->addWidget(m_restore);
    promptActionsLayout->addWidget(m_assist);
    connect(m_restore, &QToolButton::clicked, this, [this] {
        setPrompts(m_lastPositive, m_lastNegative);
    });

    // Reference images: the edit bubble's paperclip with the thumbnails beside it.
    auto* referenceRow = new QWidget(content);
    auto* referenceLayout = new QHBoxLayout(referenceRow);
    referenceLayout->setContentsMargins(0, 0, 0, 0);
    referenceLayout->setSpacing(metrics.spacing8);
    m_attach = new Ardoise::IconButton(Ardoise::Icon::Paperclip, tr("Add reference images"), referenceRow);
    m_attach->setObjectName(QStringLiteral("generationAttach"));
    m_attach->setFixedSize(ReferenceImageStrip::ThumbnailSize, ReferenceImageStrip::ThumbnailSize);
    referenceLayout->addWidget(m_attach);
    m_attachIdentity = new Ardoise::IconButton(Ardoise::Icon::Person, tr("Attach an identity"), referenceRow);
    m_attachIdentity->setObjectName(QStringLiteral("generationAttachIdentity"));
    m_attachIdentity->setFixedSize(m_attach->size());
    referenceLayout->addWidget(m_attachIdentity);
    m_references = new ReferenceImageStrip(QStringLiteral("generationRemoveReference"), referenceRow);
    m_references->setObjectName(QStringLiteral("generationReferences"));
    referenceLayout->addWidget(m_references, 1);
    referenceLayout->addStretch();
    connect(m_references, &ReferenceImageStrip::referencesChanged, this, [this] {
        updateReferenceAvailability();
        // Removing every reference detaches the identity's instruction too.
        if (m_references->references().isEmpty()) m_identityPrompt.clear();
        emit draftChanged();
    });
    step(look, 1, tr("Choose the look"), {}, m_parameters);
    step(look, 2, tr("Frame the shot"), tr("Optional"), m_shot);
    step(look, 3, tr("Reference images"), tr("Optional"), referenceRow);
    step(prompts, 4, tr("Describe your image"), {}, m_positive, promptActions);
    step(prompts, 5, tr("What to avoid"), tr("Optional"), m_negative);
    prompts->addStretch();
    m_prompts = prompts;
    look->addStretch();
    m_footer = new QHBoxLayout;
    m_footer->setSpacing(metrics.spacing12);
    const QKeySequence shortcut(Qt::CTRL | Qt::Key_Return);
    m_shortcutHint = new QLabel(tr("%1 to generate").arg(shortcut.toString(QKeySequence::NativeText)), this);
    m_shortcutHint->setObjectName(QStringLiteral("generateShortcutHint"));
    Ardoise::Theme::applyTypography(m_shortcutHint, Ardoise::TextRole::Caption);
    m_shortcutHint->setForegroundRole(QPalette::PlaceholderText);
    m_shortcutHint->hide();
    m_footer->addWidget(m_shortcutHint);
    m_footer->addStretch();
    m_generate = new Ardoise::PrimaryButton(tr("Generate"), this);
    m_generate->setIcon(Ardoise::IconProvider::icon(Ardoise::Icon::Wand));
    m_generate->setObjectName(QStringLiteral("generateButton"));
    m_generate->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setGenerationAvailable(false);
    m_footer->addWidget(m_generate, 1);
    layout->addLayout(m_footer);
    connect(m_generate, &QPushButton::clicked, this, &GenerationInspectorWidget::generationRequested);
    // Generate straight from the prompts without reaching for the mouse.
    for (const auto& keys : {shortcut, QKeySequence(Qt::CTRL | Qt::Key_Enter)}) {
        auto* generate = new QShortcut(keys, this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
        connect(generate, &QShortcut::activated, this, [this] {
            if (m_generate->isEnabled())
                m_generate->click();
        });
    }
    connect(m_positive, &PromptEditorWidget::textChanged, this, &GenerationInspectorWidget::draftChanged);
    connect(m_negative, &PromptEditorWidget::textChanged, this, &GenerationInspectorWidget::draftChanged);
    connect(m_parameters, &GenerationParametersWidget::parametersChanged, this,
        &GenerationInspectorWidget::updateReferenceAvailability);
    connect(m_parameters, &GenerationParametersWidget::parametersChanged, this,
        &GenerationInspectorWidget::draftChanged);
    updateReferenceAvailability();
}

bool GenerationInspectorWidget::referencesUsed() const
{
    const auto* preset = m_parameters->selectedPreset();
    return !preset || m_parameters->presetUsesReferences(preset->id);
}

void GenerationInspectorWidget::updateReferenceAvailability()
{
    const auto* preset = m_parameters->selectedPreset();
    const bool used = referencesUsed();
    const QString name = preset ? preset->displayLabel() : QString();
    m_references->setIgnored(used ? QString()
        : tr("%1 cannot use reference images, so they are ignored. Choose a preset whose model can edit, "
             "or remove them.").arg(name));
    const QString cannotAdd = tr("%1 cannot use reference images. Choose a preset whose model can edit to add "
                                 "some.").arg(name);
    m_attach->setEnabled(used && !m_references->isFull());
    m_attach->setToolTip(used ? tr("Add up to %n reference image(s) to guide the generation", nullptr,
                                    ReferenceImageStrip::MaximumReferences)
                              : cannotAdd);
    m_attachIdentity->setEnabled(used && !m_references->isFull());
    m_attachIdentity->setToolTip(used
            ? tr("Attach an identity: its reference images guide the character's face and hair")
            : cannotAdd);
}

QSize GenerationInspectorWidget::sizeHint() const
{
    // The natural height of the steps plus the Generate row, ignoring the scroll area.
    const auto margins = layout()->contentsMargins();
    return { QWidget::sizeHint().width(), m_content->sizeHint().height() + layout()->spacing()
            + m_footer->sizeHint().height() + margins.top() + margins.bottom() };
}

void GenerationInspectorWidget::setLandscape(bool landscape)
{
    if (isLandscape() == landscape)
        return;
    m_columns->setDirection(landscape ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    // Side by side, Generate settles under the prompts column with the shortcut beside it.
    m_shortcutHint->setVisible(landscape);
    m_footer->setStretchFactor(m_generate, landscape ? 0 : 1);
    m_generate->setSizePolicy(landscape ? QSizePolicy::Preferred : QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_generate->setMinimumWidth(landscape ? 260 : 0);
    // Side by side, the description takes the prompts column's spare height so the negative
    // prompt ends level with the reference buttons; stacked, both keep three lines.
    m_positive->setExpanding(landscape);
    m_prompts->setStretchFactor(m_positive, landscape ? 1 : 0);    updateGeometry();
}

bool GenerationInspectorWidget::isLandscape() const { return m_columns->direction() == QBoxLayout::LeftToRight; }

bool GenerationInspectorWidget::eventFilter(QObject* watched, QEvent* event)
{
    // Content inside the scroll area does not resize this widget; announce its new height.
    if (watched == m_content && event->type() == QEvent::LayoutRequest)
        updateGeometry();
    return QWidget::eventFilter(watched, event);
}

void GenerationInspectorWidget::clearPrompts()
{
    m_positive->setText({ });
    m_negative->setText({ });
}

void GenerationInspectorWidget::resetDraft()
{
    clearPrompts();
    m_references->clearReferences();
    m_identityPrompt.clear();
    m_shot->setCurrentShot(ShotPicker::Shot::Free);
    m_parameters->resetDefaults();
}

QString GenerationInspectorWidget::positivePrompt() const
{
    return m_positive->text();
}

Nukleus::ImageGenerationRequest GenerationInspectorWidget::generationRequest() const
{
    auto request = m_parameters->generationRequest();
    request.prompt = m_positive->text();
    request.negativePrompt = m_negative->text();
    return request;
}

void GenerationInspectorWidget::setGenerationAvailable(bool available, const QString& reason)
{
    m_generate->setEnabled(available);
    m_generate->setToolTip(available ? tr("Generate one image")
        : !reason.isEmpty() ? reason
        : tr("Generation requires an available service and model, with no translation, edit or upscale in progress."));
    m_generate->setAccessibleDescription(m_generate->toolTip());
}

void GenerationInspectorWidget::setPromptAssistAvailable(bool available, const QString& reason)
{
    const bool written = !m_positive->text().trimmed().isEmpty() || !m_negative->text().trimmed().isEmpty();
    m_assist->setEnabled(available && written);
    m_assist->setToolTip(!available && !reason.isEmpty() ? reason
        : !written ? tr("Write a prompt to improve it.")
        : tr("Improve the prompts: richer and better structured, in the same language, with details suited to the selected shot"));
}

void GenerationInspectorWidget::setPrompts(const QString& positive, const QString& negative)
{
    m_positive->setText(positive);
    m_negative->setText(negative);
}

void GenerationInspectorWidget::rememberSubmittedPrompts(const QString& positive, const QString& negative)
{
    m_lastPositive = positive;
    m_lastNegative = negative;
    m_restore->setEnabled(!positive.isEmpty() || !negative.isEmpty());
    clearPrompts();
}
