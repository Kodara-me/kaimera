#include "MacPanelSurface.h"

#include <QPainter>
#include <QPainterPath>
#include <QStyle>

MacPanelSurface::MacPanelSurface(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("macWorkspacePanel"));
    setAutoFillBackground(false);
}

void MacPanelSurface::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    const int radius = style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, this) + 10;
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(0, 0, -1, -1), radius, radius);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillPath(path, palette().brush(QPalette::AlternateBase));
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawPath(path);
}
