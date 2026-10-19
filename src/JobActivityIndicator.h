#pragma once
#include <QWidget>
class QGraphicsOpacityEffect;
class QLabel;
class QTimer;
class QToolButton;
class QVariantAnimation;
// Discreet floating status pill anchored to the top centre of a host (a scroll
// area's viewport, or the host itself). It slides in when a job starts, follows
// its stage and percentage, fades out after success and keeps failures, with
// their full selectable message, until dismissed or the next job starts.
class JobActivityIndicator final : public QWidget
{
    Q_OBJECT
public:
    enum class State { Idle, Running, Succeeded, Failed };
    explicit JobActivityIndicator(QWidget* host);
    void start(const QString& title);
    // A percentage of -1 means the progress is unknown.
    void setProgress(const QString& status, int percentage);
    void finish();
    void fail(const QString& title, const QString& message);
    void dismiss();
    State state() const { return m_state; }
    int percentage() const { return m_percentage; }
    QString title() const;
    QString status() const;
signals:
    // Height of the host area covered by the pill, including its margin.
    void coveredHeightChanged(int height);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void reveal(bool visible);
    void relayout();
    void updateStatusText();
    QRect area() const;
    QWidget* m_host;
    QWidget* m_ring;
    QLabel* m_title;
    QLabel* m_status;
    QLabel* m_percentageLabel;
    QToolButton* m_dismiss;
    QTimer* m_hideTimer;
    QVariantAnimation* m_reveal;
    QVariantAnimation* m_spin;
    QVariantAnimation* m_progress;
    QGraphicsOpacityEffect* m_opacity;
    QString m_statusText;
    State m_state = State::Idle;
    int m_percentage = -1;
    int m_fitWidth = 0;
    qreal m_revealValue = 0;
    qreal m_displayedProgress = 0;
    qreal m_spinAngle = 0;
    friend class ActivityRing;
};
