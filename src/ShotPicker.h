#pragma once
#include <QWidget>
class QVariantAnimation;
// Horizontal strip of camera shot tiles, each a small frame cropping a figure
// at its framing (Free has no figure). The selection highlight glides between
// tiles and the selected shot's name and description sit underneath.
class ShotPicker final : public QWidget
{
    Q_OBJECT
public:
    enum class Shot { Free, ExtremeCloseUp, CloseUp, Portrait, Medium, Cowboy, FullBody, Wide };
    explicit ShotPicker(QWidget* parent = nullptr);
    Shot currentShot() const { return Shot(m_current); }
    void setCurrentShot(Shot shot);
    QString shotName(Shot shot) const;
    QString shotDescription(Shot shot) const;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
signals:
    void currentShotChanged(ShotPicker::Shot shot);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    int tilesHeight() const;
    QRectF tileRect(qreal slot) const;
    int slotAt(const QPointF& point) const;
    void select(int index, bool notify);
    int m_current = 0;
    int m_hovered = -1;
    qreal m_highlightSlot = 0;
    QVariantAnimation* m_highlight;
};
