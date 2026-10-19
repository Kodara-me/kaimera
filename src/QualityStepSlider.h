#pragma once
#include <QSlider>
#include <QStringList>
class QVariantAnimation;
// A horizontal slider with a few named stops. The thumb glides between stops and
// each stop shows its label underneath; unavailable stops are flagged.
class QualityStepSlider final : public QSlider
{
    Q_OBJECT
public:
    explicit QualityStepSlider(QWidget* parent = nullptr);
    void setSteps(const QStringList& labels, const QList<bool>& available, const QStringList& toolTips);
    QString currentLabel() const { return m_labels.value(value()); }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void sliderChange(SliderChange change) override;

private:
    int padding() const;
    int trackY() const;
    qreal stopX(qreal position) const;
    qreal positionAt(qreal x) const;
    QStringList m_labels;
    QList<bool> m_available;
    QStringList m_toolTips;
    QVariantAnimation* m_animation;
    qreal m_position = 0;
    bool m_dragging = false;
    bool m_hovered = false;
};
