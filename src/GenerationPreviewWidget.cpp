#include "GenerationPreviewWidget.h"
#include <Ardoise/Theme.h>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QRubberBand>
#include <QPainter>
#include <QLocale>
#include <algorithm>
#include <cmath>

GenerationPreviewWidget::GenerationPreviewWidget(QWidget* parent)
    : QGraphicsView(parent)
{
    setObjectName(QStringLiteral("generationPreview"));
    setScene(new QGraphicsScene(this));
    setBackgroundBrush(Ardoise::Theme::tokens().colors.backgroundPrimary);
    setFrameShape(QFrame::StyledPanel);
    // Keep the rectangular viewport from painting over the rounded frame corners.
    const int inset = Ardoise::Theme::tokens().metrics.borderWidth;
    setViewportMargins(inset, inset, inset, inset);
    setRenderHint(QPainter::SmoothPixmapTransform);
    // Repaint the fixed badge instead of scrolling its painted pixels with the image.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setAccessibleName(tr("Image preview"));
    m_cropBand = new QRubberBand(QRubberBand::Rectangle, viewport());
}

void GenerationPreviewWidget::setImage(const QImage& image)
{
    clear();
    if (image.isNull())
        return;
    m_image = image;
    auto pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(1);
    m_item = scene()->addPixmap(pixmap);
    m_item->setTransformationMode(Qt::SmoothTransformation);
    scene()->setSceneRect(m_item->boundingRect());
    if (m_fit) fitImage(); else actualSize();
}

void GenerationPreviewWidget::clear()
{
    setCropEnabled(false);
    scene()->clear();
    m_item = nullptr;
    m_image = { };
    scene()->setSceneRect(QRectF());
    resetTransform();
    emit viewModeChanged(m_fit);
    updateOverlay();
}

void GenerationPreviewWidget::fitImage()
{
    m_fit = true;
    emit viewModeChanged(true);
    if (!m_item || viewport()->width() < 1 || viewport()->height() < 1)
        return;
    // Explicit fitting avoids scrollbar/resize recursion from fitInView().
    resetTransform();
    const auto rect = m_item->boundingRect();
    const double factor = std::min(std::max(1, viewport()->width() - 4) / rect.width(),
        std::max(1, viewport()->height() - 4) / rect.height());
    scale(factor, factor);
    centerOn(m_item);
    updateOverlay();
}

void GenerationPreviewWidget::actualSize()
{
    m_fit = false;
    emit viewModeChanged(false);
    resetTransform();
    if (m_item)
        centerOn(m_item);
    updateOverlay();
}

void GenerationPreviewWidget::zoom(double factor)
{
    if (!m_item)
        return;
    setZoom(transform().m11() * factor);
}

void GenerationPreviewWidget::setZoom(double factor)
{
    if (!m_item || !std::isfinite(factor))
        return;
    m_fit = false;
    emit viewModeChanged(false);
    const double current = transform().m11();
    const double target = std::clamp(factor, 0.01, 64.0);
    scale(target / current, target / current);
    updateOverlay();
}

void GenerationPreviewWidget::zoomIn() { zoom(1.25); }

void GenerationPreviewWidget::zoomOut() { zoom(0.8); }

void GenerationPreviewWidget::resizeEvent(QResizeEvent* event)
{
    if (m_cropping) setCropEnabled(false);
    QGraphicsView::resizeEvent(event);
    if (m_fit)
        fitImage();
}

void GenerationPreviewWidget::wheelEvent(QWheelEvent* event)
{
    if (m_cropping) { event->accept(); return; }
    if (event->modifiers().testFlag(Qt::ControlModifier))
    {
        if (event->angleDelta().y() > 0)
            zoomIn();
        else if (event->angleDelta().y() < 0)
            zoomOut();
        event->accept();
    }
    else
        QGraphicsView::wheelEvent(event);
}

void GenerationPreviewWidget::setCropEnabled(bool enabled)
{
    m_cropEnabled = enabled && !m_image.isNull();
    m_cropping = false;
    m_cropBand->hide();
    m_cropPixels = {};
    setDragMode(m_cropEnabled ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
    viewport()->setCursor(m_cropEnabled ? Qt::CrossCursor : Qt::OpenHandCursor);
    if (m_cropEnabled) setFocus(Qt::OtherFocusReason);
    emit cropModeChanged(m_cropEnabled);
    updateOverlay();
}

void GenerationPreviewWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_cropEnabled && event->button() == Qt::LeftButton) {
        const QPointF point = mapToScene(event->position().toPoint());
        if (sceneRect().contains(point)) {
            m_cropStart = point;
            m_cropping = true;
            m_cropPixels = {};
            m_cropBand->setGeometry(QRect(event->position().toPoint(), QSize()));
            m_cropBand->show();
            updateOverlay();
        }
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void GenerationPreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_cropping) {
        const QPointF point = mapToScene(event->position().toPoint());
        const QRectF rect = QRectF(m_cropStart, point).normalized().intersected(sceneRect());
        m_cropBand->setGeometry(mapFromScene(rect).boundingRect());
        m_cropPixels = cropPixels(event->position().toPoint());
        updateOverlay();
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void GenerationPreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_cropping && event->button() == Qt::LeftButton) {
        const QRectF selection = QRectF(m_cropStart, mapToScene(event->position().toPoint()))
            .normalized().intersected(sceneRect());
        const QRect pixels = cropPixels(event->position().toPoint());
        m_cropping = false;
        m_cropBand->hide();
        updateOverlay();
        if (selection.width() > 0 && selection.height() > 0 && !pixels.isEmpty()) {
            const QImage cropped = m_image.copy(pixels);
            setCropEnabled(false);
            emit imageCropped(cropped);
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void GenerationPreviewWidget::keyPressEvent(QKeyEvent* event)
{
    if (m_cropEnabled && event->key() == Qt::Key_Escape) {
        setCropEnabled(false);
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

QRect GenerationPreviewWidget::cropPixels(const QPoint& point) const
{
    return QRectF(m_cropStart, mapToScene(point)).normalized().intersected(sceneRect())
        .toAlignedRect().intersected(m_image.rect());
}

void GenerationPreviewWidget::updateOverlay()
{
    m_overlayText = !m_overlayVisible || m_image.isNull() ? QString() : tr("%1 × %2 px · %3%")
        .arg(m_image.width()).arg(m_image.height()).arg(QLocale().toString(zoom() * 100, 'f', 1));
    if (m_overlayVisible && m_cropping)
        m_overlayText.prepend(tr("Crop: %1 × %2 px").arg(m_cropPixels.width()).arg(m_cropPixels.height()) + '\n');
    setAccessibleDescription(m_overlayText);
    viewport()->update();
}

void GenerationPreviewWidget::setOverlayVisible(bool visible)
{
    m_overlayVisible = visible;
    updateOverlay();
}

void GenerationPreviewWidget::setOverlayObstacle(const QRect& rect, const QObject* source)
{
    if (m_overlayObstacles.value(source) == rect)
        return;
    m_overlayObstacles.insert(source, rect);
    viewport()->update();
}
QFont GenerationPreviewWidget::overlayFont() const
{
    QFont caption = font();
    if (caption.pointSizeF() > 0)
        caption.setPointSizeF(qMax(1.0, caption.pointSizeF() + Ardoise::Theme::tokens().typography.captionOffset));
    return caption;
}

QRect GenerationPreviewWidget::overlayRect() const
{
    if (m_overlayText.isEmpty()) return {};
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    const QRect textBounds = QFontMetrics(overlayFont()).boundingRect(
        QRect(0, 0, viewport()->width(), viewport()->height()), Qt::AlignLeft, m_overlayText);
    const QSize size = textBounds.size() + QSize(metrics.spacing8 * 2, metrics.spacing4 * 2);
    QRect box(QPoint(metrics.spacing8, viewport()->height() - size.height() - metrics.spacing8), size);
    // Each rise only moves the badge up, so this ends above every overlapping obstacle.
    for (bool moved = true; moved;) {
        moved = false;
        for (const QRect& obstacle : m_overlayObstacles)
            if (box.adjusted(0, -metrics.spacing8, metrics.spacing8, 0).intersects(obstacle)) {
                box.moveBottom(obstacle.top() - metrics.spacing8);
                moved = true;
            }
    }
    return box;
}

void GenerationPreviewWidget::paintEvent(QPaintEvent* event)
{
    QGraphicsView::paintEvent(event);
    if (m_overlayText.isEmpty()) return;
    const auto& tokens = Ardoise::Theme::tokens();
    const auto& metrics = tokens.metrics;
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(overlayFont());
    const QRect box = overlayRect();
    QColor background = tokens.colors.backgroundElevated;
    background.setAlpha(220);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(box, metrics.radiusSmall, metrics.radiusSmall);
    painter.setPen(tokens.colors.textPrimary);
    painter.drawText(box.adjusted(metrics.spacing8, metrics.spacing4, -metrics.spacing8, -metrics.spacing4),
        Qt::AlignLeft | Qt::AlignVCenter, m_overlayText);
}
