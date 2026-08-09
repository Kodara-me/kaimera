#pragma once

#include "CuratedResource.h"

#include <QWidget>

#include <QMap>

#include <optional>

class CuratedResourceManager;
class QLabel;
class QProgressBar;
class QPushButton;

class StartupPreparationWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit StartupPreparationWidget(CuratedResourceManager *resourceManager, QWidget *parent = nullptr);

    void beginPreparation();

signals:
    void preparationFinished();

private:
    struct ResourceRow
    {
        QLabel *statusLabel;
        QProgressBar *progressBar;
        qint64 bytesDownloaded = 0;
        std::optional<qint64> totalBytes;
        CuratedResourceState state = CuratedResourceState::Pending;
    };

    void updateResourceProgress(const CuratedResourceProgress &progress);
    void updateOverallProgress();
    void updateResourceStatus(CuratedResourceId id, CuratedResourceState state);
    void checkPreparationFinished();
    void retryFailedResources();
    QString progressText(const ResourceRow &row) const;
    static int keyFor(CuratedResourceId id);

    CuratedResourceManager *m_resourceManager;
    QList<CuratedResourceId> m_requiredResourceIds;
    QMap<int, ResourceRow> m_rows;
    QLabel *m_errorLabel;
    QProgressBar *m_overallProgressBar;
    QPushButton *m_retryButton;
    bool m_started = false;
    bool m_finished = false;
};
