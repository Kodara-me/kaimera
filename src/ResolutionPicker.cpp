#include "ResolutionPicker.h"
#include <Ardoise/Theme.h>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int ShapeBox = 26;
constexpr int ToggleWidth = 32;
constexpr int ToggleHeight = 24;
// Breathing room above the tiles, separating them from the row above.
constexpr int TopPadding = 10;
qreal aspect(const QSize& size) { return size.height() > 0 ? qreal(size.width()) / size.height() : 1; }
}

ResolutionPicker::ResolutionPicker(QWidget* parent)
    : QWidget(parent)
    , m_highlight(new QVariantAnimation(this))
    , m_flipAnimation(new QVariantAnimation(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_highlight->setDuration(240);
    m_highlight->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_highlight, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_highlightSlot = value.toReal();
        update();
    });
    m_flipAnimation->setDuration(420);
    m_flipAnimation->setEasingCurve(QEasingCurve::OutBack);
    m_flipAnimation->setStartValue(0.0);
    m_flipAnimation->setEndValue(1.0);
    connect(m_flipAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_flip = value.toReal();
        update();
    });
}

void ResolutionPicker::setOptions(const QList<Option>& options, int current)
{
    m_options = options;
    m_current = current >= 0 && current < options.size() ? current : -1;
    if (m_current >= 0 && m_options[m_current].size.width() != m_options[m_current].size.height())
        m_portrait = m_options[m_current].size.width() < m_options[m_current].size.height();
    m_highlight->stop();
    m_highlightSlot = qMax(0, int(visibleOptions().indexOf(m_current)));
    m_hovered = -1;
    setAccessibleDescription(m_current >= 0 ? m_options[m_current].text : QString());
    updateGeometry();
    update();
}

void ResolutionPicker::setCurrentIndex(int index) { select(index, false); }

void ResolutionPicker::setPortrait(bool portrait)
{
    if (m_portrait == portrait)
        return;
    m_portrait = portrait;
    m_flipAnimation->stop();
    m_flip = 0;
    m_flipAnimation->start();
    // Keep the same ratio, rotated, so flipping feels like turning the canvas.
    const int rotated = counterpart(m_current);
    m_highlight->stop();
    m_highlightSlot = qMax(0, int(visibleOptions().indexOf(rotated >= 0 ? rotated : m_current)));
    if (rotated >= 0)
        select(rotated, true);
    update();
}

QList<int> ResolutionPicker::visibleOptions() const
{
    QList<int> visible;
    for (int i = 0; i < m_options.size(); ++i) {
        const auto size = m_options[i].size;
        if (size.width() == size.height() || (size.width() < size.height()) == m_portrait)
            visible.append(i);
    }
    std::stable_sort(visible.begin(), visible.end(), [this](int a, int b) {
        const auto elongation = [this](int i) {
            const auto size = m_options[i].size;
            return qreal(qMax(size.width(), size.height())) / qMax(1, qMin(size.width(), size.height()));
        };
        return elongation(a) < elongation(b);
    });
    return visible;
}

int ResolutionPicker::counterpart(int index) const
{
    if (index < 0 || index >= m_options.size())
        return -1;
    const auto size = m_options[index].size;
    if (size.width() == size.height())
        return index;
    for (int i = 0; i < m_options.size(); ++i)
        if (i != index && std::abs(aspect(m_options[i].size) * aspect(size) - 1) < 0.03)
            return i;
    return -1;
}

int ResolutionPicker::tilesHeight() const
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    return metrics.spacing6 + ShapeBox + metrics.spacing4 + QFontMetrics(Ardoise::Theme::font(Ardoise::TextRole::Caption)).height()
        + metrics.spacing6;
}

QSize ResolutionPicker::sizeHint() const
{
    return { 320, TopPadding + tilesHeight() + Ardoise::Theme::tokens().metrics.spacing8 + ToggleHeight };
}

QSize ResolutionPicker::minimumSizeHint() const { return { 200, sizeHint().height() }; }

QRectF ResolutionPicker::tileRect(qreal slot, int count) const
{
    const qreal tileWidth = count > 0 ? qreal(width()) / count : width();
    return { slot * tileWidth, qreal(TopPadding), tileWidth, qreal(tilesHeight()) };
}

QRectF ResolutionPicker::toggleRect(bool portrait) const
{
    const qreal top = TopPadding + tilesHeight() + Ardoise::Theme::tokens().metrics.spacing8;
    const qreal left = width() - 2 * ToggleWidth - 1;
    return { left + (portrait ? ToggleWidth : 0), top, qreal(ToggleWidth), qreal(ToggleHeight) };
}

int ResolutionPicker::slotAt(const QPointF& point) const
{
    const int count = int(visibleOptions().size());
    if (count == 0 || point.y() < TopPadding || point.y() > TopPadding + tilesHeight())
        return -1;
    return qBound(0, int(point.x() / tileRect(0, count).width()), count - 1);
}

void ResolutionPicker::select(int index, bool notify)
{
    if (index < 0 || index >= m_options.size() || index == m_current)
        return;
    m_current = index;
    const auto size = m_options[index].size;
    if (size.width() != size.height() && (size.width() < size.height()) != m_portrait) {
        // Selected from outside in the other orientation: turn the strip to match.
        m_portrait = !m_portrait;
        m_flip = 0;
        m_flipAnimation->start();
        m_highlightSlot = qMax(0, int(visibleOptions().indexOf(index)));
    }
    setAccessibleDescription(m_options[index].text);
    animateHighlight();
    if (notify)
        emit currentIndexChanged(index);
}

void ResolutionPicker::animateHighlight()
{
    const int target = int(visibleOptions().indexOf(m_current));
    if (target < 0)
        return;
    m_highlight->stop();
    if (!isVisible()) {
        m_highlightSlot = target;
        update();
        return;
    }
    m_highlight->setStartValue(m_highlightSlot);
    m_highlight->setEndValue(qreal(target));
    m_highlight->start();
}

bool ResolutionPicker::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const auto visible = visibleOptions();
        const int slot = slotAt(help->pos());
        if (slot >= 0)
            QToolTip::showText(help->globalPos(), m_options[visible[slot]].text, this);
        else if (toggleRect(false).contains(help->pos()))
            QToolTip::showText(help->globalPos(), tr("Landscape formats"), this);
        else if (toggleRect(true).contains(help->pos()))
            QToolTip::showText(help->globalPos(), tr("Portrait formats"), this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void ResolutionPicker::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return QWidget::mousePressEvent(event);
    const QPointF point = event->position();
    if (toggleRect(false).contains(point))
        setPortrait(false);
    else if (toggleRect(true).contains(point))
        setPortrait(true);
    else if (const int slot = slotAt(point); slot >= 0)
        select(visibleOptions()[slot], true);
}

void ResolutionPicker::mouseMoveEvent(QMouseEvent* event)
{
    const int slot = slotAt(event->position());
    const bool toggle = toggleRect(false).contains(event->position()) || toggleRect(true).contains(event->position());
    setCursor(slot >= 0 || toggle ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (slot != m_hovered) {
        m_hovered = slot;
        update();
    }
}

void ResolutionPicker::leaveEvent(QEvent* event)
{
    m_hovered = -1;
    update();
    QWidget::leaveEvent(event);
}

void ResolutionPicker::keyPressEvent(QKeyEvent* event)
{
    const auto visible = visibleOptions();
    const int slot = int(visible.indexOf(m_current));
    switch (event->key()) {
    case Qt::Key_Left:
        if (slot > 0) select(visible[slot - 1], true);
        break;
    case Qt::Key_Right:
        if (slot >= 0 && slot + 1 < visible.size()) select(visible[slot + 1], true);
        break;
    case Qt::Key_Up:
    case Qt::Key_Down:
        setPortrait(!m_portrait);
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
}

void ResolutionPicker::paintEvent(QPaintEvent*)
{
    const auto& tokens = Ardoise::Theme::tokens();
    const auto& colors = tokens.colors;
    const auto& metrics = tokens.metrics;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool enabled = isEnabled();
    const auto visible = visibleOptions();
    const int count = int(visible.size());
    const int currentSlot = int(visible.indexOf(m_current));

    if (m_hovered >= 0 && m_hovered != currentSlot) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.panelBackgroundHover);
        painter.drawRoundedRect(tileRect(m_hovered, count).adjusted(2, 0, -2, 0), metrics.radiusMedium, metrics.radiusMedium);
    }
    if (currentSlot >= 0) {
        painter.setPen(QPen(hasFocus() ? colors.borderAccent : colors.accent, 1));
        painter.setBrush(colors.accentMuted);
        painter.drawRoundedRect(tileRect(m_highlightSlot, count).adjusted(2.5, 0.5, -2.5, -0.5),
            metrics.radiusMedium, metrics.radiusMedium);
    }

    const QFont caption = Ardoise::Theme::font(Ardoise::TextRole::Caption);
    for (int slot = 0; slot < count; ++slot) {
        const auto& option = m_options[visible[slot]];
        const QRectF tile = tileRect(slot, count);
        const bool selected = slot == currentSlot;
        // Draw each ratio to scale; while flipping, morph from its rotated twin.
        const qreal longest = qMax(option.size.width(), option.size.height());
        const QSizeF shape(ShapeBox * option.size.width() / longest, ShapeBox * option.size.height() / longest);
        const QSizeF rotated(shape.height(), shape.width());
        const QSizeF drawn(rotated.width() + (shape.width() - rotated.width()) * m_flip,
            rotated.height() + (shape.height() - rotated.height()) * m_flip);
        const QPointF centre(tile.center().x(), tile.top() + metrics.spacing6 + ShapeBox / 2.0);
        const QRectF frame(centre.x() - drawn.width() / 2, centre.y() - drawn.height() / 2, drawn.width(), drawn.height());
        QColor stroke = !enabled ? colors.textDisabled
            : selected ? colors.textPrimary : slot == m_hovered ? colors.textSecondary : colors.textMuted;
        QColor fill = selected && enabled ? colors.accent : Qt::transparent;
        painter.setPen(QPen(stroke, 1.5));
        painter.setBrush(fill);
        painter.drawRoundedRect(frame, 2.5, 2.5);
        QFont label = caption;
        label.setBold(selected);
        painter.setFont(label);
        painter.setPen(!enabled ? colors.textDisabled : selected ? colors.textPrimary : colors.textMuted);
        const QRectF labelBox(tile.left(), tile.top() + metrics.spacing6 + ShapeBox + metrics.spacing4, tile.width(),
            QFontMetrics(caption).height());
        painter.drawText(labelBox, Qt::AlignCenter | Qt::TextDontClip, option.ratio);
    }

    // Selected format name and pixel size, beside the orientation switch.
    const qreal rowTop = toggleRect(false).top();
    const QRectF textArea(0, rowTop, toggleRect(false).left() - metrics.spacing8, ToggleHeight);
    if (m_current >= 0) {
        const auto& option = m_options[m_current];
        QFont name = font();
        name.setBold(true);
        // Pixel sizes never use digit grouping; the name is always shown in full.
        const QString pixels = QStringLiteral("%1 × %2").arg(option.size.width()).arg(option.size.height());
        const qreal nameWidth = std::ceil(QFontMetricsF(name).horizontalAdvance(option.name));
        painter.setFont(name);
        painter.setPen(enabled ? colors.textPrimary : colors.textDisabled);
        painter.drawText(QRectF(textArea.left(), rowTop, nameWidth + 1, ToggleHeight),
            Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, option.name);
        painter.setFont(font());
        painter.setPen(colors.textMuted);
        painter.drawText(QRectF(textArea.left() + nameWidth + metrics.spacing8, rowTop,
                             textArea.width() - nameWidth - metrics.spacing8, ToggleHeight),
            Qt::AlignLeft | Qt::AlignVCenter, pixels);
    }

    const QRectF toggles = toggleRect(false).united(toggleRect(true));
    painter.setPen(QPen(colors.borderNormal, 1));
    painter.setBrush(colors.inputBackground);
    painter.drawRoundedRect(toggles.adjusted(0.5, 0.5, -0.5, -0.5), metrics.radiusMedium, metrics.radiusMedium);
    for (const bool portrait : { false, true }) {
        const QRectF segment = toggleRect(portrait).adjusted(2, 2, -2, -2);
        const bool active = portrait == m_portrait;
        if (active) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(enabled ? colors.accentMuted : colors.panelBackgroundHover);
            painter.drawRoundedRect(segment, metrics.radiusSmall, metrics.radiusSmall);
        }
        const QSizeF icon = portrait ? QSizeF(9, 13) : QSizeF(13, 9);
        painter.setPen(QPen(!enabled ? colors.textDisabled : active ? colors.textPrimary : colors.textMuted, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(segment.center() - QPointF(icon.width() / 2, icon.height() / 2), icon), 2, 2);
    }
}
