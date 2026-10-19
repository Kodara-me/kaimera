#pragma once
#include "ReferenceImageStrip.h"
#include <QUuid>
#include <QWidget>
class QAbstractScrollArea;
class QGraphicsOpacityEffect;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QVariantAnimation;
namespace Ardoise { class PlainTextEdit; }
// Kira Sparkle: a round Sparkles bubble in the bottom-right corner of a scroll area's
// viewport that toggles a chat panel growing to its left, leaving the bubble visible: a
// header with New chat, the conversation, a two-line request field and a toolbar with the
// attach button, the base image and up to four reference images on the left, then the
// engine button (creation preset monogram and editing quality level, chosen from its menu)
// and Send on the right. Enter sends, Shift+Enter inserts a
// newline; Escape, the bubble or a click outside collapses it. The conversation shows
// Kira Sparkle thinking and each image it queues, in the image's ratio, from its progress
// to the delivered image.
class KiraSparkle final : public QWidget
{
    Q_OBJECT
public:
    explicit KiraSparkle(QAbstractScrollArea* host);
    Ardoise::PlainTextEdit* prompt() const { return m_prompt; }
    QPushButton* sendButton() const { return m_send; }
    // Opens a menu choosing the base image (from disk or the displayed image) or adding
    // reference images (from disk, the displayed image or an identity).
    QToolButton* attachButton() const { return m_attach; }
    // Whether an image is displayed, to use as base or reference.
    void setContextAvailable(bool available) { m_contextAvailable = available; }
    // The base image (one at most, unnumbered with an accent border), shown before the references.
    ReferenceImageStrip* baseStrip() const { return m_baseStrip; }
    QImage baseImage() const { return m_baseStrip->references().value(0); }
    // Replaces the base image; a null image removes it.
    void setBaseImage(const QImage& image);
    // The reference images (up to four, numbered).
    ReferenceImageStrip* referenceStrip() const { return m_referenceStrip; }
    QList<QImage> attachments() const { return m_referenceStrip->references(); }
    bool addAttachment(const QImage& image) { return m_referenceStrip->addReference(image); }
    // Send also requires a nonblank request; disabled sends keep the draft and show reason.
    void setSendEnabled(bool enabled, const QString& reason = {});
    // Puts back a request that could not be carried out, unless a new one was started.
    void restoreRequest(const QString& request);
    // Adds Kira Sparkle's answer to the conversation.
    void addReply(const QString& text);
    // Adds an answer proposing something to carry out, with details as its tooltip and a
    // button labelled action that emits proposalAccepted with the returned id.
    QUuid addProposal(const QString& text, const QString& details, const QString& action);
    // Disables a proposal's button, now reading state (carried out, or replaced).
    void closeProposal(const QUuid& id, const QString& state);
    // Shows Kira Sparkle working on the request as a progress message (-1: unknown);
    // stopThinking removes it.
    void setThinking(const QString& status, int percentage = -1);
    void stopThinking();
    // An image Kira Sparkle queued, keyed by its history ticket and drawn in the given
    // size's ratio. Unknown tickets are ignored.
    void addResult(const QUuid& id, const QSize& size, const QString& status);
    void setResultProgress(const QUuid& id, const QString& status, int percentage);
    void completeResult(const QUuid& id, const QImage& image);
    void failResult(const QUuid& id, const QString& message);
    // Empties the conversation and its attachments and greets again; the draft stays.
    void newChat();
    // A creation preset or editing quality offered by the engine button, left of Send;
    // issue says why it cannot run now (empty when it can).
    struct Choice
    {
        QString id;
        QString name;
        QString issue;
        bool references = true; // A preset whose model uses reference images.
    };
    void setPresets(const QList<Choice>& presets, const QString& current);
    void setQualities(const QList<Choice>& qualities, const QString& current);
    void setEngineLocked(bool locked);
    QToolButton* engineButton() const { return m_engine; }
    // Short codes for preset names: the first two letters, else the first word initials,
    // else the first letter and a number, unique in order.
    static QStringList monograms(const QStringList& names);
    void setActive(bool active);
    bool isActive() const { return m_active; }
    void setExpanded(bool expanded, bool focusPrompt = false);
    bool isExpanded() const { return m_expanded; }
signals:
    void submitted(const QString& request, const QImage& base, const QList<QImage>& attachments);
    // New chat emptied the conversation, so whatever remembers it forgets it too.
    void conversationCleared();
    void proposalAccepted(const QUuid& id);
    void expandedChanged(bool expanded);
    // A delivered result was double-clicked, or asks for its context menu at globalPos.
    void resultOpened(const QUuid& id, const QImage& image);
    void resultMenuRequested(const QUuid& id, const QImage& image, const QPoint& globalPos);
    // The attach menu asks for a base image from disk or the displayed one, or for reference
    // images from disk, the displayed one or an identity.
    void baseFileRequested();
    void baseContextRequested();
    void referenceFilesRequested();
    void referenceContextRequested();
    void referenceIdentityRequested();
    // The engine button's menu chose a creation preset or an editing quality.
    void presetSelected(const QString& id);
    void qualitySelected(const QString& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QRect bubbleRect() const;
    QRect panelRect(qreal morph) const;
    void relayout();
    void updateSendButton();
    void send();
    void addMessage(bool fromUser, const QString& text);
    void addItem(QWidget* item, Qt::Alignment alignment);
    QWidget* result(const QUuid& id) const;
    void fitMessages();
    void updateEngine();
    // Whether attached references could serve neither creating nor editing now.
    bool referencesUseless() const;
    void showEngineMenu();
    void showAttachMenu();
    bool m_contextAvailable = false;
    QAbstractScrollArea* m_host;
    QToolButton* m_engine;
    QList<Choice> m_presets;
    QString m_currentPreset;
    QList<Choice> m_qualities;
    QString m_currentQuality;
    QWidget* m_panel;
    QToolButton* m_bubble;
    QToolButton* m_newChat;
    QWidget* m_thinking = nullptr;
    QScrollArea* m_transcript;
    QVBoxLayout* m_messages;
    ReferenceImageStrip* m_baseStrip;
    ReferenceImageStrip* m_referenceStrip;
    QToolButton* m_attach;
    Ardoise::PlainTextEdit* m_prompt;
    QPushButton* m_send;
    QVariantAnimation* m_reveal;
    QVariantAnimation* m_morph;
    QGraphicsOpacityEffect* m_opacity;
    bool m_active = false;
    bool m_expanded = false;
    bool m_sendEnabled = true;
    QString m_sendIssue;
    bool m_focusAfterMorph = false;
    qreal m_revealValue = 0;
    qreal m_morphValue = 0;
};
