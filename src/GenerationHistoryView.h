#pragma once
#include <Ardoise/ThumbnailView.h>
#include <QPersistentModelIndex>
#include <QUuid>
class QTimer;
class QToolButton;
class GenerationHistoryView final : public Ardoise::ThumbnailView
{
    Q_OBJECT
public:
    explicit GenerationHistoryView(QWidget* parent = nullptr);
    void setModel(QAbstractItemModel* model) override;
    // Pending and failed generations hold a place in the strip but cannot be selected.
    static bool isReady(const QModelIndex& index);
    // Angle of the spinning arc drawn on pending entries without a percentage.
    qreal spinAngle() const { return m_spinAngle; }
    // Angle of the accent sweep around pending entries' borders.
    qreal sweepAngle() const { return m_sweepAngle; }
signals:
    void entrySelected(const QUuid& id);
    // The hovered thumbnail's remove button was clicked.
    void removeRequested(const QUuid& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void updateGeometries() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    QModelIndex moveCursor(CursorAction action, Qt::KeyboardModifiers modifiers) override;

private:
    void showRemove(const QModelIndex& index);
    void updateRemove();
    void updateSpin();
    QToolButton* m_remove;
    QPersistentModelIndex m_hovered;
    QTimer* m_spin;
    qreal m_spinAngle = 0;
    qreal m_sweepAngle = 0;
};
