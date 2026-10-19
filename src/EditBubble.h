#pragma once
#include "ReferenceImageStrip.h"
#include <QWidget>
class QAbstractScrollArea;
class QPushButton;
class QToolButton;
class QVariantAnimation;
class QGraphicsOpacityEffect;
class QualityStepSlider;
namespace Ardoise { class PlainTextEdit; }
// Floating edit panel anchored to the bottom of a scroll area's viewport. It slides
// in when activated and morphs between a compact pill and the full editor.
class EditBubble final : public QWidget
{
    Q_OBJECT
public:
    explicit EditBubble(QAbstractScrollArea* host);
    QualityStepSlider* quality() const { return m_quality; }
    Ardoise::PlainTextEdit* prompt() const { return m_prompt; }
    QPushButton* submitButton() const { return m_submit; }
    QToolButton* attachButton() const { return m_attach; }
    // Sparkles: improves the instruction with the default prompt enhancer.
    QToolButton* improveButton() const { return m_improve; }
    // Reference images sent, in order, after the source image.
    static constexpr int MaximumReferences = ReferenceImageStrip::MaximumReferences;
    QList<QImage> references() const;
    ReferenceImageStrip* referenceStrip() const { return m_referenceStrip; }
    bool addReference(const QImage& image);
    void clearReferences();
    void setActive(bool active);
    bool isActive() const { return m_active; }
    void setExpanded(bool expanded, bool focusPrompt = false);
    bool isExpanded() const { return m_expanded; }
signals:
    // Viewport area covered by the bubble's shape, in viewport coordinates; empty when hidden.
    void coveredRectChanged(const QRect& rect);
    void expandedChanged(bool expanded);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QRect shapeRect(qreal morph) const;
    void relayout();
    QAbstractScrollArea* m_host;
    ReferenceImageStrip* m_referenceStrip;
    QToolButton* m_attach;
    QToolButton* m_improve;
    QWidget* m_panel;
    QToolButton* m_pill;
    QualityStepSlider* m_quality;
    Ardoise::PlainTextEdit* m_prompt;
    QPushButton* m_submit;
    QVariantAnimation* m_reveal;
    QVariantAnimation* m_morph;
    QGraphicsOpacityEffect* m_opacity;
    bool m_active = false;
    bool m_expanded = false;
    qreal m_revealValue = 0;
    qreal m_morphValue = 0;
};
