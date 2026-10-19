#include "QualityStepSlider.h"
#include <Ardoise/Theme.h>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QVariantAnimation>
#include <cmath>

namespace
{
constexpr int ThumbRadius = 8;
constexpr int HaloRadius = 13;
}

QualityStepSlider::QualityStepSlider(QWidget* parent)
    : QSlider(Qt::Horizontal, parent)
    , m_animation(new QVariantAnimation(this))
{
    setRange(0, 0);
    setPageStep(1);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    setFont(Ardoise::Theme::font(Ardoise::TextRole::Caption));
    m_animation->setDuration(280);
    m_animation->setEasingCurve(QEasingCurve(QEasingCurve::OutBack));
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& position) {
        m_position = position.toReal();
        update();
    });
}

void QualityStepSlider::setSteps(const QStringList& labels, const QList<bool>& available, const QStringList& toolTips)
{
    m_labels = labels;
    m_available = available;
    m_toolTips = toolTips;
    setRange(0, qMax(0, int(labels.size()) - 1));
    m_animation->stop();
    m_position = value();
    updateGeometry();
    update();
}

QSize QualityStepSlider::sizeHint() const
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    return { 260, HaloRadius * 2 + metrics.spacing2 + fontMetrics().height() };
}

QSize QualityStepSlider::minimumSizeHint() const
{
    int labels = 0;
    for (const auto& label : m_labels)
        labels += fontMetrics().horizontalAdvance(label) + Ardoise::Theme::tokens().metrics.spacing12;
    return { qMax(160, labels), sizeHint().height() };
}

int QualityStepSlider::padding() const
{
    int widest = 0;
    for (const auto& label : m_labels)
        widest = qMax(widest, fontMetrics().horizontalAdvance(label));
    return qMax(HaloRadius, widest / 2 + 1);
}

int QualityStepSlider::trackY() const { return HaloRadius; }

qreal QualityStepSlider::stopX(qreal position) const
{
    const int count = int(m_labels.size());
    if (count < 2)
        return width() / 2.0;
    return padding() + position * (width() - 2.0 * padding()) / (count - 1);
}

qreal QualityStepSlider::positionAt(qreal x) const
{
    const int count = int(m_labels.size());
    if (count < 2)
        return 0;
    const qreal span = width() - 2.0 * padding();
    return qBound(0.0, (x - padding()) * (count - 1) / qMax(1.0, span), qreal(count - 1));
}

void QualityStepSlider::sliderChange(SliderChange change)
{
    QSlider::sliderChange(change);
    if (change != SliderValueChange)
        return;
    setAccessibleDescription(currentLabel());
    if (m_dragging || !isVisible()) {
        if (!m_dragging) m_position = value();
        update();
        return;
    }
    m_animation->stop();
    m_animation->setStartValue(m_position);
    m_animation->setEndValue(qreal(value()));
    m_animation->start();
}

bool QualityStepSlider::event(QEvent* event)
{
    switch (event->type()) {
    case QEvent::HoverEnter: m_hovered = true; update(); break;
    case QEvent::HoverLeave: m_hovered = false; update(); break;
    case QEvent::ToolTip: {
        auto* help = static_cast<QHelpEvent*>(event);
        const int index = qRound(positionAt(help->pos().x()));
        const QString tip = m_toolTips.value(index);
        if (m_labels.isEmpty() || tip.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), QStringLiteral("%1 — %2").arg(m_labels.value(index), tip), this);
        return true;
    }
    default: break;
    }
    return QSlider::event(event);
}

void QualityStepSlider::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_labels.isEmpty()) {
        event->ignore();
        return;
    }
    m_animation->stop();
    m_dragging = true;
    setSliderDown(true);
    m_position = positionAt(event->position().x());
    setValue(qRound(m_position));
    update();
}

void QualityStepSlider::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging)
        return;
    m_position = positionAt(event->position().x());
    setValue(qRound(m_position));
    update();
}

void QualityStepSlider::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragging || event->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    setSliderDown(false);
    m_animation->stop();
    m_animation->setStartValue(m_position);
    m_animation->setEndValue(qreal(value()));
    m_animation->start();
}

void QualityStepSlider::paintEvent(QPaintEvent*)
{
    const auto& tokens = Ardoise::Theme::tokens();
    const auto& colors = tokens.colors;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool enabled = isEnabled() && !m_labels.isEmpty();
    const int count = int(m_labels.size());
    const qreal y = trackY();
    // Without steps, show an empty disabled track so the row never looks broken.
    const qreal left = count ? stopX(0) : HaloRadius;
    const qreal right = count ? stopX(qMax(0, count - 1)) : width() - HaloRadius;
    const qreal position = qBound(0.0, m_position, qreal(qMax(0, count - 1)));
    const qreal thumb = stopX(position);

    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.borderNormal);
    painter.drawRoundedRect(QRectF(QPointF(left, y - 2), QPointF(right, y + 2)), 2, 2);
    QColor active = enabled ? colors.accent : colors.textDisabled;
    painter.setBrush(active);
    if (count > 0)
        painter.drawRoundedRect(QRectF(QPointF(left, y - 2), QPointF(thumb, y + 2)), 2, 2);

    for (int i = 0; i < count; ++i) {
        const QPointF centre(stopX(i), y);
        const bool reached = i <= position + 0.01;
        painter.setPen(Qt::NoPen);
        painter.setBrush(reached ? active : colors.borderHover);
        painter.drawEllipse(centre, 4, 4);
        if (!m_available.value(i, true)) {
            painter.setPen(QPen(colors.warning, 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(centre, 6.5, 6.5);
        }
    }

    if (count > 0) {
        if (enabled && (m_hovered || hasFocus() || m_dragging)) {
            QColor halo = colors.accent;
            halo.setAlpha(m_dragging ? 110 : 70);
            painter.setPen(Qt::NoPen);
            painter.setBrush(halo);
            painter.drawEllipse(QPointF(thumb, y), HaloRadius, HaloRadius);
        }
        painter.setPen(QPen(enabled ? colors.accentHover : colors.borderNormal, 2));
        painter.setBrush(enabled ? colors.textPrimary : colors.textDisabled);
        painter.drawEllipse(QPointF(thumb, y), ThumbRadius - 1, ThumbRadius - 1);
        painter.setPen(Qt::NoPen);
        painter.setBrush(active);
        painter.drawEllipse(QPointF(thumb, y), 3, 3);
    }

    const int labelTop = HaloRadius * 2 + tokens.metrics.spacing2;
    const int current = qRound(position);
    for (int i = 0; i < count; ++i) {
        QFont labelFont = font();
        labelFont.setBold(i == current);
        painter.setFont(labelFont);
        const int labelWidth = QFontMetrics(labelFont).horizontalAdvance(m_labels[i]);
        const QRectF box(stopX(i) - labelWidth / 2.0 - 1, labelTop, labelWidth + 2, fontMetrics().height());
        QColor text = !enabled ? colors.textDisabled
            : !m_available.value(i, true) ? colors.warning
            : i == current ? colors.textPrimary : colors.textMuted;
        painter.setPen(text);
        painter.drawText(box, Qt::AlignCenter, m_labels[i]);
    }
}
