#pragma once
#include <QWidget>
class QAbstractScrollArea;
class QGraphicsOpacityEffect;
class QVariantAnimation;
class GenerationInspectorWidget;
// The New image card: hosts the generation controls centred over the preview,
// whatever it shows. It rises and fades in when activated, follows the viewport
// on resize and switches the controls to a landscape layout when there is room.
class GenerationCard final : public QWidget
{
    Q_OBJECT
public:
    GenerationCard(GenerationInspectorWidget* content, QAbstractScrollArea* host);
    void setActive(bool active);
    bool isActive() const { return m_active; }
    // Keeps the card below an overlay covering the top of the viewport.
    void setTopInset(int inset);
signals:
    // The close button or Escape asks to put the card away.
    void closeRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void relayout();
    void animateReveal();
    QAbstractScrollArea* m_host;
    GenerationInspectorWidget* m_content;
    QWidget* m_panel;
    QVariantAnimation* m_reveal;
    QGraphicsOpacityEffect* m_opacity;
    bool m_active = true;
    qreal m_revealValue = 0;
    int m_topInset = 0;
};
