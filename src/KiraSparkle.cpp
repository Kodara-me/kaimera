#include "KiraSparkle.h"
#include "FloatingSurface.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/IconProvider.h>
#include <Ardoise/PlainTextEdit.h>
#include <Ardoise/PrimaryButton.h>
#include <Ardoise/Theme.h>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QContextMenuEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QMenu>
#include <QScrollBar>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <cmath>
#include <functional>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace
{
// Room around the shape for its painted shadow.
int shadowMargin() { return Ardoise::Theme::tokens().metrics.spacing8; }
// Distance the bubble travels while rising in.
constexpr int RevealTravel = 28;
// Fits five attachments, the engine button and Send in the toolbar.
constexpr int MaximumWidth = 480;

// A four-pointed star with gently curved sides, like the framework's Sparkles icon.
QPainterPath starPath(const QPointF& center, qreal radius)
{
    const qreal k = 0.14 * radius;
    const qreal x = center.x(), y = center.y();
    QPainterPath path(QPointF(x, y - radius));
    path.quadTo(x + k, y - k, x + radius, y);
    path.quadTo(x + k, y + k, x, y + radius);
    path.quadTo(x - k, y + k, x - radius, y);
    path.quadTo(x - k, y - k, x, y - radius);
    return path;
}

// The round Kira Sparkle bubble. It draws nothing itself: KiraSparkle paints it with its
// surface (paintBubble) so the halo and the click ripple can reach beyond the button.
// Hovering lights a halo, thickens and spins a gradient ring and makes the drawn stars
// fill in and twinkle in turn; a press dips it and sends out a ripple; it stays lit while
// its panel is open.
class SparkleBubble final : public QToolButton
{
public:
    explicit SparkleBubble(QWidget* parent)
        : QToolButton(parent)
        , m_hover(new QVariantAnimation(this))
        , m_twinkle(new QVariantAnimation(this))
        , m_ripple(new QVariantAnimation(this))
    {
        m_hover->setDuration(260);
        m_hover->setEasingCurve(QEasingCurve::OutCubic);
        m_twinkle->setStartValue(0.0);
        m_twinkle->setEndValue(1.0);
        m_twinkle->setDuration(2400);
        m_twinkle->setLoopCount(-1);
        m_ripple->setStartValue(0.0);
        m_ripple->setEndValue(1.0);
        m_ripple->setDuration(460);
        m_ripple->setEasingCurve(QEasingCurve::OutCubic);
        for (auto* animation : {m_hover, m_twinkle, m_ripple})
            connect(animation, &QVariantAnimation::valueChanged, this, [this] { parentWidget()->update(); });
        // The ring keeps turning only while the halo is lit.
        connect(m_hover, &QVariantAnimation::finished, this, [this] {
            if (hover() == 0) m_twinkle->stop();
        });
        connect(this, &QToolButton::pressed, this, [this] { m_ripple->stop(); m_ripple->start(); });
        connect(this, &QToolButton::released, this, [this] { parentWidget()->update(); });
    }

    void setLit(bool lit)
    {
        m_lit = lit;
        parentWidget()->update();
    }

    void paintBubble(QPainter& painter, const QRectF& circle) const
    {
        const auto& colors = Ardoise::Theme::tokens().colors;
        const QPointF center = circle.center();
        const qreal radius = circle.width() / 2;
        const qreal hovered = hover();
        const qreal lit = qMax(hovered, m_lit ? 1.0 : 0.0);
        const qreal turn = m_twinkle->state() == QAbstractAnimation::Running ? m_twinkle->currentValue().toReal() : 0;
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        if (m_ripple->state() == QAbstractAnimation::Running) {
            const qreal t = m_ripple->currentValue().toReal();
            QColor wave = colors.borderAccent;
            wave.setAlphaF(0.75 * (1 - t));
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(wave, 0.5 + 2 * (1 - t)));
            painter.drawEllipse(center, radius + 6 * t, radius + 6 * t);
        }
        // A press dips the whole bubble slightly.
        const qreal scale = isDown() ? 0.93 : 1.0;
        painter.translate(center);
        painter.scale(scale, scale);
        painter.translate(-center);
        // The halo: soft accent rings outside the bubble, half lit for keyboard focus.
        const qreal glow = qMax(hovered, hasFocus() && !underMouse() ? 0.5 : 0.0);
        painter.setBrush(Qt::NoBrush);
        for (int i = 1; glow > 0 && i <= 3; ++i) {
            QColor halo = colors.accent;
            halo.setAlphaF(0.3 * glow / i);
            painter.setPen(QPen(halo, 2));
            painter.drawEllipse(center, radius + 2 * i - 1, radius + 2 * i - 1);
        }
        // A violet core that glows from the centre while lit.
        QColor core = colors.accentMuted;
        core.setAlphaF(0.95 * lit);
        QColor edge = core;
        edge.setAlphaF(0);
        QRadialGradient fill(center, radius);
        fill.setColorAt(0, core);
        fill.setColorAt(1, edge);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawEllipse(center, radius - 1, radius - 1);
        // The gradient ring thickens and turns while lit.
        QConicalGradient sweep(center, 90 - 360 * turn);
        sweep.setColorAt(0, colors.borderAccent);
        sweep.setColorAt(0.3, colors.accentHover.lighter(135));
        sweep.setColorAt(0.55, colors.accent);
        sweep.setColorAt(0.8, colors.accentHover);
        sweep.setColorAt(1, colors.borderAccent);
        const qreal width = 2.25 + 1.25 * lit;
        painter.setPen(QPen(QBrush(sweep), width));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius - width / 2, radius - width / 2);
        // The stars, drawn on the Sparkles icon's 24-unit grid around its visual centre.
        struct Star { QPointF center; qreal radius; qreal phase; };
        static const Star stars[] = {{{10, 13}, 7.5, 0.0}, {{18, 6}, 3.5, 0.33}, {{19.5, 18}, 2, 0.66}};
        const qreal unit = (20 + 3 * hovered) / 24.0;
        const QPointF origin = center - QPointF(11.5, 12) * unit;
        QLinearGradient shine(center - QPointF(0, radius / 2), center + QPointF(0, radius / 2));
        QColor top = colors.accentHover.lighter(150), bottom = colors.borderAccent;
        top.setAlphaF(lit);
        bottom.setAlphaF(lit);
        shine.setColorAt(0, top);
        shine.setColorAt(1, bottom);
        painter.setPen(QPen(colors.borderAccent, 1.6 * unit, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(shine);
        for (const auto& star : stars) {
            // Each star swells briefly in turn while hovered.
            const qreal pulse = std::pow(qMax(0.0, std::sin(2 * M_PI * (turn - star.phase))), 2);
            painter.drawPath(starPath(origin + star.center * unit, star.radius * unit * (1 + 0.22 * hovered * pulse)));
        }
        painter.restore();
    }

protected:
    void enterEvent(QEnterEvent* event) override
    {
        QToolButton::enterEvent(event);
        animateHover(1);
    }
    void leaveEvent(QEvent* event) override
    {
        QToolButton::leaveEvent(event);
        animateHover(0);
    }
    void focusInEvent(QFocusEvent* event) override
    {
        QToolButton::focusInEvent(event);
        parentWidget()->update();
    }
    void focusOutEvent(QFocusEvent* event) override
    {
        QToolButton::focusOutEvent(event);
        parentWidget()->update();
    }
    void paintEvent(QPaintEvent*) override {}

private:
    qreal hover() const { return m_hover->currentValue().toReal(); }
    void animateHover(qreal target)
    {
        m_hover->stop();
        m_hover->setStartValue(hover());
        m_hover->setEndValue(target);
        m_hover->start();
        if (target > 0 && m_twinkle->state() != QAbstractAnimation::Running)
            m_twinkle->start();
    }
    QVariantAnimation* m_hover;
    QVariantAnimation* m_twinkle;
    QVariantAnimation* m_ripple;
    bool m_lit = false;
};

// The largest height of a result in the conversation.
constexpr int MaximumResultHeight = 220;

// An entry of the conversation, fitted to the width it may take.
class ChatItem : public QWidget
{
public:
    using QWidget::QWidget;
    virtual void fitWidth(int width) = 0;
    // The history ticket of a result, null for other entries.
    virtual QUuid id() const { return {}; }
};

// A progress ring: a filling arc for a known percentage, a spinning one otherwise, a full
// danger ring once failed.
void paintRing(QPainter& painter, const QRectF& ring, int percentage, qreal spin, bool failed)
{
    const auto& colors = Ardoise::Theme::tokens().colors;
    const qreal pen = qMax(2.0, ring.width() / 12);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(failed ? colors.danger : colors.borderSubtle, pen));
    painter.drawEllipse(ring);
    if (failed)
        return;
    painter.setPen(QPen(colors.accent, pen, Qt::SolidLine, Qt::RoundCap));
    if (percentage < 0)
        painter.drawArc(ring, qRound(-spin * 16), 100 * 16);
    else
        painter.drawArc(ring, 90 * 16, -qRound(percentage * 3.6 * 16));
}

// A looping angle that turns a spinning ring while its owner is busy.
QVariantAnimation* spinAnimation(QWidget* owner)
{
    auto* spin = new QVariantAnimation(owner);
    spin->setStartValue(0.0);
    spin->setEndValue(360.0);
    spin->setDuration(1100);
    spin->setLoopCount(-1);
    QObject::connect(spin, &QVariantAnimation::valueChanged, owner, [owner] { owner->update(); });
    return spin;
}

qreal spinAngle(const QVariantAnimation* spin)
{
    return spin->state() == QAbstractAnimation::Running ? spin->currentValue().toReal() : 0;
}

// Kira Sparkle at work: a spinning ring with its status, on the left like its replies.
class ChatThinking final : public ChatItem
{
public:
    explicit ChatThinking(QWidget* parent)
        : ChatItem(parent)
        , m_spin(spinAnimation(this))
    {
        const auto& metrics = Ardoise::Theme::tokens().metrics;
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(2 * metrics.spacing8 + metrics.iconSize, metrics.spacing8, metrics.spacing12,
            metrics.spacing8);
        m_label = new QLabel(this);
        m_label->setObjectName(QStringLiteral("kiraSparkleThinking"));
        m_label->setTextFormat(Qt::PlainText);
        layout->addWidget(m_label);
        m_spin->start();
    }

    void setStatus(const QString& status, int percentage)
    {
        m_label->setText(percentage >= 0 ? QStringLiteral("%1 %2%").arg(status).arg(percentage) : status);
        m_percentage = percentage;
        update();
    }
    void fitWidth(int width) override
    {
        const auto& metrics = Ardoise::Theme::tokens().metrics;
        m_label->setMaximumWidth(qMax(1, width - 2 * metrics.spacing8 - metrics.iconSize - metrics.spacing12));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& tokens = Ardoise::Theme::tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(tokens.colors.panelBackgroundHover);
        painter.drawRoundedRect(QRectF(rect()), tokens.metrics.radiusLarge, tokens.metrics.radiusLarge);
        const int size = tokens.metrics.iconSize;
        paintRing(painter, QRectF(tokens.metrics.spacing8 + 1, (height() - size) / 2.0 + 1, size - 2, size - 2),
            m_percentage, spinAngle(m_spin), false);
    }

private:
    QVariantAnimation* m_spin;
    QLabel* m_label;
    int m_percentage = -1;
};

// An image Kira Sparkle queued, in the image's ratio: its progress ring and status while
// pending, the delivered image (double-click opens it, right-click shows the image menu)
// or the failure.
class ChatResult final : public ChatItem
{
public:
    ChatResult(const QUuid& id, const QSize& size, const QString& status, QWidget* parent)
        : ChatItem(parent)
        , m_id(id)
        , m_size(size.isEmpty() ? QSize(1, 1) : size)
        , m_status(status)
        , m_spin(spinAnimation(this))
    {
        setObjectName(QStringLiteral("kiraSparkleResult"));
        setToolTip(status);
        m_spin->start();
    }

    QUuid id() const override { return m_id; }
    QImage image() const { return m_image; }
    bool failed() const { return m_failed; }
    std::function<void()> opened;
    std::function<void(const QPoint&)> menuRequested;

    void fitWidth(int width) override
    {
        m_width = width;
        setFixedSize(m_size.scaled(QSize(qMax(1, width), MaximumResultHeight), Qt::KeepAspectRatio));
    }
    void setProgress(const QString& status, int percentage)
    {
        m_status = status;
        m_percentage = percentage;
        setToolTip(status);
        update();
    }
    void complete(const QImage& image)
    {
        m_image = image;
        m_size = image.size();
        m_spin->stop();
        setToolTip({});
        setCursor(Qt::PointingHandCursor);
        fitWidth(m_width);
        update();
    }
    void fail(const QString& message)
    {
        m_failed = true;
        m_status = message;
        m_spin->stop();
        setToolTip(message);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& tokens = Ardoise::Theme::tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(rect()), tokens.metrics.radiusLarge, tokens.metrics.radiusLarge);
        painter.setClipPath(shape);
        if (!m_image.isNull()) {
            painter.drawImage(rect(), m_image);
            return;
        }
        painter.fillRect(rect(), tokens.colors.panelBackgroundHover);
        // The ring, with its percentage inside, above the status.
        const int ringSize = qMin(40, height() / 3);
        const QRectF ring(width() / 2.0 - ringSize / 2.0, height() / 2.0 - ringSize, ringSize, ringSize);
        paintRing(painter, ring, m_percentage, spinAngle(m_spin), m_failed);
        painter.setPen(m_failed ? tokens.colors.danger : tokens.colors.textPrimary);
        if (m_failed || m_percentage >= 0)
            painter.drawText(ring, Qt::AlignCenter, m_failed ? QStringLiteral("!") : QStringLiteral("%1%").arg(m_percentage));
        painter.setPen(m_failed ? tokens.colors.danger : tokens.colors.textMuted);
        const QRect text(tokens.metrics.spacing8, qRound(ring.bottom()) + tokens.metrics.spacing8,
            width() - 2 * tokens.metrics.spacing8, height() / 2 - tokens.metrics.spacing8);
        painter.drawText(text, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
            painter.fontMetrics().elidedText(m_status, Qt::ElideRight, 2 * text.width()));
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (!m_image.isNull() && event->button() == Qt::LeftButton && opened)
            opened();
    }
    void contextMenuEvent(QContextMenuEvent* event) override
    {
        if (!m_image.isNull() && menuRequested)
            menuRequested(event->globalPos());
    }

private:
    QUuid m_id;
    QSize m_size;
    QString m_status;
    QImage m_image;
    QVariantAnimation* m_spin;
    int m_percentage = -1;
    int m_width = MaximumResultHeight;
    bool m_failed = false;
};

// One chat message: the user's on the right in an accent bubble,
// Kira Sparkle's on the left on a quieter surface.
// A message; a proposal also has a button carrying it out, keyed by its id.
class ChatMessage final : public ChatItem
{
public:
    ChatMessage(bool fromUser, const QString& text, QWidget* parent, const QUuid& id = {},
        const QString& action = {})
        : ChatItem(parent)
        , m_fromUser(fromUser)
        , m_id(id)
    {
        const auto& metrics = Ardoise::Theme::tokens().metrics;
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(metrics.spacing12, metrics.spacing8, metrics.spacing12, metrics.spacing8);
        layout->setSpacing(metrics.spacing6);
        m_label = new QLabel(text, this);
        m_label->setObjectName(QStringLiteral("kiraSparkleMessage"));
        m_label->setProperty("fromUser", fromUser);
        m_label->setTextFormat(Qt::PlainText);
        m_label->setWordWrap(true);
        m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(m_label);
        if (!action.isEmpty()) {
            m_button = new Ardoise::PrimaryButton(action, this);
            m_button->setObjectName(QStringLiteral("kiraSparkleProposalAction"));
            m_button->setIcon(Ardoise::IconProvider::icon(Ardoise::Icon::Wand));
            layout->addWidget(m_button, 0, Qt::AlignLeft);
        }
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    }

    QUuid id() const override { return m_id; }
    QLabel* label() const { return m_label; }
    QPushButton* button() const { return m_button; }

    // Short messages keep their natural width; longer ones wrap at the given width.
    void fitWidth(int width) override
    {
        width -= 2 * Ardoise::Theme::tokens().metrics.spacing12;
        m_label->ensurePolished();
        const QRect text = m_label->fontMetrics().boundingRect(
            QRect(0, 0, qMax(1, width), QWIDGETSIZE_MAX), Qt::TextWordWrap, m_label->text());
        m_label->setFixedSize(text.width() + 1, text.height());
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& tokens = Ardoise::Theme::tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_fromUser ? tokens.colors.accentMuted : tokens.colors.panelBackgroundHover);
        painter.drawRoundedRect(QRectF(rect()), tokens.metrics.radiusLarge, tokens.metrics.radiusLarge);
    }

private:
    QLabel* m_label;
    QPushButton* m_button = nullptr;
    bool m_fromUser;
    QUuid m_id;
};

// A preset's monogram in a rounded tile, in warning colour when the preset cannot generate.
void paintMonogram(QPainter& painter, const QRectF& rect, const QString& code, bool unavailable, bool enabled)
{
    const auto& tokens = Ardoise::Theme::tokens();
    painter.setPen(Qt::NoPen);
    painter.setBrush(tokens.colors.accentMuted);
    painter.drawRoundedRect(rect, tokens.metrics.radiusMedium, tokens.metrics.radiusMedium);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(qRound(rect.height() / 2));
    painter.setFont(font);
    painter.setPen(!enabled ? tokens.colors.textDisabled
        : unavailable      ? tokens.colors.warning
                           : tokens.colors.textPrimary);
    painter.drawText(rect, Qt::AlignCenter, code);
}

// An editing quality as rising bars, the first level ones filled.
void paintLevel(QPainter& painter, const QRectF& rect, int level, int levels, bool unavailable, bool enabled)
{
    const auto& colors = Ardoise::Theme::tokens().colors;
    levels = qMax(1, levels);
    const qreal gap = rect.width() / (3 * levels - 1);
    const qreal bar = 2 * gap;
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < levels; ++i) {
        const qreal height = rect.height() * (i + 1 + levels / 2.0) / (levels + levels / 2.0);
        const QColor fill = i >= level ? colors.borderNormal
            : !enabled                 ? colors.textDisabled
            : unavailable              ? colors.warning
                                       : colors.borderAccent;
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(rect.left() + i * (bar + gap), rect.bottom() - height, bar, height), 1, 1);
    }
}

// Menu icons for the engine button's choices, at the screen's pixel ratio.
QIcon choiceIcon(const QWidget* widget, const std::function<void(QPainter&, const QRectF&)>& paint)
{
    const int size = Ardoise::Theme::tokens().metrics.iconSize;
    QPixmap pixmap(QSize(size, size) * widget->devicePixelRatioF());
    pixmap.setDevicePixelRatio(widget->devicePixelRatioF());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    paint(painter, QRectF(0, 0, size, size));
    return QIcon(pixmap);
}

// The engine button: the creation preset's monogram, a divider, the editing quality's
// level and a chevron, over the shared icon button frame.
class EngineButton final : public Ardoise::IconButton
{
public:
    explicit EngineButton(QWidget* parent) : Ardoise::IconButton(Ardoise::Icon::Quality, QString(), parent) {}
    // editing dims the preset, which only applies without a base image, else the quality.
    void setState(const QString& monogram, bool presetUnavailable, int level, int levels, bool qualityUnavailable,
        bool editing)
    {
        m_monogram = monogram;
        m_presetUnavailable = presetUnavailable;
        m_level = level;
        m_levels = levels;
        m_qualityUnavailable = qualityUnavailable;
        m_editing = editing;
        update();
    }
    bool isEditing() const { return m_editing; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& tokens = Ardoise::Theme::tokens();
        const auto& metrics = tokens.metrics;
        QStylePainter painter(this);
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.icon = QIcon();
        option.text.clear();
        painter.drawComplexControl(QStyle::CC_ToolButton, option);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool enabled = isEnabled();
        const qreal tile = metrics.iconSizeLarge - metrics.spacing4;
        const QRectF monogram(metrics.spacing8, (height() - tile) / 2, tile, tile);
        constexpr qreal Dimmed = 0.35;
        painter.setOpacity(m_editing ? Dimmed : 1);
        paintMonogram(painter, monogram, m_monogram.isEmpty() ? QStringLiteral("–") : m_monogram,
            m_presetUnavailable, enabled);
        painter.setOpacity(1);
        const qreal divider = monogram.right() + metrics.spacing6 + 0.5;
        painter.setPen(QPen(tokens.colors.borderSubtle, metrics.borderWidth));
        painter.drawLine(QPointF(divider, monogram.top()), QPointF(divider, monogram.bottom()));
        const QRectF level(divider + metrics.spacing6, monogram.top() + metrics.spacing4, metrics.iconSizeSmall,
            tile - 2 * metrics.spacing4);
        painter.setOpacity(m_editing ? 1 : Dimmed);
        paintLevel(painter, level, m_level, m_levels, m_qualityUnavailable, enabled);
        painter.setOpacity(1);
        const QIcon chevron = Ardoise::IconProvider::icon(Ardoise::Icon::ChevronDown);
        const int chevronSize = metrics.iconSizeSmall - metrics.spacing4;
        chevron.paint(&painter, QRect(qRound(level.right()) + metrics.spacing2, (height() - chevronSize) / 2,
                                    chevronSize, chevronSize), Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
    }

private:
    QString m_monogram;
    bool m_presetUnavailable = false;
    int m_level = 0;
    int m_levels = 3;
    bool m_qualityUnavailable = false;
    bool m_editing = false;
};
}

QStringList KiraSparkle::monograms(const QStringList& names)
{
    QStringList codes;
    for (const QString& name : names) {
        QStringList words;
        QString word;
        for (const QChar c : name + QLatin1Char(' ')) {
            if (c.isLetterOrNumber()) {
                word.append(c);
            } else if (!word.isEmpty()) {
                words.append(word);
                word.clear();
            }
        }
        if (words.isEmpty()) words.append(QStringLiteral("?"));
        const QString first = words.first();
        QStringList candidates{first.left(1).toUpper() + first.mid(1, 1)};
        if (words.size() > 1)
            candidates.append(first.left(1).toUpper() + words.at(1).left(1).toUpper());
        for (int i = 2; i < 100; ++i)
            candidates.append(first.left(1).toUpper() + QString::number(i));
        for (const QString& candidate : candidates)
            if (!codes.contains(candidate)) {
                codes.append(candidate);
                break;
            }
    }
    return codes;
}

KiraSparkle::KiraSparkle(QAbstractScrollArea* host)
    : QWidget(host)
    , m_host(host)
    , m_reveal(new QVariantAnimation(this))
    , m_morph(new QVariantAnimation(this))
    , m_opacity(new QGraphicsOpacityEffect(this))
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    setObjectName(QStringLiteral("kiraSparkle"));
    setAttribute(Qt::WA_NoSystemBackground);
    m_opacity->setOpacity(0);
    setGraphicsEffect(m_opacity);

    m_panel = new QWidget(this);
    auto* layout = new QVBoxLayout(m_panel);
    layout->setContentsMargins(metrics.spacing16, metrics.spacing12, metrics.spacing12, metrics.spacing12);
    layout->setSpacing(metrics.spacing8);
    auto* header = new QHBoxLayout;
    header->setSpacing(metrics.spacing8);
    auto* icon = new QLabel(m_panel);
    icon->setPixmap(Ardoise::IconProvider::icon(Ardoise::Icon::Sparkles, Ardoise::IconTone::Accent)
            .pixmap(metrics.iconSize, metrics.iconSize));
    header->addWidget(icon);
    auto* title = new QLabel(tr("Kira Sparkle"), m_panel);
    Ardoise::Theme::applyTypography(title, Ardoise::TextRole::BodyStrong);
    header->addWidget(title);
    header->addStretch();
    m_newChat = new Ardoise::IconButton(Ardoise::Icon::Plus, tr("New chat"), m_panel);
    m_newChat->setObjectName(QStringLiteral("kiraSparkleNewChat"));
    m_newChat->setFixedSize(metrics.controlHeightSmall, metrics.controlHeightSmall);
    header->addWidget(m_newChat);
    layout->addLayout(header);
    // The conversation, kept at the bottom of its scrollable area, newest last.
    m_transcript = new QScrollArea(m_panel);
    m_transcript->setObjectName(QStringLiteral("kiraSparkleTranscript"));
    m_transcript->setFrameShape(QFrame::NoFrame);
    m_transcript->setWidgetResizable(true);
    m_transcript->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_transcript->setMinimumHeight(metrics.controlHeightLarge);
    m_transcript->viewport()->setAutoFillBackground(false);
    auto* conversation = new QWidget(m_transcript);
    conversation->setAutoFillBackground(false);
    m_messages = new QVBoxLayout(conversation);
    m_messages->setContentsMargins(0, 0, 0, 0);
    m_messages->setSpacing(metrics.spacing8);
    m_messages->addStretch();
    m_transcript->setWidget(conversation);
    connect(m_transcript->verticalScrollBar(), &QScrollBar::rangeChanged, this,
        [this](int, int maximum) { m_transcript->verticalScrollBar()->setValue(maximum); });
    layout->addWidget(m_transcript, 1);
    m_prompt = new Ardoise::PlainTextEdit(m_panel);
    m_prompt->setObjectName(QStringLiteral("kiraSparklePrompt"));
    m_prompt->setPlaceholderText(tr("Describe what you want to create or change…"));
    m_prompt->setAccessibleName(tr("Kira Sparkle request"));
    m_prompt->setFixedHeight(m_prompt->heightForLines(2));
    m_prompt->installEventFilter(this);
    layout->addWidget(m_prompt);
    // Under the field: the attach button, the base image and the references on the left,
    // Send on the right, the buttons as tall as a thumbnail so attaching never moves the toolbar.
    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(metrics.spacing8);
    const int button = ReferenceImageStrip::ThumbnailSize;
    m_attach = new Ardoise::IconButton(Ardoise::Icon::Plus, tr("Attach images"), m_panel);
    m_attach->setObjectName(QStringLiteral("kiraSparkleAttach"));
    m_attach->setToolTip(tr("Add a base image to change or up to %n reference image(s)", nullptr,
        ReferenceImageStrip::MaximumReferences));
    m_attach->setFixedSize(button, button);
    toolbar->addWidget(m_attach, 0, Qt::AlignBottom);
    m_baseStrip = new ReferenceImageStrip(QStringLiteral("kiraSparkleRemoveBase"), m_panel);
    m_baseStrip->setObjectName(QStringLiteral("kiraSparkleBase"));
    m_baseStrip->setCountVisible(false);
    m_baseStrip->setBase(true);
    m_baseStrip->setMaximum(1);
    toolbar->addWidget(m_baseStrip);
    m_referenceStrip = new ReferenceImageStrip(QStringLiteral("kiraSparkleRemoveAttachment"), m_panel);
    m_referenceStrip->setObjectName(QStringLiteral("kiraSparkleAttachments"));
    m_referenceStrip->setCountVisible(false);
    toolbar->addWidget(m_referenceStrip);
    toolbar->addStretch();
    // The creation preset and editing quality the user chose; Kira Sparkle never picks them.
    m_engine = new EngineButton(m_panel);
    m_engine->setObjectName(QStringLiteral("kiraSparkleEngine"));
    m_engine->setAccessibleName(tr("Creation preset and editing quality"));
    m_engine->setFixedSize(button + 2 * metrics.spacing16, button);
    toolbar->addWidget(m_engine, 0, Qt::AlignBottom);
    m_send = new Ardoise::PrimaryButton(QString(), m_panel);
    m_send->setObjectName(QStringLiteral("kiraSparkleSend"));
    m_send->setAccessibleName(tr("Send"));
    m_send->setIcon(Ardoise::IconProvider::icon(Ardoise::Icon::Send));
    m_send->setFixedSize(button, button);
    toolbar->addWidget(m_send, 0, Qt::AlignBottom);
    layout->addLayout(toolbar);
    addMessage(false, tr("How can I help you?"));

    m_bubble = new SparkleBubble(this);
    m_bubble->setObjectName(QStringLiteral("kiraSparkleBubble"));
    m_bubble->setToolTip(tr("Kira Sparkle"));
    m_bubble->setAccessibleName(tr("Kira Sparkle"));
    m_bubble->setFixedSize(metrics.controlHeightLarge, metrics.controlHeightLarge);
    m_bubble->setCursor(Qt::PointingHandCursor);
    m_panel->hide();

    // The bubble stays visible and toggles the panel beside it.
    connect(m_bubble, &QToolButton::clicked, this, [this] { setExpanded(!m_expanded, true); });
    connect(m_send, &QPushButton::clicked, this, &KiraSparkle::send);
    connect(m_newChat, &QToolButton::clicked, this, &KiraSparkle::newChat);
    connect(m_engine, &QToolButton::clicked, this, &KiraSparkle::showEngineMenu);
    connect(m_attach, &QToolButton::clicked, this, &KiraSparkle::showAttachMenu);
    connect(m_prompt, &QPlainTextEdit::textChanged, this, &KiraSparkle::updateSendButton);
    for (auto* strip : {m_baseStrip, m_referenceStrip})
        connect(strip, &ReferenceImageStrip::referencesChanged, this, [this] {
            updateEngine(); // A base image makes the request an edit.
            m_panel->layout()->invalidate();
            relayout();
        });
    connect(m_reveal, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_revealValue = value.toReal();
        m_opacity->setOpacity(m_revealValue);
        relayout();
    });
    connect(m_reveal, &QVariantAnimation::finished, this, [this] {
        if (!m_active) hide();
        // Render crisply once fully revealed; the effect is only needed while fading.
        m_opacity->setEnabled(m_revealValue < 1);
    });
    m_morph->setDuration(340);
    connect(m_morph, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_morphValue = value.toReal();
        relayout();
    });
    connect(m_morph, &QVariantAnimation::finished, this, [this] {
        if (m_expanded && m_focusAfterMorph)
            m_prompt->setFocus(Qt::OtherFocusReason);
        m_focusAfterMorph = false;
    });
    m_host->installEventFilter(this);
    m_host->viewport()->installEventFilter(this);
    m_transcript->viewport()->installEventFilter(this);
    // Watches clicks anywhere in the window to collapse the panel when they land outside it.
    qApp->installEventFilter(this);
    updateSendButton();
    updateEngine();
    hide();
}

void KiraSparkle::setPresets(const QList<Choice>& presets, const QString& current)
{
    m_presets = presets;
    m_currentPreset = current;
    updateEngine();
}

void KiraSparkle::setQualities(const QList<Choice>& qualities, const QString& current)
{
    m_qualities = qualities;
    m_currentQuality = current;
    updateEngine();
}

void KiraSparkle::setEngineLocked(bool locked)
{
    m_engine->setEnabled(!locked);
}

void KiraSparkle::updateEngine()
{
    QStringList names;
    int preset = -1;
    for (int i = 0; i < m_presets.size(); ++i) {
        names.append(m_presets.at(i).name);
        if (m_presets.at(i).id == m_currentPreset) preset = i;
    }
    int quality = -1;
    for (int i = 0; i < m_qualities.size(); ++i)
        if (m_qualities.at(i).id == m_currentQuality) quality = i;
    const Choice presetChoice = m_presets.value(preset);
    const Choice qualityChoice = m_qualities.value(quality);
    // References the preset cannot use only serve edits: they show as ignored for creating.
    const bool ignoring = !presetChoice.references && !m_referenceStrip->references().isEmpty();
    m_referenceStrip->setIgnored(presetChoice.references ? QString()
        : tr("%1 cannot use reference images: a new image ignores them, only edits use them.")
              .arg(presetChoice.name));
    static_cast<EngineButton*>(m_engine)->setState(monograms(names).value(preset),
        preset < 0 || !presetChoice.issue.isEmpty() || ignoring, quality + 1,
        m_qualities.isEmpty() ? 3 : int(m_qualities.size()), quality < 0 || !qualityChoice.issue.isEmpty(),
        !baseImage().isNull());
    const auto describe = [](const Choice& choice) {
        return choice.id.isEmpty() ? tr("none")
            : choice.issue.isEmpty() ? choice.name
                                     : tr("%1 (%2)").arg(choice.name, choice.issue);
    };
    m_engine->setToolTip(tr("Creation: %1\nEditing: %2").arg(describe(presetChoice), describe(qualityChoice))
        + (ignoring ? QStringLiteral("\n") + tr("The creation preset ignores the reference images.") : QString()));
}

bool KiraSparkle::referencesUseless() const
{
    // Neither creating with the preset nor editing with the quality can use them.
    bool preset = true;
    for (const auto& choice : m_presets)
        if (choice.id == m_currentPreset) preset = choice.references;
    bool quality = false;
    for (const auto& choice : m_qualities)
        if (choice.id == m_currentQuality) quality = choice.issue.isEmpty();
    return !preset && !quality;
}

void KiraSparkle::showAttachMenu()
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("kiraSparkleAttachMenu"));
    menu.setToolTipsVisible(true);
    // The base image to change (replacing any current one), then the reference images.
    const bool hasBase = !baseImage().isNull();
    const bool useless = referencesUseless();
    const bool full = m_referenceStrip->isFull() || useless;
    const QString noImage = tr("No image is displayed.");
    const QString noRoom = useless
        ? tr("Neither the creation preset nor the editing quality can use reference images now.")
        : tr("Up to %n reference image(s): remove one first.", nullptr, ReferenceImageStrip::MaximumReferences);
    const auto add = [&](Ardoise::Icon icon, const QString& text, bool enabled, const QString& reason,
                         void (KiraSparkle::*requested)()) {
        auto* action = menu.addAction(Ardoise::IconProvider::icon(icon), text);
        action->setEnabled(enabled);
        action->setToolTip(reason);
        connect(action, &QAction::triggered, this, requested);
    };
    const QString replaces = hasBase ? tr("Replaces the current base image.") : QString();
    add(Ardoise::Icon::Upload, tr("Add a base image…"), true, replaces, &KiraSparkle::baseFileRequested);
    add(Ardoise::Icon::Image, tr("Use the displayed image as base"), m_contextAvailable,
        m_contextAvailable ? replaces : noImage, &KiraSparkle::baseContextRequested);
    menu.addSeparator();
    add(Ardoise::Icon::Upload, tr("Add reference images…"), !full, full ? noRoom : QString(),
        &KiraSparkle::referenceFilesRequested);
    add(Ardoise::Icon::Image, tr("Add the displayed image as reference"), !full && m_contextAvailable,
        full ? noRoom : m_contextAvailable ? QString() : noImage, &KiraSparkle::referenceContextRequested);
    add(Ardoise::Icon::Person, tr("Add an identity as reference…"), !full, full ? noRoom : QString(),
        &KiraSparkle::referenceIdentityRequested);
    // Opens above the button, aligned with its left edge.
    menu.exec(m_attach->mapToGlobal(QPoint(0, -menu.sizeHint().height() - metrics.spacing4)));
}

void KiraSparkle::showEngineMenu()
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    // The icon takes the check mark's place, so the current choice is bold; one that cannot
    // run says so on the right, with the reason as tooltip.
    const auto markChoice = [](QAction* action, bool current, const QString& issue) {
        action->setCheckable(true);
        action->setChecked(current);
        QFont font = action->font();
        font.setBold(current);
        action->setFont(font);
        action->setToolTip(issue);
        if (!issue.isEmpty())
            action->setText(action->text() + QLatin1Char('\t') + tr("Unavailable"));
    };
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("kiraSparkleEngineMenu"));
    menu.setToolTipsVisible(true);
    menu.addSection(tr("Creation"));
    QStringList names;
    for (const auto& preset : m_presets) names.append(preset.name);
    const QStringList codes = monograms(names);
    for (int i = 0; i < m_presets.size(); ++i) {
        const Choice preset = m_presets.at(i);
        const QString code = codes.at(i);
        const bool unavailable = !preset.issue.isEmpty();
        auto* action = menu.addAction(choiceIcon(this, [&](QPainter& painter, const QRectF& rect) {
            paintMonogram(painter, rect, code, unavailable, true);
        }), preset.name);
        markChoice(action, preset.id == m_currentPreset, preset.issue);
        if (!unavailable && !preset.references) {
            action->setText(action->text() + QLatin1Char('\t') + tr("No references"));
            action->setToolTip(tr("Its model cannot use reference images."));
        }
        connect(action, &QAction::triggered, this, [this, id = preset.id] { emit presetSelected(id); });
    }
    if (m_presets.isEmpty()) menu.addAction(tr("No presets available"))->setEnabled(false);
    menu.addSection(tr("Editing"));
    for (int i = 0; i < m_qualities.size(); ++i) {
        const Choice quality = m_qualities.at(i);
        const bool unavailable = !quality.issue.isEmpty();
        const int levels = int(m_qualities.size());
        auto* action = menu.addAction(choiceIcon(this, [&](QPainter& painter, const QRectF& rect) {
            paintLevel(painter, rect.adjusted(metrics.spacing2, metrics.spacing2, -metrics.spacing2, -metrics.spacing2),
                i + 1, levels, unavailable, true);
        }), quality.name);
        markChoice(action, quality.id == m_currentQuality, quality.issue);
        connect(action, &QAction::triggered, this, [this, id = quality.id] { emit qualitySelected(id); });
    }
    if (m_qualities.isEmpty()) menu.addAction(tr("No editing qualities available"))->setEnabled(false);
    // Opens above the button, aligned with its right edge.
    const QSize size = menu.sizeHint();
    menu.exec(m_engine->mapToGlobal(QPoint(m_engine->width() - size.width(), -size.height() - metrics.spacing4)));
}

void KiraSparkle::setSendEnabled(bool enabled, const QString& reason)
{
    m_sendEnabled = enabled;
    m_sendIssue = reason;
    updateSendButton();
}

void KiraSparkle::setBaseImage(const QImage& image)
{
    m_baseStrip->clearReferences();
    m_baseStrip->addReference(image);
}

void KiraSparkle::restoreRequest(const QString& request)
{
    if (!m_prompt->toPlainText().trimmed().isEmpty())
        return;
    m_prompt->setPlainText(request);
    m_prompt->moveCursor(QTextCursor::End);
}

void KiraSparkle::updateSendButton()
{
    const bool written = !m_prompt->toPlainText().trimmed().isEmpty();
    m_send->setEnabled(m_sendEnabled && written);
    m_send->setToolTip(!m_sendEnabled && !m_sendIssue.isEmpty() ? m_sendIssue
        : !written ? tr("Describe what you want to create or change.")
        : tr("Kira Sparkle chooses between a new image and an edit, then the framing and format"));
}

void KiraSparkle::send()
{
    if (!m_send->isEnabled())
        return;
    const QString request = m_prompt->toPlainText();
    // The attachments belong to the conversation: they stay beside the attach button
    // until removed or a new chat starts, and go with every request.
    m_prompt->clear();
    addMessage(true, request.trimmed());
    emit submitted(request, baseImage(), attachments());
}

void KiraSparkle::addReply(const QString& text)
{
    addMessage(false, text);
}

QUuid KiraSparkle::addProposal(const QString& text, const QString& details, const QString& action)
{
    const QUuid id = QUuid::createUuid();
    auto* item = new ChatMessage(false, text, m_transcript->widget(), id, action);
    item->label()->setToolTip(details);
    connect(item->button(), &QPushButton::clicked, this, [this, id] { emit proposalAccepted(id); });
    addItem(item, Qt::AlignLeft);
    return id;
}

void KiraSparkle::closeProposal(const QUuid& id, const QString& state)
{
    auto* item = static_cast<ChatMessage*>(result(id));
    if (!item || !item->button()) return;
    item->button()->setEnabled(false);
    item->button()->setText(state);
}

void KiraSparkle::setThinking(const QString& status, int percentage)
{
    if (!m_thinking) {
        m_thinking = new ChatThinking(m_transcript->widget());
        addItem(m_thinking, Qt::AlignLeft);
    }
    static_cast<ChatThinking*>(m_thinking)->setStatus(status, percentage);
}

void KiraSparkle::stopThinking()
{
    delete m_thinking;
    m_thinking = nullptr;
}

void KiraSparkle::addResult(const QUuid& id, const QSize& size, const QString& status)
{
    auto* item = new ChatResult(id, size, status, m_transcript->widget());
    item->opened = [this, item] { emit resultOpened(item->id(), item->image()); };
    item->menuRequested = [this, item](const QPoint& globalPos) {
        emit resultMenuRequested(item->id(), item->image(), globalPos);
    };
    addItem(item, Qt::AlignLeft);
}

void KiraSparkle::setResultProgress(const QUuid& id, const QString& status, int percentage)
{
    if (auto* item = static_cast<ChatResult*>(result(id)))
        item->setProgress(status, percentage);
}

void KiraSparkle::completeResult(const QUuid& id, const QImage& image)
{
    if (auto* item = static_cast<ChatResult*>(result(id)))
        item->complete(image);
}

void KiraSparkle::failResult(const QUuid& id, const QString& message)
{
    if (auto* item = static_cast<ChatResult*>(result(id)))
        item->fail(message);
}

void KiraSparkle::newChat()
{
    m_thinking = nullptr;
    while (m_messages->count() > 1)
        delete m_messages->takeAt(1)->widget();
    m_baseStrip->clearReferences();
    m_referenceStrip->clearReferences();
    emit conversationCleared();
    addMessage(false, tr("How can I help you?"));
    if (m_expanded)
        m_prompt->setFocus(Qt::OtherFocusReason);
}

QWidget* KiraSparkle::result(const QUuid& id) const
{
    for (int i = 0; !id.isNull() && i < m_messages->count(); ++i)
        if (auto* item = m_messages->itemAt(i)->widget(); item && static_cast<ChatItem*>(item)->id() == id)
            return item;
    return nullptr;
}

void KiraSparkle::addMessage(bool fromUser, const QString& text)
{
    addItem(new ChatMessage(fromUser, text, m_transcript->widget()),
        fromUser ? Qt::AlignRight : Qt::AlignLeft);
}

void KiraSparkle::addItem(QWidget* item, Qt::Alignment alignment)
{
    m_messages->addWidget(item, 0, alignment);
    fitMessages();
}

void KiraSparkle::fitMessages()
{
    // Entries take at most four fifths of the conversation's width.
    const int width = m_transcript->viewport()->width() * 4 / 5;
    for (int i = 0; i < m_messages->count(); ++i)
        if (auto* item = m_messages->itemAt(i)->widget())
            static_cast<ChatItem*>(item)->fitWidth(width);
}

void KiraSparkle::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (active) {
        show();
        raise();
    } else if (m_prompt->hasFocus()) {
        m_host->setFocus(Qt::OtherFocusReason);
    }
    m_opacity->setEnabled(true);
    m_reveal->stop();
    m_reveal->setDuration(active ? 300 : 180);
    m_reveal->setEasingCurve(active ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    m_reveal->setStartValue(m_revealValue);
    m_reveal->setEndValue(active ? 1.0 : 0.0);
    m_reveal->start();
}

void KiraSparkle::setExpanded(bool expanded, bool focusPrompt)
{
    if (m_expanded == expanded)
        return;
    m_expanded = expanded;
    static_cast<SparkleBubble*>(m_bubble)->setLit(expanded);
    if (expanded)
        raise();
    else if (m_prompt->hasFocus())
        m_bubble->setFocus(Qt::OtherFocusReason);
    m_focusAfterMorph = focusPrompt;
    m_morph->stop();
    m_morph->setEasingCurve(expanded ? QEasingCurve(QEasingCurve::OutBack) : QEasingCurve(QEasingCurve::InOutCubic));
    m_morph->setStartValue(m_morphValue);
    m_morph->setEndValue(expanded ? 1.0 : 0.0);
    m_morph->start();
    emit expandedChanged(expanded);
}

QRect KiraSparkle::bubbleRect() const
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    const QRect viewport = m_host->viewport()->geometry();
    const int bubble = metrics.controlHeightLarge;
    const int right = viewport.right() - metrics.spacing16;
    const int bottom = viewport.bottom() - metrics.spacing16 + qRound((1 - m_revealValue) * RevealTravel);
    return { right - bubble + 1, bottom - bubble + 1, bubble, bubble };
}

QRect KiraSparkle::panelRect(qreal morph) const
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    const QRect viewport = m_host->viewport()->geometry();
    const QRect bubble = bubbleRect();
    const int room = bubble.left() - metrics.spacing8 - viewport.left() - metrics.spacing16;
    const int height = bubble.bottom() + 1 - viewport.top() - metrics.spacing16;
    // The chat takes the full height above the bubble's bottom edge, following the viewport.
    const QSize full(qMax(qMin(room, MaximumWidth), 240), qMax(height, m_panel->minimumSizeHint().height()));
    // The panel grows up and to the left from the bubble, overshooting slightly in width.
    const qreal t = qMax(0.0, morph);
    const QSize size(qRound(bubble.width() + (full.width() - bubble.width()) * t),
        qRound(bubble.height() + (full.height() - bubble.height()) * qMin(1.0, t)));
    const QPoint bottomRight(bubble.left() - metrics.spacing8, bubble.bottom());
    return { bottomRight - QPoint(size.width() - 1, size.height() - 1), size };
}

void KiraSparkle::relayout()
{
    const int margin = shadowMargin();
    const QRect bubble = bubbleRect();
    const QRect panel = panelRect(m_morphValue);
    const bool panelShown = m_morphValue > 0;
    const QRect bounds = (panelShown ? bubble.united(panel) : bubble).adjusted(-margin, -margin, margin, margin);
    setGeometry(bounds);
    // Keep the content at full size, anchored to the growing shape's bottom-right corner.
    const QSize full(panelRect(1).size());
    const QPoint origin = -bounds.topLeft();
    m_panel->setGeometry(QRect(panel.bottomRight() + origin - QPoint(full.width() - 1, full.height() - 1), full));
    m_panel->setVisible(m_morphValue > 0.55);
    m_bubble->move(bubble.topLeft() + origin);
    // Only the bubble and the panel take clicks; the space around them belongs to the preview.
    QRegion mask(bubble.translated(origin).adjusted(-margin, -margin, margin, margin));
    if (panelShown)
        mask += panel.translated(origin).adjusted(-margin, -margin, margin, margin);
    setMask(mask);
    update();
}

bool KiraSparkle::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == m_host || watched == m_host->viewport()) && event->type() == QEvent::Resize)
        relayout();
    if (watched == m_transcript->viewport() && event->type() == QEvent::Resize)
        fitMessages();
    // A click elsewhere in this window collapses the panel; dialogs it opens do not.
    if (m_expanded && event->type() == QEvent::MouseButtonPress && watched->isWidgetType()) {
        auto* target = static_cast<QWidget*>(watched);
        if (target->window() == window() && target != this && !isAncestorOf(target))
            setExpanded(false);
    }
    if (watched == m_prompt) {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)
            update();
        if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                setExpanded(false);
                return true;
            }
            // Enter (including keypad Enter) sends; Shift+Enter inserts a newline.
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
                && (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
                send();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void KiraSparkle::wheelEvent(QWheelEvent* event) { event->accept(); }

void KiraSparkle::mousePressEvent(QMouseEvent* event) { event->accept(); }

void KiraSparkle::paintEvent(QPaintEvent*)
{
    const auto& tokens = Ardoise::Theme::tokens();
    const int margin = shadowMargin();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPoint origin = -geometry().topLeft();
    const auto local = [&](const QRect& rect) { return QRectF(rect.translated(origin)).adjusted(0.5, 0.5, -0.5, -0.5); };
    if (m_morphValue > 0) {
        const QRectF panel = local(panelRect(m_morphValue));
        const qreal bubbleRadius = tokens.metrics.controlHeightLarge / 2.0;
        const qreal panelRadius = tokens.metrics.radiusLarge + tokens.metrics.spacing4;
        const qreal morph = qBound(0.0, m_morphValue, 1.0);
        const qreal radius = qMin(panel.height() / 2, bubbleRadius + (panelRadius - bubbleRadius) * morph);
        FloatingSurface::paint(painter, panel, radius, margin, m_prompt->hasFocus() && m_panel->isVisible());
    }
    // The bubble's shadow and fill, then its own ring, glow and stars on top.
    const QRectF bubble(bubbleRect().translated(origin));
    FloatingSurface::paint(painter, bubble, bubble.height() / 2, margin, false);
    static_cast<SparkleBubble*>(m_bubble)->paintBubble(painter, bubble);
}
