#include "EditBubble.h"
#include "FloatingSurface.h"
#include "QualityStepSlider.h"
#include "ReferenceImageStrip.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/IconProvider.h>
#include <Ardoise/PlainTextEdit.h>
#include <Ardoise/PrimaryButton.h>
#include <Ardoise/Theme.h>
#include <QAbstractScrollArea>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace
{
// Room around the bubble's shape for its painted shadow.
int shadowMargin() { return Ardoise::Theme::tokens().metrics.spacing8; }
// Distance the bubble travels while sliding in from below.
constexpr int RevealTravel = 28;
constexpr int MaximumWidth = 680;
}

EditBubble::EditBubble(QAbstractScrollArea* host)
    : QWidget(host)
    , m_host(host)
    , m_reveal(new QVariantAnimation(this))
    , m_morph(new QVariantAnimation(this))
    , m_opacity(new QGraphicsOpacityEffect(this))
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    setObjectName(QStringLiteral("imageEditRow"));
    setAttribute(Qt::WA_NoSystemBackground);
    m_opacity->setOpacity(0);
    setGraphicsEffect(m_opacity);

    m_panel = new QWidget(this);
    auto* layout = new QVBoxLayout(m_panel);
    layout->setContentsMargins(metrics.spacing16, metrics.spacing12, metrics.spacing12, metrics.spacing12);
    layout->setSpacing(metrics.spacing8);
    auto* header = new QHBoxLayout;
    header->setSpacing(metrics.spacing8);
    auto* icon = new QLabel(m_panel);
    icon->setPixmap(Ardoise::IconProvider::icon(Ardoise::Icon::Wand, Ardoise::IconTone::Accent)
            .pixmap(metrics.iconSize, metrics.iconSize));
    header->addWidget(icon);
    auto* title = new QLabel(tr("Edit image"), m_panel);
    Ardoise::Theme::applyTypography(title, Ardoise::TextRole::BodyStrong);
    header->addWidget(title);
    header->addStretch();
    m_quality = new QualityStepSlider(m_panel);
    m_quality->setObjectName(QStringLiteral("editQuality"));
    m_quality->setAccessibleName(tr("Edit quality"));
    header->addWidget(m_quality);
    header->addSpacing(metrics.spacing4);
    auto* collapse = new Ardoise::IconButton(Ardoise::Icon::ChevronDown, tr("Collapse editor"), m_panel);
    collapse->setObjectName(QStringLiteral("imageEditCollapse"));
    collapse->setProperty("kiraFlat", true);
    header->addWidget(collapse, 0, Qt::AlignTop);
    layout->addLayout(header);
    m_referenceStrip = new ReferenceImageStrip(QStringLiteral("imageEditRemoveReference"), m_panel);
    m_referenceStrip->setObjectName(QStringLiteral("imageEditReferences"));
    layout->addWidget(m_referenceStrip);
    // Three blocks share the field's top and bottom edges, leaving no gaps: a column with the
    // paperclip above Sparkles, the three-line instruction field, and a full-height Edit button.
    auto* inputRow = new QHBoxLayout;
    inputRow->setSpacing(metrics.spacing8);
    m_prompt = new Ardoise::PlainTextEdit(m_panel);
    m_prompt->setObjectName(QStringLiteral("imageEditPrompt"));
    m_prompt->setPlaceholderText(tr("Describe your edit…"));
    m_prompt->setAccessibleName(tr("Edit instructions"));
    const int fieldHeight = qMax(m_prompt->heightForLines(3), 2 * metrics.controlHeight + metrics.spacing8);
    m_prompt->setFixedHeight(fieldHeight);
    m_prompt->installEventFilter(this);
    const int tile = (fieldHeight - metrics.spacing8) / 2;
    m_attach = new Ardoise::IconButton(Ardoise::Icon::Paperclip, tr("Add reference images"), m_panel);
    m_attach->setObjectName(QStringLiteral("imageEditAttach"));
    m_attach->setToolTip(tr("Add up to %n reference image(s), sent after the source image", nullptr,
        MaximumReferences));
    m_attach->setFixedSize(tile, tile);
    connect(m_referenceStrip, &ReferenceImageStrip::referencesChanged, this, [this] {
        m_attach->setEnabled(!m_referenceStrip->isFull());
        m_panel->layout()->invalidate();
        relayout();
    });
    m_improve = new Ardoise::IconButton(Ardoise::Icon::Sparkles, tr("Improve the instruction"), m_panel);
    m_improve->setObjectName(QStringLiteral("imageEditImprove"));
    m_improve->setFixedSize(tile, tile);
    m_improve->setEnabled(false);
    auto* tools = new QVBoxLayout;
    tools->setSpacing(fieldHeight - 2 * tile);
    tools->addWidget(m_attach);
    tools->addWidget(m_improve);
    inputRow->addLayout(tools);
    inputRow->addWidget(m_prompt, 1);
    m_submit = new Ardoise::PrimaryButton(tr("Edit"), m_panel);
    m_submit->setObjectName(QStringLiteral("imageEditButton"));
    m_submit->setIcon(Ardoise::IconProvider::icon(Ardoise::Icon::Wand));
    m_submit->setFixedHeight(fieldHeight);
    // Wide enough to read as the primary action beside a tall field.
    m_submit->setMinimumWidth(qMax(m_submit->sizeHint().width(), fieldHeight + metrics.spacing24));
    m_submit->setEnabled(false);
    inputRow->addWidget(m_submit);
    layout->addLayout(inputRow);

    m_pill = new QToolButton(this);
    m_pill->setObjectName(QStringLiteral("imageEditExpand"));
    m_pill->setText(tr("Edit image"));
    m_pill->setToolTip(tr("Open the edit panel"));
    m_pill->setIcon(Ardoise::IconProvider::icon(Ardoise::Icon::Wand, Ardoise::IconTone::Accent));
    m_pill->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_pill->setProperty("kiraFlat", true);
    m_pill->setCursor(Qt::PointingHandCursor);
    m_panel->hide();

    connect(collapse, &QToolButton::clicked, this, [this] { setExpanded(false); });
    connect(m_pill, &QToolButton::clicked, this, [this] { setExpanded(true, true); });
    m_reveal->setStartValue(0.0);
    m_reveal->setEndValue(1.0);
    connect(m_reveal, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_revealValue = value.toReal();
        m_opacity->setOpacity(m_revealValue);
        relayout();
    });
    connect(m_reveal, &QVariantAnimation::finished, this, [this] {
        if (!m_active) hide();
        // Render crisply once fully revealed; the effect is only needed while fading.
        m_opacity->setEnabled(m_revealValue < 1);
    });
    m_morph->setDuration(340);
    connect(m_morph, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_morphValue = value.toReal();
        relayout();
    });
    connect(m_morph, &QVariantAnimation::finished, this, [this] {
        if (m_expanded && property("focusPromptAfterMorph").toBool())
            m_prompt->setFocus(Qt::OtherFocusReason);
        setProperty("focusPromptAfterMorph", false);
    });
    m_host->installEventFilter(this);
    m_host->viewport()->installEventFilter(this);
    hide();
}

void EditBubble::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (active) {
        show();
        raise();
    } else if (m_prompt->hasFocus()) {
        m_host->setFocus(Qt::OtherFocusReason);
    }
    m_opacity->setEnabled(true);
    m_reveal->stop();
    m_reveal->setDuration(active ? 300 : 180);
    m_reveal->setEasingCurve(active ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    m_reveal->setStartValue(m_revealValue);
    m_reveal->setEndValue(active ? 1.0 : 0.0);
    m_reveal->start();
}

QList<QImage> EditBubble::references() const { return m_referenceStrip->references(); }

bool EditBubble::addReference(const QImage& image) { return m_referenceStrip->addReference(image); }

void EditBubble::clearReferences() { m_referenceStrip->clearReferences(); }

void EditBubble::setExpanded(bool expanded, bool focusPrompt)
{
    if (m_expanded == expanded)
        return;
    m_expanded = expanded;
    if (!expanded && m_prompt->hasFocus())
        m_pill->setFocus(Qt::OtherFocusReason);
    setProperty("focusPromptAfterMorph", focusPrompt);
    m_morph->stop();
    m_morph->setEasingCurve(expanded ? QEasingCurve(QEasingCurve::OutBack) : QEasingCurve(QEasingCurve::InOutCubic));
    m_morph->setStartValue(m_morphValue);
    m_morph->setEndValue(expanded ? 1.0 : 0.0);
    m_morph->start();
    emit expandedChanged(expanded);
}

QRect EditBubble::shapeRect(qreal morph) const
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    const QRect viewport = m_host->viewport()->geometry();
    const int fullWidth = qMax(qMin(viewport.width() - 2 * metrics.spacing16, MaximumWidth), 240);
    const QSize full(fullWidth, m_panel->sizeHint().height());
    const QSize pill(m_pill->sizeHint().width() + 2 * metrics.spacing12,
        qMax(m_pill->sizeHint().height() + metrics.spacing8, metrics.controlHeight + metrics.spacing8));
    const qreal t = qMax(0.0, morph);
    const QSize size(qRound(pill.width() + (full.width() - pill.width()) * t),
        qRound(pill.height() + (full.height() - pill.height()) * qMin(1.0, t)));
    const int bottom = viewport.bottom() - metrics.spacing16 + qRound((1 - m_revealValue) * RevealTravel);
    return { QPoint(viewport.center().x() - size.width() / 2, bottom - size.height() + 1), size };
}

void EditBubble::relayout()
{
    const int margin = shadowMargin();
    const QRect shape = shapeRect(m_morphValue);
    setGeometry(shape.adjusted(-margin, -margin, margin, margin));
    const QSize full(shapeRect(1).size());
    m_panel->setGeometry(margin + (shape.width() - full.width()) / 2, margin + shape.height() - full.height(),
        full.width(), full.height());
    m_panel->setVisible(m_morphValue > 0.55);
    m_pill->setVisible(m_morphValue < 0.45);
    m_pill->adjustSize();
    m_pill->move(margin + (shape.width() - m_pill->width()) / 2, margin + (shape.height() - m_pill->height()) / 2);
    const QRect viewport = m_host->viewport()->geometry();
    emit coveredRectChanged(isVisible() && m_revealValue > 0 ? shape.translated(-viewport.topLeft()) : QRect());
    update();
}

bool EditBubble::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == m_host || watched == m_host->viewport()) && event->type() == QEvent::Resize)
        relayout();
    if (watched == m_prompt) {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)
            update();
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            setExpanded(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void EditBubble::wheelEvent(QWheelEvent* event) { event->accept(); }

void EditBubble::mousePressEvent(QMouseEvent* event) { event->accept(); }

void EditBubble::paintEvent(QPaintEvent*)
{
    const auto& tokens = Ardoise::Theme::tokens();
    const int margin = shadowMargin();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF shape = QRectF(rect()).adjusted(margin + 0.5, margin + 0.5, -margin - 0.5, -margin - 0.5);
    const qreal pillRadius = shape.height() / 2;
    const qreal panelRadius = tokens.metrics.radiusLarge + tokens.metrics.spacing4;
    const qreal morph = qBound(0.0, m_morphValue, 1.0);
    const qreal radius = qMin(shape.height() / 2, pillRadius + (panelRadius - pillRadius) * morph);
    FloatingSurface::paint(painter, shape, radius, margin, m_prompt->hasFocus() && m_panel->isVisible());
}
