#pragma once
#include <QWidget>
class QPlainTextEdit;
class PromptEditorWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit PromptEditorWidget(QWidget* parent = nullptr);
    void setLabel(const QString& label);
    void setPlaceholderText(const QString& text);
    QString text() const;
    void setText(const QString& text);
    // Lets the editor grow beyond its three visible lines to fill the room it is given.
    void setExpanding(bool expanding);
signals:
    void textChanged();

private:
    QPlainTextEdit* m_editor;
};
