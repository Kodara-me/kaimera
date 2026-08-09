#include "StartupPreparationWidget.h"

#include "CuratedResourceManager.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

StartupPreparationWidget::StartupPreparationWidget(CuratedResourceManager *resourceManager, QWidget *parent)
    : QWidget(parent)
    , m_resourceManager(resourceManager)
{
    setWindowTitle(tr("Kira Canvas"));
    setMinimumWidth(440);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    auto *titleLabel = new QLabel(tr("Preparing Kira Canvas"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);
    layout->addWidget(new QLabel(tr("Preparing required resources."), this));

    m_requiredResourceIds = m_resourceManager->requiredAtStartupResourceIds();
    for (const CuratedResourceId id : m_requiredResourceIds) {
        auto *resourceLayout = new QVBoxLayout;
        resourceLayout->setSpacing(4);

        auto *headerLayout = new QHBoxLayout;
        auto *nameLabel = new QLabel(m_resourceManager->displayName(id), this);
        auto *statusLabel = new QLabel(tr("Waiting"), this);
        headerLayout->addWidget(nameLabel);
        headerLayout->addStretch();
        headerLayout->addWidget(statusLabel);
        resourceLayout->addLayout(headerLayout);

        auto *progressBar = new QProgressBar(this);
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
        progressBar->setTextVisible(false);
        resourceLayout->addWidget(progressBar);
        layout->addLayout(resourceLayout);

        m_rows.insert(keyFor(id), {statusLabel, progressBar, 0, std::nullopt, CuratedResourceState::Pending});
    }

    auto *overallLabel = new QLabel(tr("Overall progress"), this);
    layout->addWidget(overallLabel);
    m_overallProgressBar = new QProgressBar(this);
    m_overallProgressBar->setRange(0, 100);
    m_overallProgressBar->setValue(0);
    layout->addWidget(m_overallProgressBar);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    layout->addWidget(m_errorLabel);

    m_retryButton = new QPushButton(tr("Retry"), this);
    m_retryButton->setVisible(false);
    connect(m_retryButton, &QPushButton::clicked, this, &StartupPreparationWidget::retryFailedResources);
    layout->addWidget(m_retryButton, 0, Qt::AlignRight);

    connect(m_resourceManager,
            &CuratedResourceManager::progressChanged,
            this,
            &StartupPreparationWidget::updateResourceProgress);
    connect(m_resourceManager, &CuratedResourceManager::downloadFinished, this, [this](CuratedResourceId id) {
        updateResourceStatus(id, CuratedResourceState::Ready);
        checkPreparationFinished();
    });
    connect(m_resourceManager,
            &CuratedResourceManager::downloadFailed,
            this,
            [this](CuratedResourceId id, const QString &errorMessage) {
                updateResourceStatus(id, CuratedResourceState::Failed);
                m_errorLabel->setText(errorMessage);
                m_errorLabel->setVisible(true);
                m_retryButton->setVisible(true);
                m_retryButton->setEnabled(true);
            });
}

void StartupPreparationWidget::beginPreparation()
{
    if (m_started) {
        return;
    }
    m_started = true;

    for (const CuratedResourceId id : m_requiredResourceIds) {
        if (m_resourceManager->isInstalled(id)) {
            updateResourceStatus(id, CuratedResourceState::Ready);
        }
    }
    updateOverallProgress();
    checkPreparationFinished();
    if (!m_finished) {
        m_resourceManager->downloadMissing(m_requiredResourceIds);
    }
}

void StartupPreparationWidget::updateResourceProgress(const CuratedResourceProgress &progress)
{
    const auto iterator = m_rows.find(keyFor(progress.id));
    if (iterator == m_rows.end()) {
        return;
    }

    ResourceRow &row = iterator.value();
    row.bytesDownloaded = progress.bytesDownloaded;
    row.totalBytes = progress.totalBytes;
    row.state = progress.state;
    if (progress.totalBytes.has_value() && *progress.totalBytes > 0) {
        row.progressBar->setRange(0, 100);
        row.progressBar->setValue(progress.percentage.value_or(0));
    } else if (progress.state == CuratedResourceState::Downloading || progress.state == CuratedResourceState::Verifying) {
        row.progressBar->setRange(0, 0);
    }
    row.statusLabel->setText(progressText(row));
    updateOverallProgress();
}

void StartupPreparationWidget::updateOverallProgress()
{
    qint64 expectedTotalBytes = 0;
    qint64 completedBytes = 0;
    for (const CuratedResourceId id : m_requiredResourceIds) {
        const std::optional<qint64> expectedSize = m_resourceManager->expectedFileSize(id);
        if (!expectedSize.has_value() || *expectedSize <= 0) {
            continue;
        }

        expectedTotalBytes += *expectedSize;
        const ResourceRow &row = m_rows.value(keyFor(id));
        if (row.state == CuratedResourceState::Ready) {
            completedBytes += *expectedSize;
        } else {
            completedBytes += std::min(row.bytesDownloaded, *expectedSize);
        }
    }

    if (expectedTotalBytes <= 0) {
        m_overallProgressBar->setRange(0, 0);
        return;
    }
    m_overallProgressBar->setRange(0, 100);
    m_overallProgressBar->setValue(static_cast<int>((completedBytes * 100) / expectedTotalBytes));
}

void StartupPreparationWidget::updateResourceStatus(CuratedResourceId id, CuratedResourceState state)
{
    const auto iterator = m_rows.find(keyFor(id));
    if (iterator == m_rows.end()) {
        return;
    }

    ResourceRow &row = iterator.value();
    row.state = state;
    if (state == CuratedResourceState::Ready) {
        const std::optional<qint64> expectedSize = m_resourceManager->expectedFileSize(id);
        if (expectedSize.has_value()) {
            row.bytesDownloaded = *expectedSize;
            row.totalBytes = expectedSize;
        }
        row.progressBar->setRange(0, 100);
        row.progressBar->setValue(100);
    }
    row.statusLabel->setText(progressText(row));
    updateOverallProgress();
}

void StartupPreparationWidget::checkPreparationFinished()
{
    if (m_finished || !m_started) {
        return;
    }

    for (const CuratedResourceId id : m_requiredResourceIds) {
        if (!m_resourceManager->isInstalled(id)) {
            return;
        }
    }

    m_finished = true;
    emit preparationFinished();
}

void StartupPreparationWidget::retryFailedResources()
{
    m_errorLabel->clear();
    m_errorLabel->setVisible(false);
    m_retryButton->setEnabled(false);
    for (const CuratedResourceId id : m_requiredResourceIds) {
        if (!m_resourceManager->isInstalled(id)) {
            m_resourceManager->download(id);
        }
    }
}

QString StartupPreparationWidget::progressText(const ResourceRow &row) const
{
    switch (row.state) {
    case CuratedResourceState::Pending:
        return tr("Waiting");
    case CuratedResourceState::Downloading:
        if (row.totalBytes.has_value()) {
            return tr("%1 / %2")
                .arg(QLocale().formattedDataSize(row.bytesDownloaded, 1),
                     QLocale().formattedDataSize(*row.totalBytes, 1));
        }
        return tr("Downloading");
    case CuratedResourceState::Verifying:
        return tr("Verifying");
    case CuratedResourceState::Ready:
        return tr("Ready");
    case CuratedResourceState::Failed:
        return tr("Failed");
    }
    return {};
}

int StartupPreparationWidget::keyFor(CuratedResourceId id)
{
    return static_cast<int>(id);
}
