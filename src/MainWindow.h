#pragma once
#include <Ardoise/MainWindow.h>
#include <QHash>
#include <QPointer>
#include <QImage>
#include <QUuid>
#include <optional>
#include <nukleus/ImageTypes.h>
#include "SparklePlanner.h"
class QPlainTextEdit;
class QCloseEvent;
class QLabel;
class QAction;
class CanvasServiceController;
class GenerationInspectorWidget;
class GenerationWorkspaceWidget;
class IdentityPanel;
class JobActivityIndicator;
class MainWindow final : public Ardoise::MainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void showServicePreferences();
    void restoreWindowState();
    void saveWindowState();
    void showMessage(const QString& title, const QString& message);
    void openImage();
    void saveImage();
    CanvasServiceController* m_serviceClient = nullptr;
    GenerationInspectorWidget* m_panel = nullptr;
    GenerationWorkspaceWidget* m_workspace = nullptr;
    QAction* m_save = nullptr;
    IdentityPanel* m_identity = nullptr;
    // Indicator showing the active job: in the identity panel for portraits, the workspace otherwise.
    QPointer<JobActivityIndicator> m_activity;
    void startActivity(JobActivityIndicator* indicator, const QString& title);
    // Kira Sparkle is planning: its progress shows in the chat, not in a pill.
    bool m_sparklePlanning = false;
    // Kira Sparkle's short memory: the last exchanges (request, then the JSON plan answered,
    // which keeps its prompts and instructions exactly), forgotten by New chat.
    static constexpr int SparkleMemoryExchanges = 6;
    QList<Nukleus::TextChatMessage> m_sparkleHistory;
    // What Kira Sparkle can do now: the selected image, and the presets and editing qualities
    // whose models are available.
    SparklePlanner::Context sparkleContext(const QImage& image, bool base, int references) const;
    // The chat text for Kira Sparkle's first answer to a request.
    QString sparkleAnswer(const SparklePlanner::Plan& plan, const SparklePlanner::Context& context) const;
    void rememberSparkleExchange(const QString& request, const QJsonObject& answer);
    // A creation Kira Sparkle proposed, waiting for its button or an acceptance; last says
    // whether it is still the latest answer, which a written acceptance refers to.
    struct SparkleProposal
    {
        QUuid id;
        SparklePlanner::Plan plan;
        QList<QImage> references;
        bool last = true;
    };
    std::optional<SparkleProposal> m_sparkleProposal;
    // Creations Kira Sparkle queued, with their prompt: the delivered image becomes the base.
    QHash<QUuid, QString> m_sparkleCreations;
    // The base image Kira Sparkle set from a creation, and its prompt, while it stays the base.
    qint64 m_sparkleBaseKey = 0;
    QString m_sparkleBasePrompt;
    QPointer<QPlainTextEdit> m_editTarget;
};
