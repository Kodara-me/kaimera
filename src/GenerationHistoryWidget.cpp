#include "GenerationHistoryWidget.h"
#include "GenerationHistoryModel.h"
#include "GenerationHistoryView.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/Theme.h>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

GenerationHistoryWidget::GenerationHistoryWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Ardoise::Theme::tokens().metrics.spacing6);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* heading = new QHBoxLayout;
    m_heading = new QLabel(this);
    heading->addWidget(m_heading, 1);
    m_previous = new Ardoise::IconButton(Ardoise::Icon::ChevronLeft, tr("Previous image"), this);
    m_previous->setObjectName(QStringLiteral("historyPrevious"));
    heading->addWidget(m_previous);
    m_next = new Ardoise::IconButton(Ardoise::Icon::ChevronRight, tr("Next image"), this);
    m_next->setObjectName(QStringLiteral("historyNext"));
    heading->addWidget(m_next);
    layout->addLayout(heading);
    m_model = new GenerationHistoryModel(this);
    m_view = new GenerationHistoryView(this);
    m_view->setModel(m_model);
    layout->addWidget(m_view);
    connect(m_view, &GenerationHistoryView::entrySelected, this,
        [this](const QUuid& id)
        {
            updateHistory();
            emit imageSelected(m_model->image(id));
        });
    connect(m_view, &GenerationHistoryView::removeRequested, this, &GenerationHistoryWidget::removeImage);
    connect(m_previous, &QToolButton::clicked, this,
        [this]
        {
            const int row = m_view->currentIndex().row();
            if (const int previous = row < 0 ? -1 : readyRow(row, -1); previous >= 0)
                m_view->setCurrentIndex(m_model->index(previous));
        });
    connect(m_next, &QToolButton::clicked, this,
        [this]
        {
            if (const int next = readyRow(m_view->currentIndex().row(), 1); next >= 0)
                m_view->setCurrentIndex(m_model->index(next));
        });
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &GenerationHistoryWidget::updateHistory);
    connect(m_model, &QAbstractItemModel::dataChanged, this, &GenerationHistoryWidget::updateHistory);
    updateHistory();
}

int GenerationHistoryWidget::readyRow(int row, int step) const
{
    for (row += step; row >= 0 && row < m_model->rowCount(); row += step)
        if (GenerationHistoryView::isReady(m_model->index(row)))
            return row;
    return -1;
}

void GenerationHistoryWidget::updateHistory()
{
    m_heading->setText(tr("Recent generations (%n)", nullptr, m_model->rowCount()));
    const int row = m_view->currentIndex().row();
    m_previous->setEnabled(row >= 0 && readyRow(row, -1) >= 0);
    m_next->setEnabled(readyRow(row, 1) >= 0);
}

void GenerationHistoryWidget::appendPending(const QUuid& id, const QString& label, const QString& status)
{
    if (!m_model->appendPending(id, label, status))
        return;
    // The selection stays where it is; only bring the new entry into view.
    m_view->scrollTo(m_model->index(m_model->rowCount() - 1));
}

void GenerationHistoryWidget::appendImage(const QImage& image, const QString& label, const QString& prompt,
    const QString& negativePrompt)
{
    if (m_model->appendImage(image, label, prompt, negativePrompt).isNull())
        return;
    m_view->setCurrentIndex(m_model->index(m_model->rowCount() - 1));
    m_view->scrollTo(m_view->currentIndex());
}

void GenerationHistoryWidget::clearSelection()
{
    m_view->selectionModel()->clear();
    updateHistory();
}

void GenerationHistoryWidget::removeImage(const QUuid& id)
{
    const bool selected = m_view->currentIndex().data(GenerationHistoryModel::IdRole).toUuid() == id;
    {
        const QSignalBlocker blocker(m_view);
        m_model->removeImage(id);
        if (selected)
            m_view->selectionModel()->clear();
    }
    updateHistory();
    if (selected)
        emit imageSelected({});
}
