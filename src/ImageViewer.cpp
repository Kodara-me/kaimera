#include "ImageViewer.h"

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>

ImageViewer::ImageViewer(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
    , m_imageItem(m_scene->addPixmap(QPixmap()))
    , m_emptyMessage(m_scene->addText(tr("Open an image to begin")))
{
    setScene(m_scene);
    setBackgroundBrush(palette().brush(QPalette::Window));
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setAlignment(Qt::AlignCenter);
    m_emptyMessage->setDefaultTextColor(palette().color(QPalette::Text));
    clearImage();
}

void ImageViewer::setImage(const QImage &image)
{
    m_imageItem->setPixmap(QPixmap::fromImage(image));
    m_scene->setSceneRect(m_imageItem->boundingRect());
    m_emptyMessage->hide();
    m_zoom = 1.0;
    resetTransform();
    centerOn(m_imageItem);
}

void ImageViewer::clearImage()
{
    m_imageItem->setPixmap(QPixmap());
    m_emptyMessage->show();
    m_scene->setSceneRect(m_emptyMessage->boundingRect());
    resetTransform();
    centerOn(m_emptyMessage);
}

void ImageViewer::fitToWindow()
{
    actualSize();
}

void ImageViewer::actualSize()
{
    setZoom(1.0);
}

void ImageViewer::zoomIn()
{
    setZoom(m_zoom * 1.2);
}

void ImageViewer::zoomOut()
{
    setZoom(m_zoom / 1.2);
}

int ImageViewer::zoomPercent() const
{
    return qRound(m_zoom * 100.0);
}

void ImageViewer::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_imageItem->pixmap().isNull()) {
        centerOn(m_emptyMessage);
    }
}

void ImageViewer::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        if (event->angleDelta().y() > 0) {
            zoomIn();
        } else if (event->angleDelta().y() < 0) {
            zoomOut();
        }
        event->accept();
        return;
    }

    QGraphicsView::wheelEvent(event);
}

void ImageViewer::setZoom(qreal zoom)
{
    if (m_imageItem->pixmap().isNull()) {
        return;
    }

    const QPointF viewCenter = mapToScene(viewport()->rect().center());
    m_zoom = std::clamp(zoom, 0.1, 8.0);
    resetTransform();
    scale(m_zoom, m_zoom);
    centerOn(viewCenter);
}
