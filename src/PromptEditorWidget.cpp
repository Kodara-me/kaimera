#include "PromptEditorWidget.h"
#include <Ardoise/PlainTextEdit.h>
#include <Ardoise/Theme.h>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QVBoxLayout>

PromptEditorWidget::PromptEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* editor = new Ardoise::PlainTextEdit(this);
    editor->document()->setDocumentMargin(Ardoise::Theme::tokens().metrics.spacing6);
    editor->setReferenceLength(2000);
    editor->setCounterVisible(true);
    m_editor = editor;
    editor->setFixedHeight(editor->heightForLines(Ardoise::Theme::tokens().metrics.promptVisibleLines));
    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    layout->addWidget(m_editor);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &PromptEditorWidget::textChanged);
}

void PromptEditorWidget::setLabel(const QString& label)
{
    setAccessibleName(label);
    m_editor->setAccessibleName(label);
}

void PromptEditorWidget::setPlaceholderText(const QString& text) { m_editor->setPlaceholderText(text); }

QString PromptEditorWidget::text() const { return m_editor->toPlainText(); }

void PromptEditorWidget::setText(const QString& text) { m_editor->setPlainText(text); }

void PromptEditorWidget::setExpanding(bool expanding)
{
    auto* editor = static_cast<Ardoise::PlainTextEdit*>(m_editor);
    const int lines = editor->heightForLines(Ardoise::Theme::tokens().metrics.promptVisibleLines);
    editor->setMinimumHeight(lines);
    editor->setMaximumHeight(expanding ? QWIDGETSIZE_MAX : lines);
    const auto policy = expanding ? QSizePolicy::Expanding : QSizePolicy::Fixed;
    editor->setSizePolicy(editor->sizePolicy().horizontalPolicy(), policy);
    setSizePolicy(sizePolicy().horizontalPolicy(), expanding ? QSizePolicy::Expanding : QSizePolicy::Maximum);
}
