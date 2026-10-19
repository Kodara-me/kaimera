#include "GenerationCard.h"
#include "FloatingSurface.h"
#include "GenerationInspectorWidget.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/IconProvider.h>
#include <Ardoise/Theme.h>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QShortcut>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWheelEvent>

namespace
{
int shadowMargin() { return Ardoise::Theme::tokens().metrics.spacing8; }
// Room kept between the card and the preview edges.
int viewportMargin() { return Ardoise::Theme::tokens().metrics.spacing24; }
constexpr int MaximumWidth = 920;
// From this width the prompts and the look sit side by side.
constexpr int LandscapeWidth = 660;
// Distance the card rises while fading in.
constexpr int RevealTravel = 28;
}

GenerationCard::GenerationCard(GenerationInspectorWidget* content, QAbstractScrollArea* host)
    : QWidget(host)
    , m_host(host)
    , m_content(content)
    , m_reveal(new QVariantAnimation(this))
    , m_opacity(new QGraphicsOpacityEffect(this))
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    setObjectName(QStringLiteral("generationCard"));
    setAttribute(Qt::WA_NoSystemBackground);
    m_opacity->setOpacity(0);
    setGraphicsEffect(m_opacity);

    m_panel = new QWidget(this);
    auto* layout = new QVBoxLayout(m_panel);
    layout->setContentsMargins(0, metrics.spacing12, 0, 0);
    layout->setSpacing(0);
    auto* header = new QHBoxLayout;
    header->setContentsMargins(metrics.spacing16, 0, metrics.spacing16, 0);
    header->setSpacing(metrics.spacing8);
    auto* icon = new QLabel(m_panel);
    icon->setPixmap(Ardoise::IconProvider::icon(Ardoise::Icon::Wand, Ardoise::IconTone::Accent)
            .pixmap(metrics.iconSize, metrics.iconSize));
    header->addWidget(icon);
    auto* title = new QLabel(tr("New image"), m_panel);
    title->setObjectName(QStringLiteral("generationCardTitle"));
    Ardoise::Theme::applyTypography(title, Ardoise::TextRole::SectionTitle);
    header->addWidget(title, 1);
    auto* close = new Ardoise::IconButton(Ardoise::Icon::Close, tr("Close"), m_panel);
    close->setObjectName(QStringLiteral("generationCardClose"));
    close->setProperty("kiraFlat", true);
    close->setFixedSize(metrics.controlHeightSmall, metrics.controlHeightSmall);
    connect(close, &QToolButton::clicked, this, &GenerationCard::closeRequested);
    header->addWidget(close);
    layout->addLayout(header);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), m_panel, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &GenerationCard::closeRequested);
    content->setParent(m_panel);
    layout->addWidget(content, 1);

    connect(m_reveal, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_revealValue = value.toReal();
        m_opacity->setOpacity(m_revealValue);
        relayout();
    });
    connect(m_reveal, &QVariantAnimation::finished, this, [this] {
        if (!m_active)
            hide();
        // Render crisply once fully revealed; the effect is only needed while fading.
        m_opacity->setEnabled(m_revealValue < 1);
    });
    connect(qApp, &QApplication::focusChanged, this, [this] { update(); });
    m_host->installEventFilter(this);
    m_host->viewport()->installEventFilter(this);
    m_panel->installEventFilter(this);
    relayout();
}

void GenerationCard::relayout()
{
    const int margin = shadowMargin();
    const QRect viewport = m_host->viewport()->geometry();
    const int width = qMax(qMin(viewport.width() - 2 * viewportMargin(), MaximumWidth), 280);
    // Choose the layout for this width before measuring the height it needs.
    m_content->setLandscape(width >= LandscapeWidth);
    const int height = qMin(viewport.height() - 2 * viewportMargin(), m_panel->sizeHint().height());
    // Stay centred, moving down to clear an overlay at the top when there is room, but never shrinking.
    const int lowest = viewport.bottom() + 1 - viewportMargin() - height;
    const int top = qMin(qMax(viewport.center().y() - height / 2, viewport.top() + m_topInset), lowest);
    const QRect shape(viewport.center().x() - width / 2,
        top + qRound((1 - m_revealValue) * RevealTravel), width, qMax(height, 0));
    setGeometry(shape.adjusted(-margin, -margin, margin, margin));
    m_panel->setGeometry(margin, margin, shape.width(), shape.height());
    update();
}

void GenerationCard::setTopInset(int inset)
{
    if (m_topInset == inset)
        return;
    m_topInset = inset;
    relayout();
}

bool GenerationCard::eventFilter(QObject* watched, QEvent* event)
{
    if (((watched == m_host || watched == m_host->viewport()) && event->type() == QEvent::Resize)
        || (watched == m_panel && event->type() == QEvent::LayoutRequest))
        relayout();
    return QWidget::eventFilter(watched, event);
}

void GenerationCard::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    raise();
    relayout();
    if (m_active && m_revealValue < 1 && m_reveal->state() != QAbstractAnimation::Running)
        animateReveal();
}

void GenerationCard::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (active) {
        show();
        if (isVisible()) // Also reverses a fade-out that is still running.
            animateReveal();
        return;
    }
    if (isAncestorOf(QApplication::focusWidget()))
        m_host->setFocus(Qt::OtherFocusReason);
    if (isVisible())
        animateReveal();
    else
        hide();
}

void GenerationCard::animateReveal()
{
    m_reveal->stop();
    m_opacity->setEnabled(true);
    m_reveal->setDuration(m_active ? 480 : 200);
    m_reveal->setEasingCurve(m_active ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    m_reveal->setStartValue(m_revealValue);
    m_reveal->setEndValue(m_active ? 1.0 : 0.0);
    m_reveal->start();
}

// The card sits over the preview; keep clicks and scrolling from reaching it.
void GenerationCard::mousePressEvent(QMouseEvent* event) { event->accept(); }

void GenerationCard::wheelEvent(QWheelEvent* event) { event->accept(); }

void GenerationCard::paintEvent(QPaintEvent*)
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    const int margin = shadowMargin();
    QPainter painter(this);
    const QRectF shape = QRectF(rect()).adjusted(margin + 0.5, margin + 0.5, -margin - 0.5, -margin - 0.5);
    const bool focused = isAncestorOf(QApplication::focusWidget());
    FloatingSurface::paint(painter, shape, metrics.radiusLarge + metrics.spacing4, margin, focused, 248);
}
