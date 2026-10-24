#pragma once
#include <QGraphicsView>
#include <QHash>
#include <QImage>
class QGraphicsPixmapItem;
class QMediaPlayer;
class QRubberBand;
class GenerationPreviewWidget final : public QGraphicsView
{
    Q_OBJECT
public:
    explicit GenerationPreviewWidget(QWidget* parent = nullptr);
    void setImage(const QImage& image);
    // Plays a video in a loop at once, with the same zoom and fit as images; a click pauses or
    // resumes it. The badge shows its size, frame count and frames per second.
    void setAnimation(const QByteArray& video, const QSize& size, int frames, int fps);
    bool hasAnimation() const { return m_player != nullptr; }
    bool isPlaying() const;
    // The frame shown last, or null.
    QImage currentFrame() const { return m_frame; }
    void setOverlayVisible(bool visible);
    // Floating content over the viewport, in viewport coordinates, one rect per source.
    // The dimensions badge stays in the bottom-left corner and rises above them only
    // while they overlap.
    void setOverlayObstacle(const QRect& rect, const QObject* source = nullptr);
    // Dimensions badge geometry in viewport coordinates; empty when there is no badge.
    QRect overlayRect() const;
    void clear();
    void fitImage();
    void fitToView() { fitImage(); }
    void setZoom(double factor);
    double zoom() const { return transform().m11(); }
    QImage currentImage() const { return m_image; }
    void actualSize();
    void zoomIn();
    void zoomOut();
    bool isFitToView() const { return m_fit; }
    bool cropEnabled() const { return m_cropEnabled; }
    void setCropEnabled(bool enabled);
signals:
    void viewModeChanged(bool fit);
    void cropModeChanged(bool enabled);
    void imageCropped(const QImage& image);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void zoom(double factor);
    void updateOverlay();
    void showFrame(const QImage& frame);
    QMediaPlayer* m_player = nullptr;
    QImage m_frame;
    QSize m_animationSize;
    int m_animationFrames = 0;
    int m_animationFps = 0;
    QPoint m_pressPosition;
    QRect cropPixels(const QPoint& point) const;
    QString m_overlayText;
    bool m_overlayVisible = true;
    QHash<const QObject*, QRect> m_overlayObstacles;
    QFont overlayFont() const;
    QRect m_cropPixels;
    QGraphicsPixmapItem* m_item = nullptr;
    bool m_fit = false;
    QImage m_image;
    bool m_cropEnabled = false;
    bool m_cropping = false;
    QPointF m_cropStart;
    QRubberBand* m_cropBand = nullptr;
};
