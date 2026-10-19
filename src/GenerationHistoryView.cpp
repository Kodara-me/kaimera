#include "GenerationHistoryView.h"
#include "GenerationHistoryModel.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/IconProvider.h>
#include <Ardoise/Theme.h>
#include <Ardoise/ThumbnailDelegate.h>
#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <cmath>

namespace
{
using EntryState = GenerationHistoryModel::State;
EntryState entryState(const QModelIndex& index)
{
    return EntryState(index.data(GenerationHistoryModel::StateRole).toInt());
}

// Draws pending generations as a progress ring with their percentage inside an accent sweep
// around the frame, and failed ones with an exclamation mark and caption, in the frame of a
// thumbnail.
class HistoryDelegate final : public Ardoise::ThumbnailDelegate
{
public:
    explicit HistoryDelegate(GenerationHistoryView* view)
        : Ardoise::ThumbnailDelegate(view), m_view(view)
    {
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const EntryState state = entryState(index);
        if (state == EntryState::Ready) {
            Ardoise::ThumbnailDelegate::paint(painter, option, index);
            return;
        }
        const auto& tokens = Ardoise::Theme::tokens();
        const auto& metrics = tokens.metrics;
        const auto& colors = tokens.colors;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF frame = QRectF(option.rect).adjusted(metrics.spacing2 + 0.5, metrics.spacing2 + 0.5,
            -metrics.spacing2 - 0.5, -metrics.spacing2 - 0.5);
        const bool failed = state == EntryState::Failed;
        if (failed) {
            QPen border(colors.danger, metrics.borderWidth);
            border.setStyle(Qt::DashLine);
            painter->setPen(border);
        } else {
            painter->setPen(Qt::NoPen);
        }
        painter->setBrush(colors.inputBackground);
        painter->drawRoundedRect(frame, metrics.radiusMedium, metrics.radiusMedium);
        if (!failed) {
            // A bright arc sweeps around the border, trailing into the accent.
            QConicalGradient sweep(frame.center(), -m_view->sweepAngle());
            QColor clear = colors.accent;
            clear.setAlpha(0);
            sweep.setColorAt(0, colors.borderAccent);
            sweep.setColorAt(0.18, colors.accent);
            sweep.setColorAt(0.45, clear);
            sweep.setColorAt(1, clear);
            QColor glow = colors.accent;
            glow.setAlpha(50);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(glow, 3));
            painter->drawRoundedRect(frame, metrics.radiusMedium, metrics.radiusMedium);
            painter->setPen(QPen(QBrush(sweep), 2));
            painter->drawRoundedRect(frame, metrics.radiusMedium, metrics.radiusMedium);
        }
        const QFont caption = Ardoise::Theme::font(Ardoise::TextRole::Caption);
        const QFontMetrics captionMetrics(caption);
        const qreal size = frame.width() * 0.42;
        const qreal top = failed ? frame.center().y() - (size + metrics.spacing6 + captionMetrics.height()) / 2
                                 : frame.center().y() - size / 2;
        const qreal pen = 3;
        const QRectF ring(frame.center().x() - size / 2 + pen, top + pen, size - 2 * pen, size - 2 * pen);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(colors.borderSubtle, pen));
        painter->drawEllipse(ring);
        const QColor tone = failed ? colors.danger : colors.borderAccent;
        painter->setPen(QPen(tone, pen, Qt::SolidLine, Qt::RoundCap));
        const int percentage = index.data(GenerationHistoryModel::ProgressRole).toInt();
        if (failed) {
            painter->drawEllipse(ring);
            const QPointF c = ring.center();
            const qreal r = ring.width() / 2;
            painter->drawLine(QPointF(c.x(), c.y() - r * 0.45), QPointF(c.x(), c.y() + r * 0.1));
            painter->drawPoint(QPointF(c.x(), c.y() + r * 0.42));
            painter->setFont(caption);
            painter->setPen(colors.danger);
            const QRectF text(frame.left() + metrics.spacing4, top + size + metrics.spacing6,
                frame.width() - 2 * metrics.spacing4, captionMetrics.height());
            painter->drawText(text, Qt::AlignCenter,
                captionMetrics.elidedText(GenerationHistoryView::tr("Failed"), Qt::ElideRight, int(text.width())));
        } else {
            if (percentage < 0)
                painter->drawArc(ring, qRound(-m_view->spinAngle() * 16), 100 * 16);
            else
                painter->drawArc(ring, 90 * 16, -qRound(percentage * 3.6 * 16));
            if (percentage >= 0) {
                painter->setFont(caption);
                painter->setPen(colors.textPrimary);
                painter->drawText(ring, Qt::AlignCenter, QStringLiteral("%1%").arg(percentage));
            }
        }
        painter->restore();
    }

private:
    GenerationHistoryView* m_view;
};
}

GenerationHistoryView::GenerationHistoryView(QWidget* parent)
    : Ardoise::ThumbnailView(parent)
    , m_spin(new QTimer(this))
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    setItemDelegate(new HistoryDelegate(this));
    m_spin->setInterval(16);
    connect(m_spin, &QTimer::timeout, this, [this] {
        m_spinAngle = std::fmod(m_spinAngle + 6, 360.0);
        m_sweepAngle = std::fmod(m_sweepAngle + 3.6, 360.0);
        viewport()->update();
    });
    setObjectName(QStringLiteral("generationHistory"));
    setAccessibleName(tr("Recent generations"));
    setMouseTracking(true);
    // One remove button follows the hovered thumbnail.
    m_remove = new Ardoise::IconButton(Ardoise::Icon::Clear, tr("Remove from history"), viewport());
    m_remove->setObjectName(QStringLiteral("historyRemove"));
    m_remove->setFixedSize(metrics.iconSizeSmall + metrics.spacing2, metrics.iconSizeSmall + metrics.spacing2);
    m_remove->setIconSize(QSize(metrics.iconSizeSmall - metrics.spacing4, metrics.iconSizeSmall - metrics.spacing4));
    m_remove->hide();
    viewport()->installEventFilter(this);
    connect(this, &QAbstractItemView::entered, this, &GenerationHistoryView::showRemove);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, &GenerationHistoryView::updateRemove);
    connect(m_remove, &QToolButton::clicked, this,
        [this]
        {
            if (m_hovered.isValid())
                emit removeRequested(m_hovered.data(GenerationHistoryModel::IdRole).toUuid());
        });
}

void GenerationHistoryView::setModel(QAbstractItemModel* model)
{
    QListView::setModel(model);
    if (selectionModel())
        connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& index)
            { emit entrySelected(index.data(GenerationHistoryModel::IdRole).toUuid()); });
    if (model) {
        connect(model, &QAbstractItemModel::rowsInserted, this, &GenerationHistoryView::updateSpin);
        connect(model, &QAbstractItemModel::rowsRemoved, this, &GenerationHistoryView::updateSpin);
        connect(model, &QAbstractItemModel::modelReset, this, &GenerationHistoryView::updateSpin);
        connect(model, &QAbstractItemModel::dataChanged, this, [this] {
            updateSpin();
            updateRemove();
        });
    }
    updateSpin();
}

bool GenerationHistoryView::isReady(const QModelIndex& index)
{
    return index.isValid() && entryState(index) == EntryState::Ready;
}

void GenerationHistoryView::updateSpin()
{
    // Pending entries animate their border sweep (and their ring without a percentage).
    bool spinning = false;
    for (int row = 0; model() && row < model()->rowCount() && !spinning; ++row)
        spinning = entryState(model()->index(row, 0)) == EntryState::Pending;
    if (spinning && !m_spin->isActive())
        m_spin->start();
    else if (!spinning)
        m_spin->stop();
}

void GenerationHistoryView::mousePressEvent(QMouseEvent* event)
{
    const auto index = indexAt(event->position().toPoint());
    if (index.isValid() && !isReady(index)) {
        event->accept();
        return;
    }
    Ardoise::ThumbnailView::mousePressEvent(event);
}

void GenerationHistoryView::mouseDoubleClickEvent(QMouseEvent* event)
{
    const auto index = indexAt(event->position().toPoint());
    if (index.isValid() && !isReady(index)) {
        event->accept();
        return;
    }
    Ardoise::ThumbnailView::mouseDoubleClickEvent(event);
}

QModelIndex GenerationHistoryView::moveCursor(CursorAction action, Qt::KeyboardModifiers modifiers)
{
    // Keyboard navigation skips over generations that have no image to show.
    const QModelIndex current = currentIndex();
    QModelIndex next = Ardoise::ThumbnailView::moveCursor(action, modifiers);
    if (!next.isValid() || isReady(next))
        return next;
    const int step = current.isValid() && next.row() < current.row() ? -1 : 1;
    for (int row = next.row(); row >= 0 && row < model()->rowCount(); row += step)
        if (isReady(model()->index(row, 0)))
            return model()->index(row, 0);
    return current;
}

bool GenerationHistoryView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == viewport()) {
        if (event->type() == QEvent::Leave)
            showRemove({});
        else if (event->type() == QEvent::MouseMove
            && !indexAt(static_cast<QMouseEvent*>(event)->position().toPoint()).isValid())
            showRemove({});
    }
    return Ardoise::ThumbnailView::eventFilter(watched, event);
}

void GenerationHistoryView::updateGeometries()
{
    Ardoise::ThumbnailView::updateGeometries();
    updateRemove();
}

void GenerationHistoryView::showRemove(const QModelIndex& index)
{
    m_hovered = index;
    // Pending generations stay until their image arrives or they fail.
    if (!index.isValid() || entryState(index) == EntryState::Pending) {
        m_remove->hide();
        return;
    }
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    // Inside the top-right corner of the frame the thumbnail delegate draws.
    const QRect frame = visualRect(index).adjusted(metrics.spacing2, metrics.spacing2, -metrics.spacing2, -metrics.spacing2);
    m_remove->move(frame.right() + 1 - m_remove->width() - metrics.spacing2, frame.top() + metrics.spacing2);
    m_remove->show();
    m_remove->raise();
}

void GenerationHistoryView::updateRemove()
{
    // After scrolling or removal, follow whichever thumbnail is now under the cursor.
    if (!m_remove->isVisible() && !m_hovered.isValid())
        return;
    const QPoint cursor = viewport()->mapFromGlobal(QCursor::pos());
    showRemove(viewport()->rect().contains(cursor) ? indexAt(cursor) : QModelIndex());
}
