#pragma once
#include <QSize>
#include <QWidget>
class QVariantAnimation;
// Horizontal strip of aspect-ratio tiles drawn to scale. A landscape/portrait
// switch morphs every tile into its rotated counterpart, and the selection
// highlight glides between tiles.
class ResolutionPicker final : public QWidget
{
    Q_OBJECT
public:
    struct Option
    {
        QSize size;
        QString ratio; // Target ratio, for example "16:9".
        QString name;
        QString text; // Full description for tooltips and accessibility.
    };
    explicit ResolutionPicker(QWidget* parent = nullptr);
    void setOptions(const QList<Option>& options, int current);
    void setCurrentIndex(int index);
    int currentIndex() const { return m_current; }
    bool isPortrait() const { return m_portrait; }
    void setPortrait(bool portrait);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
signals:
    void currentIndexChanged(int index);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QList<int> visibleOptions() const;
    int counterpart(int index) const;
    int tilesHeight() const;
    QRectF tileRect(qreal slot, int count) const;
    QRectF toggleRect(bool portrait) const;
    int slotAt(const QPointF& point) const;
    void select(int index, bool notify);
    void animateHighlight();
    QList<Option> m_options;
    int m_current = -1;
    int m_hovered = -1;
    bool m_portrait = false;
    qreal m_highlightSlot = 0;
    qreal m_flip = 1;
    QVariantAnimation* m_highlight;
    QVariantAnimation* m_flipAnimation;
};
