#pragma once
#include <QWidget>
#include <nukleus/ImageTypes.h>
class QBoxLayout;
class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QToolButton;
class PromptEditorWidget;
class ReferenceImageStrip;
class ShotPicker;
class GenerationParametersWidget;
class GenerationInspectorWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit GenerationInspectorWidget(QWidget* parent = nullptr);
    GenerationParametersWidget* parameters() const { return m_parameters; }
    // Reference images guiding the generation, kept in memory with the draft.
    ReferenceImageStrip* references() const { return m_references; }
    // Camera shot guiding the scene's framing; not submitted yet.
    ShotPicker* shotPicker() const { return m_shot; }
    QToolButton* attachButton() const { return m_attach; }
    QToolButton* attachIdentityButton() const { return m_attachIdentity; }
    // An attached identity's instruction, appended to the submitted prompt but never shown.
    QString identityPrompt() const { return m_identityPrompt; }
    // Whether the selected preset's model uses reference images; when not, attached ones show
    // as ignored, none can be added and Generate leaves them (and the identity's instruction) out.
    bool referencesUsed() const;
    void setIdentityPrompt(const QString& prompt) { m_identityPrompt = prompt; }
    void resetDraft();
    void clearPrompts();
    QString positivePrompt() const;
    Nukleus::ImageGenerationRequest generationRequest() const;
    // An unavailable Generate explains itself with reason, or a generic tooltip.
    void setGenerationAvailable(bool available, const QString& reason = {});
    // Sparkles needs an available enhancer (else reason explains why) and some prompt text.
    void setPromptAssistAvailable(bool available, const QString& reason = {});
    void setPrompts(const QString& positive, const QString& negative);
    // Retain the submitted draft in memory and clear both editors after acceptance.
    void rememberSubmittedPrompts(const QString& positive, const QString& negative);
    QSize sizeHint() const override;
    // Places the prompts and the look side by side instead of stacking them.
    void setLandscape(bool landscape);
    bool isLandscape() const;
signals:
    void draftChanged();
    void generationRequested();
    // Asks to improve the current prompts with the default prompt enhancer.
    void promptAssistRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateReferenceAvailability();
    QWidget* m_content;
    QBoxLayout* m_columns;
    QVBoxLayout* m_prompts;
    QHBoxLayout* m_footer;
    QLabel* m_shortcutHint;
    QToolButton* m_attach;
    QToolButton* m_attachIdentity;
    QString m_identityPrompt;
    ReferenceImageStrip* m_references;
    QToolButton* m_assist;
    QToolButton* m_restore;
    QString m_lastPositive;
    QString m_lastNegative;
    PromptEditorWidget* m_positive;
    PromptEditorWidget* m_negative;
    GenerationParametersWidget* m_parameters;
    ShotPicker* m_shot;
    QPushButton* m_generate;
};
