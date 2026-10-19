#include "JobActivityIndicator.h"
#include "FloatingSurface.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/Theme.h>
#include <QAbstractScrollArea>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVariantAnimation>

namespace
{
// Room around the pill's shape for its painted shadow.
int shadowMargin() { return Ardoise::Theme::tokens().metrics.spacing8; }
// Distance the pill travels while sliding in from above.
constexpr int RevealTravel = 20;
// Running pills grow from RunningWidth to fit their title and status, up to
// MaximumRunningWidth, and never shrink during a job so they do not jitter.
constexpr int RunningWidth = 420;
constexpr int MaximumRunningWidth = 720;
constexpr int FailedWidth = 560;
constexpr int SuccessHoldMs = 900;

// Single-line label that elides its text instead of growing the pill.
class ElidedLabel final : public QLabel
{
public:
    using QLabel::QLabel;
    QSize minimumSizeHint() const override { return { 0, QLabel::minimumSizeHint().height() }; }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        if (wordWrap()) {
            QLabel::paintEvent(event);
            return;
        }
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(contentsRect(), Qt::AlignVCenter | Qt::AlignLeft,
            fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};
}

// Progress ring: a spinning arc while the percentage is unknown, a filling arc
// otherwise, a check mark after success and an exclamation mark after failure.
class ActivityRing final : public QWidget
{
public:
    explicit ActivityRing(JobActivityIndicator* indicator)
        : QWidget(indicator), m_indicator(indicator)
    {
        const int size = Ardoise::Theme::tokens().metrics.iconSize;
        setFixedSize(size, size);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& colors = Ardoise::Theme::tokens().colors;
        using State = JobActivityIndicator::State;
        const State state = m_indicator->m_state;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal pen = 2.5;
        const QRectF ring = QRectF(rect()).adjusted(pen, pen, -pen, -pen);
        const QColor tone = state == State::Failed ? colors.danger
            : state == State::Succeeded ? colors.success : colors.borderAccent;
        painter.setPen(QPen(colors.borderSubtle, pen));
        painter.drawEllipse(ring);
        painter.setPen(QPen(tone, pen, Qt::SolidLine, Qt::RoundCap));
        if (state == State::Running && m_indicator->m_percentage < 0) {
            painter.drawArc(ring, qRound(-m_indicator->m_spinAngle * 16), 100 * 16);
            return;
        }
        if (state == State::Running) {
            painter.drawArc(ring, 90 * 16, -qRound(m_indicator->m_displayedProgress * 3.6 * 16));
            return;
        }
        painter.drawEllipse(ring);
        const QPointF c = ring.center();
        const qreal r = ring.width() / 2;
        if (state == State::Succeeded) {
            QPainterPath check;
            check.moveTo(c.x() - r * 0.45, c.y() + r * 0.02);
            check.lineTo(c.x() - r * 0.1, c.y() + r * 0.38);
            check.lineTo(c.x() + r * 0.48, c.y() - r * 0.32);
            painter.setPen(QPen(tone, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(check);
        } else if (state == State::Failed) {
            painter.setPen(QPen(tone, 2, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(c.x(), c.y() - r * 0.45), QPointF(c.x(), c.y() + r * 0.1));
            painter.drawPoint(QPointF(c.x(), c.y() + r * 0.42));
        }
    }

private:
    JobActivityIndicator* m_indicator;
};

JobActivityIndicator::JobActivityIndicator(QWidget* host)
    : QWidget(host)
    , m_host(host)
    , m_hideTimer(new QTimer(this))
    , m_reveal(new QVariantAnimation(this))
    , m_spin(new QVariantAnimation(this))
    , m_progress(new QVariantAnimation(this))
    , m_opacity(new QGraphicsOpacityEffect(this))
{
    const auto& tokens = Ardoise::Theme::tokens();
    const auto& metrics = tokens.metrics;
    setObjectName(QStringLiteral("generationActivity"));
    setAccessibleName(tr("Generation progress"));
    setAttribute(Qt::WA_NoSystemBackground);
    m_opacity->setOpacity(0);
    setGraphicsEffect(m_opacity);

    auto* layout = new QGridLayout(this);
    const int margin = shadowMargin();
    layout->setContentsMargins(margin + metrics.spacing12, margin + metrics.spacing6,
        margin + metrics.spacing8, margin + metrics.spacing6);
    layout->setHorizontalSpacing(metrics.spacing8);
    layout->setVerticalSpacing(metrics.spacing4);
    m_ring = new ActivityRing(this);
    layout->addWidget(m_ring, 0, 0, Qt::AlignVCenter);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("generationActivityTitle"));
    Ardoise::Theme::applyTypography(m_title, Ardoise::TextRole::BodyStrong);
    layout->addWidget(m_title, 0, 1);
    m_status = new ElidedLabel(this);
    m_status->setObjectName(QStringLiteral("generationStatus"));
    Ardoise::Theme::applyTypography(m_status, Ardoise::TextRole::Caption);
    m_status->setForegroundRole(QPalette::PlaceholderText);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_status, 0, 2);
    layout->setColumnStretch(2, 1);
    m_percentageLabel = new QLabel(this);
    m_percentageLabel->setObjectName(QStringLiteral("generationPercentage"));
    Ardoise::Theme::applyTypography(m_percentageLabel, Ardoise::TextRole::Caption);
    layout->addWidget(m_percentageLabel, 0, 3);
    m_dismiss = new Ardoise::IconButton(Ardoise::Icon::Close, tr("Dismiss"), this);
    m_dismiss->setObjectName(QStringLiteral("generationActivityDismiss"));
    m_dismiss->setProperty("kiraFlat", true);
    m_dismiss->setFixedSize(metrics.controlHeightSmall, metrics.controlHeightSmall);
    layout->addWidget(m_dismiss, 0, 4);
    connect(m_dismiss, &QToolButton::clicked, this, &JobActivityIndicator::dismiss);

    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(SuccessHoldMs);
    connect(m_hideTimer, &QTimer::timeout, this, [this] {
        if (m_state == State::Succeeded) dismiss();
    });
    connect(m_reveal, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_revealValue = value.toReal();
        m_opacity->setOpacity(m_revealValue);
        relayout();
    });
    connect(m_reveal, &QVariantAnimation::finished, this, [this] {
        if (m_state == State::Idle) hide();
        // Render crisply once fully revealed; the effect is only needed while fading.
        m_opacity->setEnabled(m_revealValue < 1);
    });
    m_spin->setStartValue(0.0);
    m_spin->setEndValue(360.0);
    m_spin->setDuration(1000);
    m_spin->setLoopCount(-1);
    connect(m_spin, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_spinAngle = value.toReal();
        m_ring->update();
    });
    m_progress->setDuration(250);
    m_progress->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_progress, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_displayedProgress = value.toReal();
        m_ring->update();
    });
    m_host->installEventFilter(this);
    if (auto* area = qobject_cast<QAbstractScrollArea*>(m_host))
        area->viewport()->installEventFilter(this);
    hide();
}

QString JobActivityIndicator::title() const { return m_title->text(); }

QString JobActivityIndicator::status() const { return m_statusText; }

void JobActivityIndicator::start(const QString& title)
{
    m_hideTimer->stop();
    const bool continuing = m_state == State::Running;
    m_state = State::Running;
    m_title->setText(title);
    if (!continuing) {
        m_fitWidth = 0;
        m_percentage = -1;
        m_progress->stop();
        m_displayedProgress = 0;
        m_statusText.clear();
    }
    updateStatusText();
    m_spin->start();
    reveal(true);
}

void JobActivityIndicator::setProgress(const QString& status, int percentage)
{
    if (m_state != State::Running)
        return;
    m_statusText = status;
    const bool wasKnown = m_percentage >= 0;
    m_percentage = percentage < 0 ? -1 : qBound(0, percentage, 100);
    if (m_percentage < 0) {
        if (m_spin->state() != QAbstractAnimation::Running) m_spin->start();
    } else {
        m_spin->stop();
        m_progress->stop();
        m_progress->setStartValue(wasKnown ? m_displayedProgress : 0.0);
        m_progress->setEndValue(qreal(m_percentage));
        m_progress->start();
    }
    updateStatusText();
}

void JobActivityIndicator::finish()
{
    if (m_state != State::Running)
        return;
    m_state = State::Succeeded;
    m_spin->stop();
    m_percentage = 100;
    m_displayedProgress = 100;
    m_statusText = tr("Done");
    updateStatusText();
    m_hideTimer->start();
}

void JobActivityIndicator::fail(const QString& title, const QString& message)
{
    m_hideTimer->stop();
    m_spin->stop();
    m_state = State::Failed;
    m_title->setText(title);
    m_statusText = message;
    updateStatusText();
    reveal(true);
}

void JobActivityIndicator::dismiss()
{
    m_hideTimer->stop();
    m_spin->stop();
    m_state = State::Idle;
    reveal(false);
}

void JobActivityIndicator::updateStatusText()
{
    auto* layout = static_cast<QGridLayout*>(this->layout());
    const bool failed = m_state == State::Failed;
    // Failures show the whole message, wrapped and selectable, under the title.
    m_status->setText(m_statusText);
    m_status->setToolTip(failed ? QString() : m_statusText);
    layout->removeWidget(m_status);
    if (failed)
        layout->addWidget(m_status, 1, 1, 1, 4);
    else
        layout->addWidget(m_status, 0, 2);
    m_status->setWordWrap(failed);
    m_status->setTextInteractionFlags(failed ? Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard
                                             : Qt::NoTextInteraction);
    m_status->setSizePolicy(failed ? QSizePolicy::Preferred : QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_percentageLabel->setText(m_state == State::Running && m_percentage >= 0
        ? tr("%1%").arg(m_percentage) : QString());
    m_percentageLabel->setVisible(!m_percentageLabel->text().isEmpty());
    m_dismiss->setVisible(failed);
    if (!failed) {
        // Reserve room for "100%" so the pill does not widen once progress is known.
        const QMargins margins = layout->contentsMargins();
        const int spacing = layout->horizontalSpacing();
        const int content = margins.left() + margins.right() - 2 * shadowMargin() + m_ring->width()
            + m_title->sizeHint().width() + m_status->fontMetrics().horizontalAdvance(m_statusText)
            + m_percentageLabel->fontMetrics().horizontalAdvance(tr("%1%").arg(100)) + 3 * spacing;
        m_fitWidth = qMax(m_fitWidth, qMin(content, MaximumRunningWidth));
    }
    layout->invalidate();
    relayout();
    m_ring->update();
}

void JobActivityIndicator::reveal(bool visible)
{
    if (visible) {
        show();
        raise();
    }
    const qreal target = visible ? 1.0 : 0.0;
    if (m_revealValue == target && m_reveal->state() != QAbstractAnimation::Running) {
        if (!visible) hide();
        relayout();
        return;
    }
    m_opacity->setEnabled(true);
    m_reveal->stop();
    m_reveal->setDuration(visible ? 280 : 220);
    m_reveal->setEasingCurve(visible ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    m_reveal->setStartValue(m_revealValue);
    m_reveal->setEndValue(target);
    m_reveal->start();
}

QRect JobActivityIndicator::area() const
{
    if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(m_host))
        return scrollArea->viewport()->geometry();
    return m_host->rect();
}

void JobActivityIndicator::relayout()
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    const int margin = shadowMargin();
    const QRect host = area();
    const int preferred = m_state == State::Failed ? FailedWidth : qMax(RunningWidth, m_fitWidth);
    const int width = qMax(qMin(host.width() - 2 * metrics.spacing16, preferred), 200) + 2 * margin;
    const int height = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width)
                                                     : layout()->totalSizeHint().height();
    const int top = host.top() + metrics.spacing12 - margin - qRound((1 - m_revealValue) * RevealTravel);
    setGeometry(host.center().x() - width / 2, top, width, height);
    const int covered = isVisible()
        ? qMax(0, geometry().bottom() - margin - host.top() + 1 + metrics.spacing12) : 0;
    emit coveredHeightChanged(qRound(covered * m_revealValue));
    update();
}

bool JobActivityIndicator::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Resize && watched != this)
        relayout();
    return QWidget::eventFilter(watched, event);
}

void JobActivityIndicator::mousePressEvent(QMouseEvent* event) { event->accept(); }

void JobActivityIndicator::wheelEvent(QWheelEvent* event) { event->accept(); }

void JobActivityIndicator::paintEvent(QPaintEvent*)
{
    const int margin = shadowMargin();
    QPainter painter(this);
    const QRectF shape = QRectF(rect()).adjusted(margin + 0.5, margin + 0.5, -margin - 0.5, -margin - 0.5);
    const qreal radius = m_state == State::Failed
        ? Ardoise::Theme::tokens().metrics.radiusLarge + Ardoise::Theme::tokens().metrics.spacing4
        : shape.height() / 2;
    FloatingSurface::paint(painter, shape, radius, margin, false);
}
