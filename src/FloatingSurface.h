#pragma once
#include <QColor>
#include <QRectF>
class QPainter;
// Shared rendering for Canvas overlays that float above the image workspace:
// a soft layered shadow, a gently lit fill and an accent glow while focused.
namespace FloatingSurface
{
void paint(QPainter& painter, const QRectF& shape, qreal radius, int shadowMargin, bool focused,
    int fillAlpha = 240);
}
