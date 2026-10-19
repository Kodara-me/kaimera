#include "ShotPicker.h"
#include <Ardoise/Theme.h>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QVariantAnimation>
#include <cmath>

namespace
{
constexpr int ShotCount = 8;
constexpr int FrameWidth = 32;
constexpr int FrameHeight = 24;
constexpr int ShapeBox = 26;
// Breathing room above the tiles, separating them from the row above.
constexpr int TopPadding = 10;
constexpr int DetailsHeight = 24;
// Vertical span of a standing figure, from the crown (0) to the feet (1),
// that each shot's frame shows.
struct Span
{
    qreal top;
    qreal bottom;
};
constexpr Span Spans[ShotCount] = { { 0, 0 }, { 0.03, 0.105 }, { -0.01, 0.17 }, { -0.02, 0.3 }, { -0.03, 0.52 },
    { -0.03, 0.72 }, { -0.06, 1.06 }, { -1.0, 1.4 } };

// Right half of a standing figure's body below the neck, closed along its centre line:
// rounded shoulder, an arm hanging apart from the torso, the waist, hip and a tapered leg.
QPainterPath halfBody()
{
    QPainterPath half;
    half.moveTo(0, 0.15);
    half.lineTo(0.034, 0.145);
    half.cubicTo(0.07, 0.148, 0.122, 0.156, 0.132, 0.205);
    half.lineTo(0.156, 0.468);
    half.quadTo(0.157, 0.5, 0.136, 0.5);
    half.quadTo(0.118, 0.5, 0.117, 0.47);
    half.lineTo(0.106, 0.255);
    half.lineTo(0.094, 0.41);
    half.quadTo(0.093, 0.46, 0.102, 0.52);
    half.lineTo(0.086, 0.968);
    half.quadTo(0.086, 1.0, 0.058, 1.0);
    half.lineTo(0.034, 1.0);
    half.quadTo(0.014, 1.0, 0.015, 0.968);
    half.lineTo(0.012, 0.585);
    half.quadTo(0.01, 0.556, 0, 0.556);
    half.closeSubpath();
    return half;
}

// Boolean path operations flatten curves in absolute units, so shapes are combined
// a thousand times larger and scaled back down to stay smooth.
constexpr qreal Precision = 1000;
QPainterPath enlarged(const QPainterPath& path) { return QTransform::fromScale(Precision, Precision).map(path); }
QPainterPath reduced(const QPainterPath& path) { return QTransform::fromScale(1 / Precision, 1 / Precision).map(path); }

// A standing figure one unit tall, its crown at the origin. Close shots also
// show its eyes: almond sockets cut out of the face around their pupils.
QPainterPath figure(bool withEyes)
{
    const QPainterPath half = enlarged(halfBody());
    QPainterPath head;
    head.addEllipse(QPointF(0, 0.068), 0.056, 0.066);
    QPainterPath neck;
    neck.addRoundedRect(QRectF(-0.032, 0.1, 0.064, 0.06), 0.01, 0.01);
    QPainterPath person = half.united(QTransform::fromScale(-1, 1).map(half)).united(enlarged(head)).united(enlarged(neck));
    if (!withEyes)
        return reduced(person);
    QPainterPath sockets;
    QPainterPath pupils;
    for (const qreal side : { -1.0, 1.0 }) {
        sockets.moveTo(side * 0.007, 0.064);
        sockets.cubicTo(side * 0.014, 0.053, side * 0.03, 0.053, side * 0.038, 0.063);
        sockets.cubicTo(side * 0.03, 0.072, side * 0.014, 0.073, side * 0.007, 0.064);
        sockets.closeSubpath();
        pupils.addEllipse(QPointF(side * 0.022, 0.0635), 0.0055, 0.0055);
    }
    return reduced(person.subtracted(enlarged(sockets)).united(enlarged(pupils)));
}
}

ShotPicker::ShotPicker(QWidget* parent)
    : QWidget(parent)
    , m_highlight(new QVariantAnimation(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(tr("Camera shot"));
    setAccessibleDescription(shotDescription(Shot::Free));
    m_highlight->setDuration(240);
    m_highlight->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_highlight, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_highlightSlot = value.toReal();
        update();
    });
}

QString ShotPicker::shotName(Shot shot) const
{
    switch (shot) {
    case Shot::Free: return tr("Free");
    case Shot::ExtremeCloseUp: return tr("Extreme close-up");
    case Shot::CloseUp: return tr("Close-up");
    case Shot::Portrait: return tr("Portrait");
    case Shot::Medium: return tr("Medium shot");
    case Shot::Cowboy: return tr("Cowboy shot");
    case Shot::FullBody: return tr("Full body");
    case Shot::Wide: return tr("Wide shot");
    }
    return {};
}

QString ShotPicker::shotDescription(Shot shot) const
{
    switch (shot) {
    case Shot::Free: return tr("The model frames the scene");
    case Shot::ExtremeCloseUp: return tr("A detail of the face, such as the eyes");
    case Shot::CloseUp: return tr("The face fills the frame");
    case Shot::Portrait: return tr("Head and shoulders");
    case Shot::Medium: return tr("From the waist up");
    case Shot::Cowboy: return tr("From mid-thigh up");
    case Shot::FullBody: return tr("The whole figure, head to feet");
    case Shot::Wide: return tr("A small figure within its surroundings");
    }
    return {};
}

void ShotPicker::setCurrentShot(Shot shot) { select(int(shot), false); }

int ShotPicker::tilesHeight() const
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    return metrics.spacing6 + ShapeBox + metrics.spacing4 + QFontMetrics(Ardoise::Theme::font(Ardoise::TextRole::Caption)).height()
        + metrics.spacing6;
}

QSize ShotPicker::sizeHint() const
{
    return { 320, TopPadding + tilesHeight() + Ardoise::Theme::tokens().metrics.spacing8 + DetailsHeight };
}

QSize ShotPicker::minimumSizeHint() const { return { 200, sizeHint().height() }; }

QRectF ShotPicker::tileRect(qreal slot) const
{
    const qreal tileWidth = qreal(width()) / ShotCount;
    return { slot * tileWidth, qreal(TopPadding), tileWidth, qreal(tilesHeight()) };
}

int ShotPicker::slotAt(const QPointF& point) const
{
    if (point.y() < TopPadding || point.y() > TopPadding + tilesHeight())
        return -1;
    return qBound(0, int(point.x() / tileRect(0).width()), ShotCount - 1);
}

void ShotPicker::select(int index, bool notify)
{
    if (index < 0 || index >= ShotCount || index == m_current)
        return;
    m_current = index;
    setAccessibleDescription(shotDescription(Shot(index)));
    m_highlight->stop();
    if (isVisible()) {
        m_highlight->setStartValue(m_highlightSlot);
        m_highlight->setEndValue(qreal(index));
        m_highlight->start();
    } else {
        m_highlightSlot = index;
    }
    update();
    if (notify)
        emit currentShotChanged(Shot(index));
}

bool ShotPicker::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        if (const int slot = slotAt(help->pos()); slot >= 0)
            QToolTip::showText(help->globalPos(),
                QStringLiteral("%1 — %2").arg(shotName(Shot(slot)), shotDescription(Shot(slot))), this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void ShotPicker::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return QWidget::mousePressEvent(event);
    if (const int slot = slotAt(event->position()); slot >= 0)
        select(slot, true);
}

void ShotPicker::mouseMoveEvent(QMouseEvent* event)
{
    const int slot = slotAt(event->position());
    setCursor(slot >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (slot != m_hovered) {
        m_hovered = slot;
        update();
    }
}

void ShotPicker::leaveEvent(QEvent* event)
{
    m_hovered = -1;
    update();
    QWidget::leaveEvent(event);
}

void ShotPicker::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Left:
        select(m_current - 1, true);
        break;
    case Qt::Key_Right:
        select(m_current + 1, true);
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
}

void ShotPicker::paintEvent(QPaintEvent*)
{
    const auto& tokens = Ardoise::Theme::tokens();
    const auto& colors = tokens.colors;
    const auto& metrics = tokens.metrics;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool enabled = isEnabled();

    if (m_hovered >= 0 && m_hovered != m_current) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.panelBackgroundHover);
        painter.drawRoundedRect(tileRect(m_hovered).adjusted(2, 0, -2, 0), metrics.radiusMedium, metrics.radiusMedium);
    }
    painter.setPen(QPen(hasFocus() ? colors.borderAccent : colors.accent, 1));
    painter.setBrush(colors.accentMuted);
    painter.drawRoundedRect(tileRect(m_highlightSlot).adjusted(2.5, 0.5, -2.5, -0.5), metrics.radiusMedium,
        metrics.radiusMedium);

    const QFont caption = Ardoise::Theme::font(Ardoise::TextRole::Caption);
    const QPainterPath person = figure(false);
    const QPainterPath face = figure(true);
    for (int slot = 0; slot < ShotCount; ++slot) {
        const QRectF tile = tileRect(slot);
        const bool selected = slot == m_current && enabled;
        const QPointF centre(tile.center().x(), tile.top() + metrics.spacing6 + ShapeBox / 2.0);
        const QRectF frame(centre.x() - FrameWidth / 2.0, centre.y() - FrameHeight / 2.0, FrameWidth, FrameHeight);
        const QColor ink = !enabled ? colors.textDisabled
            : selected ? colors.textPrimary : slot == m_hovered ? colors.textSecondary : colors.textMuted;
        if (slot == int(Shot::Free)) {
            // No framing imposed: viewfinder corners around a focus point.
            painter.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            const qreal arm = 6;
            const QRectF corners = frame.adjusted(1, 1, -1, -1);
            for (const QPointF& corner : { corners.topLeft(), corners.topRight(), corners.bottomRight(), corners.bottomLeft() }) {
                const qreal dx = corner.x() < frame.center().x() ? arm : -arm;
                const qreal dy = corner.y() < frame.center().y() ? arm : -arm;
                painter.drawPolyline(QPolygonF { corner + QPointF(0, dy), corner, corner + QPointF(dx, 0) });
            }
            painter.drawEllipse(frame.center(), 3.5, 3.5);
            continue;
        }
        QPainterPath clip;
        clip.addRoundedRect(frame, 3, 3);
        QColor backdrop = selected ? colors.accent : ink;
        if (!selected)
            backdrop.setAlphaF(0.12);
        painter.setPen(Qt::NoPen);
        painter.setBrush(backdrop);
        painter.drawPath(clip);
        // Scale the figure so the shot's span fills the frame's height, then crop it.
        const Span span = Spans[slot];
        const qreal scale = FrameHeight / (span.bottom - span.top);
        painter.save();
        painter.setClipPath(clip);
        painter.translate(frame.center().x(), frame.top() - span.top * scale);
        painter.scale(scale, scale);
        QColor scenery = ink;
        scenery.setAlphaF(0.35);
        if (slot == int(Shot::Wide)) {
            // Distant hills give the figure its surroundings.
            painter.setBrush(scenery);
            painter.drawPolygon(QPolygonF { { -1.5, 1 }, { -0.75, 0.25 }, { -0.25, 0.7 }, { 0.15, 0.4 }, { 0.9, 1 } });
            painter.drawPolygon(QPolygonF { { 0.3, 1 }, { 0.85, 0.55 }, { 1.5, 1 } });
        }
        if (slot == int(Shot::Wide) || slot == int(Shot::FullBody)) {
            painter.setBrush(scenery);
            painter.drawRect(QRectF(-2, 1, 4, 1));
        }
        painter.setBrush(ink);
        painter.drawPath(slot == int(Shot::ExtremeCloseUp) || slot == int(Shot::CloseUp) ? face : person);
        painter.restore();
        QColor border = ink;
        border.setAlphaF(selected ? 1 : 0.7);
        painter.setPen(QPen(border, 1.25));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(frame.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    }

    // Short labels under the tiles; the full name and description sit below the strip.
    const QString labels[ShotCount] = { tr("Free"), tr("Detail", "Extreme close-up"), tr("Face", "Close-up"),
        tr("Bust", "Portrait"), tr("Waist", "Medium shot"), tr("Cowboy", "Cowboy shot"), tr("Full", "Full body"),
        tr("Wide", "Wide shot") };
    for (int slot = 0; slot < ShotCount; ++slot) {
        const QRectF tile = tileRect(slot);
        const bool selected = slot == m_current;
        QFont label = caption;
        label.setBold(selected);
        painter.setFont(label);
        painter.setPen(!enabled ? colors.textDisabled : selected ? colors.textPrimary : colors.textMuted);
        const QRectF labelBox(tile.left(), tile.top() + metrics.spacing6 + ShapeBox + metrics.spacing4, tile.width(),
            QFontMetrics(caption).height());
        painter.drawText(labelBox, Qt::AlignCenter | Qt::TextDontClip, labels[slot]);
    }

    const qreal rowTop = TopPadding + tilesHeight() + metrics.spacing8;
    QFont name = font();
    name.setBold(true);
    const QString title = shotName(Shot(m_current));
    const qreal nameWidth = std::ceil(QFontMetricsF(name).horizontalAdvance(title));
    painter.setFont(name);
    painter.setPen(enabled ? colors.textPrimary : colors.textDisabled);
    painter.drawText(QRectF(0, rowTop, nameWidth + 1, DetailsHeight), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip,
        title);
    painter.setFont(font());
    painter.setPen(colors.textMuted);
    const QRectF details(nameWidth + metrics.spacing8, rowTop, width() - nameWidth - metrics.spacing8, DetailsHeight);
    painter.drawText(details, Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetrics(font()).elidedText(shotDescription(Shot(m_current)), Qt::ElideRight, int(details.width())));
}
