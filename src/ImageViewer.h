#pragma once

#include <QGraphicsView>
#include <QImage>

class QGraphicsPixmapItem;
class QGraphicsScene;
class QGraphicsTextItem;
class QResizeEvent;
class QWheelEvent;

class ImageViewer final : public QGraphicsView
{
public:
    explicit ImageViewer(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void clearImage();
    void fitToWindow();
    void actualSize();
    void zoomIn();
    void zoomOut();
    int zoomPercent() const;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setZoom(qreal zoom);

    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_imageItem;
    QGraphicsTextItem *m_emptyMessage;
    qreal m_zoom = 1.0;
};
