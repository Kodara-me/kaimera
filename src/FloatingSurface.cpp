#include "FloatingSurface.h"
#include <Ardoise/Theme.h>
#include <QLinearGradient>
#include <QPainter>

void FloatingSurface::paint(QPainter& painter, const QRectF& shape, qreal radius, int shadowMargin, bool focused,
    int fillAlpha)
{
    const auto& colors = Ardoise::Theme::tokens().colors;
    painter.setRenderHint(QPainter::Antialiasing);
    // Soft layered shadow, offset slightly downward for depth.
    QColor shadow = colors.backgroundPrimary.darker(300);
    for (int i = shadowMargin; i > 0; --i) {
        shadow.setAlpha(qRound(70.0 * (shadowMargin - i + 1) / (shadowMargin * shadowMargin)));
        painter.setPen(QPen(shadow, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(shape.adjusted(-i, -i + 2, i, i + 2), radius + i, radius + i);
    }
    QColor fill = colors.backgroundElevated;
    fill.setAlpha(fillAlpha);
    QLinearGradient gradient(shape.topLeft(), shape.bottomLeft());
    gradient.setColorAt(0, fill.lighter(112));
    gradient.setColorAt(qMin(1.0, 120.0 / qMax(1.0, shape.height())), fill);
    gradient.setColorAt(1, fill);
    painter.setBrush(gradient);
    if (focused) {
        QColor glow = colors.accent;
        glow.setAlpha(60);
        painter.setPen(QPen(glow, 4));
        painter.drawRoundedRect(shape, radius, radius);
    }
    painter.setPen(QPen(focused ? colors.borderAccent : colors.borderNormal, 1));
    painter.drawRoundedRect(shape, radius, radius);
}
