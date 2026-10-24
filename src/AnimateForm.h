#pragma once
#include <QImage>
#include <QWidget>
#include <optional>
class QAbstractButton;
class QAbstractScrollArea;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QGraphicsOpacityEffect;
class QLabel;
class QPushButton;
class QToolButton;
class QVariantAnimation;
class ReferenceImageStrip;
namespace Ardoise { class PlainTextEdit; }
// Floating Animate form over the bottom of the preview, in the edit bubble's style: a motion
// prompt, a paperclip attaching one end image, an Animate button, and, when they apply, a Loop
// switch, a length choice with its duration, and an animation model selector. Hiding it keeps its draft.
class AnimateForm final : public QWidget
{
    Q_OBJECT
public:
    // An installed animation model the form can choose, with its pingPong default.
    struct Model
    {
        QString id;
        QString label;
        bool pingPong = false;
        // The model's own defaults; absent when it sets none.
        std::optional<int> videoFrames;
        std::optional<int> fps;
    };
    explicit AnimateForm(QAbstractScrollArea* host);
    // The toolbar's Animate icon, painted in the theme's colours (accent for the form's header).
    static QIcon icon(bool accent = false);
    Ardoise::PlainTextEdit* prompt() const { return m_prompt; }
    QPushButton* submitButton() const { return m_submit; }
    QToolButton* attachButton() const { return m_attach; }
    ReferenceImageStrip* endImageStrip() const { return m_endImage; }
    QCheckBox* loopSwitch() const { return m_loop; }
    QComboBox* modelSelector() const { return m_model; }
    // The attached end image, or null.
    QImage endImage() const;
    bool setEndImage(const QImage& image);
    // The models with all their files; the selector shows only with more than one. The selected
    // model is kept by ID; a new selection resets the Loop switch to the model's default.
    void setModels(const QList<Model>& models);
    QString modelId() const;
    // Shows the Loop switch when the service can make ping-pong loops.
    void setPingPongAvailable(bool available);
    bool pingPongAvailable() const { return m_pingPongAvailable; }
    bool loop() const;
    // The chosen length in frames (Short, Medium or Long) and the frame rate it plays at: the
    // selected model's fps default, else 16.
    int frameCount() const;
    int frameRate() const;
    QAbstractButton* lengthButton(int index) const;
    QLabel* lengthSummary() const { return m_summary; }
    // Whether submitting is possible apart from the prompt, with the reason when it is not.
    void setSubmitAvailable(bool available, const QString& issue = {});
    // Empties the prompt and removes the end image.
    void clearDraft();
    void setActive(bool active);
    bool isActive() const { return m_active; }
signals:
    // Animate was clicked, or Enter pressed, with a prompt and nothing preventing it.
    void submitted();
    // The close button or Escape; the draft stays.
    void closeRequested();
    void endImageRequested();
    // Viewport area covered by the form, in viewport coordinates; empty when hidden.
    void coveredRectChanged(const QRect& rect);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void relayout();
    void updateSubmit();
    void selectModel(int index);
    void updateSummary();
    QAbstractScrollArea* m_host;
    QWidget* m_panel;
    ReferenceImageStrip* m_endImage;
    QToolButton* m_attach;
    Ardoise::PlainTextEdit* m_prompt;
    QPushButton* m_submit;
    QCheckBox* m_loop;
    QComboBox* m_model;
    QButtonGroup* m_length;
    QLabel* m_summary;
    QVariantAnimation* m_reveal;
    QGraphicsOpacityEffect* m_opacity;
    QList<Model> m_models;
    bool m_pingPongAvailable = false;
    bool m_submitAvailable = false;
    QString m_submitIssue;
    bool m_active = false;
    qreal m_revealValue = 0;
};
