#include "IdentityPanel.h"
#include "CanvasServiceController.h"
#include "FloatingSurface.h"
#include "GenerationParametersWidget.h"
#include "IdentityPrompts.h"
#include "JobActivityIndicator.h"
#include "StepHeader.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/IconProvider.h>
#include <Ardoise/PlainTextEdit.h>
#include <Ardoise/PrimaryButton.h>
#include <Ardoise/Theme.h>
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QConicalGradient>
#include <QDesktopServices>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QShortcut>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>
#include <limits>
#include <utility>

namespace
{
int shadowMargin() { return Ardoise::Theme::tokens().metrics.spacing8; }
constexpr int ControlsWidth = 380;
constexpr int MinimumControlsWidth = 300;
// Distance the panel slides out from the toolbar while fading in.
constexpr int RevealTravel = 36;

// Identity portraits show natural traits only: drop comma-separated makeup, clothing and accessories.
QString withoutExcludedTraits(const QString& text)
{
    static const QRegularExpression excluded(QStringLiteral(
        "\\b(makeup|make-up|eyeshadow|eyeliner|lipstick|mascara|blush|foundation|maquillage|rouge à lèvres|fard|clothing|shirt|blouse|jacket|dress|suit|vêtement|chemise|robe|veste|accessor\\w*|jewel\\w*|earrings?|necklace|crown|tiara|glasses|lunettes|bijou\\w*|couronne)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    auto traits = text.split(',');
    traits.removeIf([](const QString& trait) { return excluded.match(trait).hasMatch(); });
    return traits.join(',').trimmed();
}
}

// One portrait view: an empty well naming the view, an accent sweep around its
// border while generating, and the portrait fading in, fitted, once delivered.
class PortraitSlot final : public QWidget
{
public:
    explicit PortraitSlot(QWidget* parent)
        : QWidget(parent)
        , m_fade(new QVariantAnimation(this))
        , m_sweep(new QVariantAnimation(this))
    {
        setAttribute(Qt::WA_Hover);
        m_fade->setDuration(420);
        m_fade->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_fadeValue = value.toReal();
            update();
        });
        m_sweep->setStartValue(0.0);
        m_sweep->setEndValue(360.0);
        m_sweep->setDuration(1600);
        m_sweep->setLoopCount(-1);
        connect(m_sweep, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_angle = value.toReal();
            update();
        });
    }
    void setLabel(const QString& label)
    {
        m_label = label;
        setAccessibleName(label);
        update();
    }
    QImage image() const { return m_image; }
    void setImage(const QImage& image)
    {
        m_image = image;
        m_fade->stop();
        m_fadeValue = 0;
        if (!image.isNull() && isVisible()) {
            m_fade->setStartValue(0.0);
            m_fade->setEndValue(1.0);
            m_fade->start();
        } else {
            m_fadeValue = 1;
        }
        update();
    }
    void setBusy(bool busy)
    {
        if (busy == (m_sweep->state() == QAbstractAnimation::Running))
            return;
        busy ? m_sweep->start() : m_sweep->stop();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& tokens = Ardoise::Theme::tokens();
        const auto& colors = tokens.colors;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF frame = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        const qreal radius = tokens.metrics.radiusLarge;
        QPainterPath clip;
        clip.addRoundedRect(frame, radius, radius);
        painter.fillPath(clip, colors.inputBackground);
        const bool busy = m_sweep->state() == QAbstractAnimation::Running;
        if (m_image.isNull() || m_fadeValue < 1) {
            // Placeholder: a faint silhouette above the view name.
            painter.setOpacity(m_image.isNull() ? 1.0 : 1.0 - m_fadeValue);
            const int icon = tokens.metrics.iconSizeLarge;
            const QPointF centre = frame.center();
            Ardoise::IconProvider::icon(Ardoise::Icon::Person, busy ? Ardoise::IconTone::Accent : Ardoise::IconTone::Muted)
                .paint(&painter, QRect(qRound(centre.x() - icon / 2.0), qRound(centre.y() - icon), icon, icon));
            painter.setFont(Ardoise::Theme::font(Ardoise::TextRole::Caption));
            painter.setPen(busy ? colors.textSecondary : colors.textMuted);
            painter.drawText(QRectF(frame.left(), centre.y() + tokens.metrics.spacing6, frame.width(),
                                 painter.fontMetrics().height()),
                Qt::AlignHCenter | Qt::AlignTop, m_label);
            painter.setOpacity(1);
        }
        if (!m_image.isNull()) {
            painter.save();
            painter.setClipPath(clip);
            painter.setOpacity(m_fadeValue);
            const QSizeF fitted = QSizeF(m_image.size()).scaled(frame.size(), Qt::KeepAspectRatio);
            const QRectF target(frame.center().x() - fitted.width() / 2, frame.center().y() - fitted.height() / 2,
                fitted.width(), fitted.height());
            painter.drawImage(target, m_image);
            // View name on a translucent chip in the lower-left corner.
            const QFont font = Ardoise::Theme::font(Ardoise::TextRole::Caption);
            painter.setFont(font);
            const int pad = tokens.metrics.spacing6;
            const QRectF chip(frame.left() + tokens.metrics.spacing8,
                frame.bottom() - tokens.metrics.spacing8 - painter.fontMetrics().height() - pad,
                painter.fontMetrics().horizontalAdvance(m_label) + 2 * pad, painter.fontMetrics().height() + pad);
            QColor chipFill = colors.backgroundPrimary;
            chipFill.setAlpha(190);
            painter.setPen(Qt::NoPen);
            painter.setBrush(chipFill);
            painter.drawRoundedRect(chip, chip.height() / 2, chip.height() / 2);
            painter.setPen(colors.textPrimary);
            painter.drawText(chip, Qt::AlignCenter, m_label);
            painter.restore();
        }
        if (busy) {
            // A bright arc sweeps around the border, trailing into the accent.
            QConicalGradient sweep(frame.center(), -m_angle);
            QColor clear = colors.accent;
            clear.setAlpha(0);
            sweep.setColorAt(0, colors.borderAccent);
            sweep.setColorAt(0.18, colors.accent);
            sweep.setColorAt(0.45, clear);
            sweep.setColorAt(1, clear);
            QColor glow = colors.accent;
            glow.setAlpha(50);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(glow, 3));
            painter.drawRoundedRect(frame, radius, radius);
            painter.setPen(QPen(QBrush(sweep), 2));
            painter.drawRoundedRect(frame, radius, radius);
        } else {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(underMouse() && !m_image.isNull() ? colors.borderHover : colors.borderSubtle, 1));
            painter.drawRoundedRect(frame, radius, radius);
        }
    }

private:
    QImage m_image;
    QString m_label;
    QVariantAnimation* m_fade;
    QVariantAnimation* m_sweep;
    qreal m_fadeValue = 1;
    qreal m_angle = 0;
};

IdentityPanel::IdentityPanel(CanvasServiceController* service, QWidget* parent)
    : QWidget(parent)
    , m_service(service)
    , m_reveal(new QVariantAnimation(this))
    , m_opacity(new QGraphicsOpacityEffect(this))
    , m_slotMotion(new QVariantAnimation(this))
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    setObjectName(QStringLiteral("identityPanel"));
    setAttribute(Qt::WA_NoSystemBackground);
    m_opacity->setOpacity(0);
    setGraphicsEffect(m_opacity);
    hide();

    m_panel = new QWidget(this);
    auto* layout = new QVBoxLayout(m_panel);
    layout->setContentsMargins(metrics.spacing20, metrics.spacing12, metrics.spacing20, metrics.spacing16);
    layout->setSpacing(metrics.spacing12);
    auto* header = new QHBoxLayout;
    m_header = header;
    header->setSpacing(metrics.spacing8);
    auto* icon = new QLabel(m_panel);
    icon->setPixmap(Ardoise::IconProvider::icon(Ardoise::Icon::Person, Ardoise::IconTone::Accent)
            .pixmap(metrics.iconSize, metrics.iconSize));
    header->addWidget(icon);
    auto* title = new QLabel(tr("New identity"), m_panel);
    title->setObjectName(QStringLiteral("identityPanelTitle"));
    Ardoise::Theme::applyTypography(title, Ardoise::TextRole::SectionTitle);
    header->addWidget(title);
    auto* subtitle = new QLabel(tr("Consistent reference portraits of one person"), m_panel);
    Ardoise::Theme::applyTypography(subtitle, Ardoise::TextRole::Caption);
    subtitle->setForegroundRole(QPalette::PlaceholderText);
    header->addSpacing(metrics.spacing4);
    header->addWidget(subtitle, 0, Qt::AlignBaseline);
    header->addStretch();
    auto* close = new Ardoise::IconButton(Ardoise::Icon::Close, tr("Close"), m_panel);
    close->setObjectName(QStringLiteral("identityPanelClose"));
    close->setProperty("kiraFlat", true);
    close->setFixedSize(metrics.controlHeightSmall, metrics.controlHeightSmall);
    connect(close, &QToolButton::clicked, this, [this] { setOpen(false); });
    header->addWidget(close);
    layout->addLayout(header);

    auto* body = new QHBoxLayout;
    body->setSpacing(metrics.spacing24);
    layout->addLayout(body, 1);
    auto* controls = new QWidget(m_panel);
    m_controls = controls;
    controls->setFixedWidth(ControlsWidth);
    auto* steps = new QVBoxLayout(controls);
    steps->setContentsMargins(0, 0, 0, 0);
    steps->setSpacing(metrics.spacing8);
    body->addWidget(controls);

    // 1. The style: the preset also sets how many views to generate.
    m_parameters = new GenerationParametersWidget(controls);
    m_parameters->showGuidedControls(false);
    m_parameters->setModels(service->generationModels());
    m_parameters->setRouting(service->modelRouting(), service->supportsModelRouting());
    steps->addLayout(StepHeader::create(1, tr("Choose the style"), {}, QStringLiteral("identityStep1"), controls));
    steps->addWidget(m_parameters);

    // 2. The face, and 3. the hair, kept apart so each can be refined independently.
    const auto editor = [&](const QString& name, const QString& accessibleName, const QString& placeholder) {
        auto* text = new Ardoise::PlainTextEdit(controls);
        text->setReferenceLength(2000);
        text->setCounterVisible(true);
        text->setObjectName(name);
        text->setAccessibleName(accessibleName);
        text->setPlaceholderText(placeholder);
        text->setMinimumHeight(text->heightForLines(2));
        return text;
    };
    m_prompt = editor(QStringLiteral("identityPrompt"), tr("Face identity"),
        tr("An adult woman in her thirties, olive skin, oval face, almond-shaped brown eyes, thick eyebrows…"));
    m_hairPrompt = editor(QStringLiteral("identityHairPrompt"), tr("Hair"),
        tr("Shoulder-length chestnut hair, loose waves, side part…"));
    steps->addSpacing(metrics.spacing12);
    auto* faceHeader = StepHeader::create(2, tr("Describe the face"), {}, QStringLiteral("identityStep2"), controls);
    // Improves both descriptions, like the generation card's Sparkles.
    m_improve = new Ardoise::IconButton(Ardoise::Icon::Sparkles, tr("Improve the descriptions"), controls);
    m_improve->setObjectName(QStringLiteral("identityImprove"));
    m_improve->setProperty("kiraFlat", true);
    m_improve->setFixedSize(metrics.controlHeightSmall, metrics.controlHeightSmall);
    m_improve->setEnabled(false);
    connect(m_improve, &QToolButton::clicked, this,
        [this] { emit improveRequested(m_prompt->toPlainText(), m_hairPrompt->toPlainText()); });
    faceHeader->addWidget(m_improve);
    steps->addLayout(faceHeader);
    steps->addWidget(m_prompt, 3);
    steps->addSpacing(metrics.spacing12);
    steps->addLayout(StepHeader::create(3, tr("Describe the hair"), tr("Optional"), QStringLiteral("identityStep3"),
        controls));
    steps->addWidget(m_hairPrompt, 2);

    m_portraitArea = new QWidget(m_panel);
    m_portraitArea->setObjectName(QStringLiteral("identityPortraits"));
    m_portraitArea->installEventFilter(this);
    body->addWidget(m_portraitArea, 1);
    for (int i = 0; i < 4; ++i) {
        auto* slot = new PortraitSlot(m_portraitArea);
        slot->setObjectName(QStringLiteral("identityPortrait%1").arg(i));
        slot->installEventFilter(this);
        slot->hide();
        m_slots.append(slot);
    }
    m_activity = new JobActivityIndicator(m_portraitArea);
    m_slotMotion->setDuration(320);
    m_slotMotion->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_slotMotion, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        const qreal t = value.toReal();
        for (int i = 0; i < m_slotTarget.size(); ++i) {
            const QRect& from = m_slotStart[i];
            const QRect& to = m_slotTarget[i];
            m_slots[i]->setGeometry(QRect(qRound(from.x() + (to.x() - from.x()) * t), qRound(from.y() + (to.y() - from.y()) * t),
                qRound(from.width() + (to.width() - from.width()) * t),
                qRound(from.height() + (to.height() - from.height()) * t)));
        }
    });

    auto* footer = new QHBoxLayout;
    m_footer = footer;
    footer->setSpacing(metrics.spacing12);
    const QKeySequence shortcut(Qt::CTRL | Qt::Key_Return);
    auto* hint = new QLabel(tr("%1 to generate").arg(shortcut.toString(QKeySequence::NativeText)), m_panel);
    Ardoise::Theme::applyTypography(hint, Ardoise::TextRole::Caption);
    hint->setForegroundRole(QPalette::PlaceholderText);
    footer->addWidget(hint);
    footer->addStretch();
    m_save = new QPushButton(Ardoise::IconProvider::icon(Ardoise::Icon::Save), tr("Save identity…"), m_panel);
    m_save->setObjectName(QStringLiteral("savePromptIdentity"));
    footer->addWidget(m_save);
    m_generate = new Ardoise::PrimaryButton(Ardoise::IconProvider::icon(Ardoise::Icon::Person), tr("Generate portraits"),
        m_panel);
    m_generate->setObjectName(QStringLiteral("generateIdentityPortrait"));
    m_generate->setMinimumWidth(220);
    footer->addWidget(m_generate);
    layout->addLayout(footer);

    for (const auto& keys : {shortcut, QKeySequence(Qt::CTRL | Qt::Key_Enter)}) {
        auto* generate = new QShortcut(keys, m_panel, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
        connect(generate, &QShortcut::activated, this, [this] {
            if (m_generate->isEnabled())
                m_generate->click();
        });
    }
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), m_panel, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { setOpen(false); });

    connect(m_reveal, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_revealValue = value.toReal();
        m_opacity->setOpacity(m_revealValue);
        relayout();
    });
    connect(m_reveal, &QVariantAnimation::finished, this, [this] {
        if (!m_open)
            hide();
        // Render crisply once fully revealed; the effect is only needed while fading.
        m_opacity->setEnabled(m_revealValue < 1);
    });
    connect(qApp, &QApplication::focusChanged, this, [this] { update(); });
    m_panel->installEventFilter(this);

    connect(m_generate, &QPushButton::clicked, this, &IdentityPanel::generate);
    connect(m_save, &QPushButton::clicked, this, [this] { emit identityRequested(portraitIdentity()); });
    connect(m_prompt, &QPlainTextEdit::textChanged, this, &IdentityPanel::updateAvailability);
    connect(m_hairPrompt, &QPlainTextEdit::textChanged, this, &IdentityPanel::updateAvailability);
    connect(m_parameters, &GenerationParametersWidget::parametersChanged, this, [this] {
        relayout(true); // The preset may change the view count and the mapped model the portrait ratio.
        updateAvailability();
    });
    connect(service, &CanvasServiceController::generationModelsChanged, this, [this] {
        m_parameters->setModels(m_service->generationModels());
        m_parameters->setRouting(m_service->modelRouting(), m_service->supportsModelRouting());
        updateAvailability();
    });
    connect(service, &CanvasServiceController::modelRoutingChanged, this, [this] {
        m_parameters->setRouting(m_service->modelRouting(), m_service->supportsModelRouting());
        relayout(true); // The preset's view count may have changed.
        updateAvailability();
    });
    connect(service, &CanvasServiceController::modelsChanged, this, &IdentityPanel::updateAvailability);
    connect(service, &CanvasServiceController::generationAvailabilityChanged, this, &IdentityPanel::updateAvailability);
    updatePortraitGeometry(false);
    updateAvailability();
}

void IdentityPanel::setAnchors(QWidget* toolbar, QWidget* area)
{
    m_toolbar = toolbar;
    m_area = area;
    toolbar->installEventFilter(this);
    area->installEventFilter(this);
    relayout();
}

void IdentityPanel::relayout(bool animateSlots)
{
    if (!m_toolbar || !m_area) {
        updatePortraitGeometry(animateSlots);
        return;
    }
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    const int margin = shadowMargin();
    const QRect toolbar = m_toolbar->geometry();
    const int left = toolbar.right() + 1 + metrics.spacing8;
    // Size the panel so the portrait grid spans exactly the height of the steps: its
    // edges then line up with the first step and the hair field.
    const auto* layout = m_panel->layout();
    const auto margins = layout->contentsMargins();
    const int bodyHeight = toolbar.height() - margins.top() - margins.bottom() - m_header->sizeHint().height()
        - m_footer->sizeHint().height() - 2 * layout->spacing();
    const int count = displayedViewCount();
    const int gap = metrics.spacing12;
    const int available = m_area->geometry().right() + 1 - left;
    const int chrome = margins.left() + metrics.spacing24 + margins.right();
    const auto gridWidth = [&](const QSize& grid) {
        const int cellHeight = qMax(0, (bodyHeight - (grid.height() - 1) * gap) / grid.height());
        return grid.width() * qRound(cellHeight * portraitAspect()) + (grid.width() - 1) * gap;
    };
    // Two views sit side by side when they fit beside the narrowest controls, stacked otherwise.
    m_grid = count == 1 ? QSize(1, 1) : count == 4 ? QSize(2, 2) : QSize(2, 1);
    if (count == 2 && chrome + MinimumControlsWidth + gridWidth(m_grid) > available)
        m_grid = QSize(1, 2);
    // Narrow the controls before the grid would have to shrink below the steps' height.
    const int controls = qBound(MinimumControlsWidth, available - chrome - gridWidth(m_grid), ControlsWidth);
    m_controls->setFixedWidth(controls);
    const int width = qMax(0, qMin(available, chrome + controls + gridWidth(m_grid)));
    // Slides out from the toolbar's edge while revealing.
    const QRect shape(left - qRound((1 - m_revealValue) * RevealTravel), toolbar.top(), width, toolbar.height());
    m_inRelayout = true;
    setGeometry(shape.adjusted(-margin, -margin, margin, margin));
    m_panel->setGeometry(margin, margin, shape.width(), shape.height());
    m_panel->layout()->activate(); // Apply the controls width now so the slots measure the final area.
    m_inRelayout = false;
    updatePortraitGeometry(animateSlots);
    update();
}

qreal IdentityPanel::portraitAspect() const
{
    // Delivered portraits keep their own ratio; otherwise use the planned portrait size.
    QSize size = m_slots.first()->image().size();
    if (size.isEmpty()) {
        const auto request = m_parameters->generationRequest();
        size = QSize(qMin(request.width, request.height), qMax(request.width, request.height));
    }
    return size.isEmpty() ? 3.0 / 4.0 : qreal(size.width()) / size.height();
}

int IdentityPanel::displayedViewCount() const
{
    // A delivered set keeps its views until the next set delivers; otherwise preview the selection.
    const bool delivered = m_portraitViewCount > 0 && !m_slots.first()->image().isNull();
    return delivered ? m_portraitViewCount : selectedViewCount();
}

void IdentityPanel::setOpen(bool open)
{
    if (m_open == open)
        return;
    m_open = open;
    if (open) {
        show();
        raise();
        relayout();
        animateReveal();
        if (m_prompt->isEnabled())
            m_prompt->setFocus(Qt::OtherFocusReason);
    } else {
        if (isAncestorOf(QApplication::focusWidget()) && parentWidget())
            parentWidget()->setFocus(Qt::OtherFocusReason);
        if (isVisible())
            animateReveal();
        else
            hide();
    }
    emit openChanged(open);
}

void IdentityPanel::animateReveal()
{
    m_reveal->stop();
    m_opacity->setEnabled(true);
    m_reveal->setDuration(m_open ? 440 : 220);
    m_reveal->setEasingCurve(m_open ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    m_reveal->setStartValue(m_revealValue);
    m_reveal->setEndValue(m_open ? 1.0 : 0.0);
    m_reveal->start();
}

bool IdentityPanel::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == m_toolbar || watched == m_area) && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        relayout();
    else if (watched == m_portraitArea && event->type() == QEvent::Resize && !m_inRelayout)
        updatePortraitGeometry(false);
    else if (event->type() == QEvent::MouseButtonDblClick
        && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        for (auto* slot : m_slots)
            if (watched == slot) {
                if (!slot->image().isNull())
                    openExternally(slot->image());
                return true;
            }
    }
    return QWidget::eventFilter(watched, event);
}

void IdentityPanel::openExternally(const QImage& image)
{
    const auto path = m_externalImages.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png");
    if (!m_externalImages.isValid() || !image.save(path, "PNG")
        || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        QMessageBox::warning(this, tr("Could not open image"),
            tr("Could not open the image in the default image application."));
}

void IdentityPanel::paintEvent(QPaintEvent*)
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    const int margin = shadowMargin();
    QPainter painter(this);
    const QRectF shape = QRectF(rect()).adjusted(margin + 0.5, margin + 0.5, -margin - 0.5, -margin - 0.5);
    FloatingSurface::paint(painter, shape, metrics.radiusLarge + metrics.spacing4, margin,
        isAncestorOf(QApplication::focusWidget()), 250);
}

// The panel floats over the workspace; keep clicks and scrolling from reaching it.
void IdentityPanel::mousePressEvent(QMouseEvent* event) { event->accept(); }

void IdentityPanel::wheelEvent(QWheelEvent* event) { event->accept(); }

int IdentityPanel::selectedViewCount() const
{
    // The service-owned preset (validated to 1, 2 or 4) decides how many views its
    // model can render consistently.
    const auto* preset = m_parameters->selectedPreset();
    return preset ? preset->identityViews : 1;
}

void IdentityPanel::updatePortraitGeometry(bool animated)
{
    const int count = displayedViewCount();
    const QStringList labels = count == 4
        ? QStringList{tr("Front"), tr("Three-quarter left"), tr("Three-quarter right"), tr("Profile")}
        : QStringList{tr("Front"), tr("Profile")};
    const QRect area = m_portraitArea->rect();
    const int gap = Ardoise::Theme::tokens().metrics.spacing12;
    const int columns = m_toolbar ? m_grid.width() : count == 1 ? 1 : 2;
    const int rows = m_toolbar ? m_grid.height() : count == 4 ? 2 : 1;
    // Slots take the portraits' aspect ratio, filling the height unless the width is short.
    const qreal aspect = portraitAspect();
    int cellHeight = qMax(0, (area.height() - gap * (rows - 1)) / rows);
    int cellWidth = qRound(cellHeight * aspect);
    if (columns * cellWidth + (columns - 1) * gap > area.width()) {
        cellWidth = qMax(0, (area.width() - gap * (columns - 1)) / columns);
        cellHeight = qRound(cellWidth / aspect);
    }
    const QPoint origin(area.center().x() + 1 - (columns * cellWidth + (columns - 1) * gap) / 2,
        area.center().y() + 1 - (rows * cellHeight + (rows - 1) * gap) / 2);
    m_slotMotion->stop();
    m_slotStart.clear();
    m_slotTarget.clear();
    for (int i = 0; i < m_slots.size(); ++i) {
        auto* slot = m_slots[i];
        const bool visible = i < count;
        const QRect target(origin.x() + (i % columns) * (cellWidth + gap), origin.y() + (i / columns) * (cellHeight + gap),
            cellWidth, cellHeight);
        slot->setLabel(labels.value(i));
        slot->setToolTip(slot->image().isNull() ? QString() : tr("Double-click to open in the default image application"));
        // New slots grow from their centre; existing ones glide to their place.
        m_slotStart.append(!visible ? target
            : slot->isHidden() ? QRect(target.center(), QSize(1, 1)) : slot->geometry());
        m_slotTarget.append(target);
        slot->setVisible(visible);
        if (!animated || !isVisible())
            slot->setGeometry(target);
    }
    if (animated && isVisible()) {
        m_slotMotion->setStartValue(0.0);
        m_slotMotion->setEndValue(1.0);
        m_slotMotion->start();
    }
    const bool pending = m_awaitingTranslation || m_awaitingPortrait;
    for (int i = 0; i < m_slots.size(); ++i)
        m_slots[i]->setBusy(pending && i == m_portraitIndex);
    m_activity->raise();
}

void IdentityPanel::generate()
{
    auto request = m_parameters->generationRequest();
    // Retain the model's native size and pixel budget, orienting landscapes
    // vertically for a head-and-shoulders portrait. Square models stay square.
    if (request.width > request.height)
        std::swap(request.width, request.height);
    request.seed = QRandomGenerator::global()->bounded(std::numeric_limits<int>::max());
    m_pendingIdentityPrompt = withoutExcludedTraits(m_prompt->toPlainText());
    m_pendingHairPrompt = withoutExcludedTraits(m_hairPrompt->toPlainText());
    const auto description = Nukleus::Protocol::composeStyledPrompt(m_pendingIdentityPrompt, m_pendingHairPrompt);
    m_pendingRequest = request;
    m_portraitIndex = 0;
    // Like image creation, the description is translated into English once; every view
    // then reuses the translation so the person stays the same.
    if (const auto english = m_service->immediateTranslation({description})) {
        requestPortraits(english->first());
        return;
    }
    emit translationRequested(description);
}

void IdentityPanel::setDescriptions(const QString& face, const QString& hair)
{
    m_prompt->setPlainText(face);
    m_hairPrompt->setPlainText(hair);
}

void IdentityPanel::setAwaitingTranslation(bool pending)
{
    m_awaitingTranslation = pending;
    updatePortraitGeometry(false);
    updateAvailability();
}

void IdentityPanel::receiveTranslation(bool succeeded, const QString& description)
{
    if (!m_awaitingTranslation)
        return;
    m_awaitingTranslation = false;
    // The translator may reintroduce excluded traits in its own words.
    const auto english = withoutExcludedTraits(description);
    if (!succeeded || english.isEmpty()) {
        updatePortraitGeometry(false);
        updateAvailability();
        return;
    }
    requestPortraits(english);
}

void IdentityPanel::requestPortraits(const QString& description)
{
    auto request = m_pendingRequest;
    const auto prefix = QStringLiteral(
        "one person, adult, nude, bare shoulders, front-facing head-and-shoulders portrait, medium shot from upper chest to above the hair, full hairstyle and both shoulders included with ample surrounding space, eye-level viewpoint directly in front of the face, straight-on frontal view, head upright and facing forward, head and torso aligned, level eyes and shoulders, face centered and symmetrical in the frame, looking directly at the viewer, entire head including hair and chin visible, top of the head and all hair fully inside the frame, clear empty background space above the highest hair, both shoulders and upper chest visible, generous plain background margin above the head and on both sides, clearly visible face outline, neutral expression, closed mouth, soft even lighting, plain background");
    request.prompt = Nukleus::Protocol::composeStyledPrompt(prefix, description);
    request.negativePrompt = Nukleus::Protocol::composeStyledPrompt(QStringLiteral(
        "head tilt, tilted viewpoint, side view, profile, three-quarter view, looking away, extreme close-up, tight framing, cropped head, cropped hair, cropped crown, hair touching top edge, cropped shoulders, cropped chin, face out of frame, makeup, eyeshadow, eyeliner, lipstick, mascara, blush, foundation, clothing, shirts, jackets, dresses, accessories, jewelry, earrings, necklaces, chokers, piercings, headwear, hats, crowns, tiaras, headdresses, hair ornaments, hair accessories, glasses, sunglasses, masks, face coverings, face straps, tattoos, facial tattoos, body tattoos, face markings, decorative skin markings, face paint, body paint, forehead symbols, forehead gems, facial gems, stickers, horns, antlers, camera, handheld camera, photography equipment, handheld props, camera overlay, viewfinder overlay, recording indicator, REC, recording frame, focus brackets, battery indicator, timestamp, HUD, user interface, border, picture frame, text, watermark, signature, logo"), request.negativePrompt);
    request.negativePrompt += QStringLiteral(", hands, fingers, finger to mouth, hand on own face");
    m_portraitRequests = {request};
    const auto framing = QStringLiteral(
        "one person, adult, nude, bare shoulders, upper body portrait from upper chest to above the highest hair, entire head and hairstyle inside the frame, clear background margins above and beside the head, head upright, level shoulders, neutral expression, closed mouth, soft even lighting, simple background");
    const QString orientations[] = {
        QStringLiteral("three-quarter view facing left, nose pointing toward the left edge of the image, gaze directed toward the left edge of the image, face and torso together at 45 degrees toward image-left, eyes looking diagonally toward image-left in the same direction as the nose, pupils centered in the turned eyes, nose tip to the left of the face center, near cheek broad and far cheek narrow, both eyes visible with the far eye foreshortened, eye-level viewpoint, both shoulders visible at an oblique angle"),
        QStringLiteral("three-quarter view facing right, nose pointing toward the right edge of the image, gaze directed toward the right edge of the image, face and torso together at 45 degrees toward image-right, eyes looking diagonally toward image-right in the same direction as the nose, pupils centered in the turned eyes, nose tip to the right of the face center, near cheek broad and far cheek narrow, both eyes visible with the far eye foreshortened, eye-level viewpoint, both shoulders visible at an oblique angle"),
        QStringLiteral("profile, from side, viewpoint exactly 90 degrees from the front, strict left-facing side profile of both head and torso, nose pointing toward the left edge of the image, gaze directed toward the left edge of the image, one visible eye, far eye and far cheek occluded, nose and lips outlined against the background, only the near shoulder visible, eye-level viewpoint")
    };
    const int viewCount = selectedViewCount();
    for (int i = 0; i < 3; ++i) {
        if (viewCount == 1 || (viewCount == 2 && i != 2)) continue;
        auto viewRequest = request;
        viewRequest.prompt = Nukleus::Protocol::composeStyledPrompt(orientations[i],
            Nukleus::Protocol::composeStyledPrompt(framing, description));
        viewRequest.negativePrompt.replace(QStringLiteral("side view, profile, three-quarter view, looking away"),
            i == 2 ? QStringLiteral("front view, frontal view, frontal torso, three-quarter view, both eyes visible, both shoulders visible, head turned independently of torso, looking at viewer")
                   : QStringLiteral("front view, frontal face, frontal torso, symmetrical face, full profile, looking at viewer, eyes turned toward the viewer, sideways glance back at viewer, head turned independently of torso"));
        viewRequest.negativePrompt = Nukleus::Protocol::composeStyledPrompt(
            i == 1 ? QStringLiteral("facing left, nose pointing left") : QStringLiteral("facing right, nose pointing right"),
            viewRequest.negativePrompt);
        viewRequest.negativePrompt = Nukleus::Protocol::composeStyledPrompt(
            QStringLiteral("looking back, over shoulder, tilted head"), viewRequest.negativePrompt);
        m_portraitRequests.append(viewRequest);
    }
    m_portraitIndex = 0;
    emit portraitRequested(m_portraitRequests.first(), 0, int(m_portraitRequests.size()));
}

bool IdentityPanel::receivePortrait(const QImage& image)
{
    if (m_portraitIndex == 0) {
        m_portraitViewCount = int(m_portraitRequests.size());
        m_portraitIdentityPrompt = m_pendingIdentityPrompt;
        m_portraitHairPrompt = m_pendingHairPrompt;
        m_portraitSeed = m_portraitRequests.first().seed;
        for (int i = 1; i < m_slots.size(); ++i) m_slots[i]->setImage({});
        setPortrait(image);
    } else {
        m_slots[m_portraitIndex]->setImage(image);
    }
    ++m_portraitIndex;
    if (m_portraitIndex < m_portraitRequests.size()) {
        updatePortraitGeometry(false);
        emit portraitRequested(m_portraitRequests[m_portraitIndex], m_portraitIndex, int(m_portraitRequests.size()));
        return m_awaitingPortrait;
    }
    m_portraitIndex = 0;
    updatePortraitGeometry(false);
    return false;
}

QImage IdentityPanel::portrait(int index) const { return m_slots.value(index) ? m_slots[index]->image() : QImage(); }

void IdentityPanel::updateAvailability()
{
    const bool pending = m_awaitingTranslation || m_awaitingPortrait;
    const bool described = !m_prompt->toPlainText().trimmed().isEmpty();
    const bool routed = m_parameters->routingAvailable() && m_service->canGenerate(m_parameters->modelId());
    m_generate->setEnabled(!pending && described && routed);
    m_generate->setToolTip(pending ? tr("Wait for the current portraits to finish.")
        : !m_parameters->routingAvailable() ? m_parameters->routingIssue()
        : !routed ? tr("Generation requires an available service and model, with no other job running.")
        : !described ? tr("Describe the face to generate portraits.")
        : tr("Generate the selected portrait views"));
    m_generate->setAccessibleDescription(m_generate->toolTip());
    const QString improveIssue = m_service->enhancementIssue();
    const bool written = described || !m_hairPrompt->toPlainText().trimmed().isEmpty();
    m_improve->setEnabled(!pending && improveIssue.isEmpty() && written);
    m_improve->setToolTip(pending ? tr("Wait for the current portraits to finish.")
        : !improveIssue.isEmpty() ? improveIssue
        : !written ? tr("Describe the face or hair to improve the descriptions.")
        : tr("Improve the descriptions: more detailed and structured, in the same language, without inventing anything"));
    m_generate->setText(m_portraitViewCount > 0 ? tr("Regenerate portraits") : tr("Generate portraits"));
    m_parameters->setRoutingLocked(pending || m_service->jobActive());
    const bool complete = m_portraitViewCount > 0
        && std::all_of(m_slots.cbegin(), m_slots.cbegin() + m_portraitViewCount,
            [](const PortraitSlot* slot) { return !slot->image().isNull(); });
    m_save->setEnabled(!pending && complete);
    m_save->setToolTip(complete ? tr("Export the portraits and descriptions as a Kira identity")
                                : tr("Generate a complete set of portraits to save the identity."));
    m_prompt->setEnabled(!pending);
    m_hairPrompt->setEnabled(!pending);
}

void IdentityPanel::setAwaitingPortrait(bool pending)
{
    m_awaitingPortrait = pending;
    updatePortraitGeometry(false);
    updateAvailability();
}

void IdentityPanel::setPortrait(const QImage& image)
{
    if (m_portraitViewCount == 0)
        m_portraitViewCount = selectedViewCount();
    m_slots.first()->setImage(image);
    relayout(true);
    updateAvailability();
}

Nukleus::IdentityPackage IdentityPanel::portraitIdentity() const
{
    Nukleus::IdentityPackage identity;
    identity.identityPrompt = m_portraitIdentityPrompt;
    identity.hairPrompt = m_portraitHairPrompt;
    identity.generationPrompt = QString::fromLatin1(IdentityPrompts::DefaultGenerationPrompt);
    identity.editPrompt = QString::fromLatin1(IdentityPrompts::DefaultEditPrompt);
    identity.seed = m_portraitSeed;
    for (int i = 0; i < m_portraitViewCount; ++i) {
        QByteArray png;
        QBuffer buffer(&png);
        const auto image = m_slots[i]->image();
        if (image.isNull() || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
        identity.references.append(png);
    }
    return identity;
}
