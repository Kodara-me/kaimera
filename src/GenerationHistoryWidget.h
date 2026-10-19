#pragma once
#include <QImage>
#include <QUuid>
#include <QWidget>
class QLabel;
class QToolButton;
class GenerationHistoryModel;
class GenerationHistoryView;

// Owns the in-memory history strip; no file paths or persistence.
class GenerationHistoryWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit GenerationHistoryWidget(QWidget* parent = nullptr);
    void appendImage(const QImage& image, const QString& label, const QString& prompt = {},
        const QString& negativePrompt = {});
    // A queued generation's place in the strip; the model then follows and completes it
    // without changing the selection.
    void appendPending(const QUuid& id, const QString& label, const QString& status);
    void clearSelection();
    GenerationHistoryModel* model() const { return m_model; }
    GenerationHistoryView* view() const { return m_view; }
    void removeImage(const QUuid& id);
signals:
    void imageSelected(const QImage& image);

private:
    void updateHistory();
    // The next row with an image after row in direction step, or -1.
    int readyRow(int row, int step) const;
    GenerationHistoryModel* m_model;
    GenerationHistoryView* m_view;
    QLabel* m_heading;
    QToolButton* m_previous;
    QToolButton* m_next;
};
