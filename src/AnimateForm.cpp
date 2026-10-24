#include "AnimateForm.h"
#include "FloatingSurface.h"
#include "ReferenceImageStrip.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/IconProvider.h>
#include <Ardoise/PlainTextEdit.h>
#include <Ardoise/PrimaryButton.h>
#include <Ardoise/Theme.h>
#include <QAbstractScrollArea>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QLocale>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace
{
// Room around the form's shape for its painted shadow, as for the edit bubble.
int shadowMargin() { return Ardoise::Theme::tokens().metrics.spacing8; }
constexpr int RevealTravel = 28;
constexpr int MaximumWidth = 680;

// The length presets, shortest first, and the frame rate used when the model has no fps default.
constexpr int PresetFrames[] = {33, 41, 81};
constexpr int MediumPreset = 1;
constexpr int DefaultFrameRate = 16;

// A film frame with a play triangle, drawn with the theme's icon colours.
QPixmap animatePixmap(int side, const QColor& color)
{
    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal stroke = qMax(1.0, side / 12.0);
    const QRectF frame = QRectF(0, 0, side, side).adjusted(side * 0.1, side * 0.18, -side * 0.1, -side * 0.18);
    painter.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(frame, side * 0.12, side * 0.12);
    QPainterPath play;
    const QPointF center = frame.center();
    const qreal r = frame.height() * 0.26;
    play.moveTo(center.x() - r * 0.75, center.y() - r);
    play.lineTo(center.x() + r, center.y());
    play.lineTo(center.x() - r * 0.75, center.y() + r);
    play.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPath(play);
    return pixmap;
}
}

QIcon AnimateForm::icon(bool accent)
{
    const auto& colors = Ardoise::Theme::tokens().colors;
    QIcon icon;
    for (const int side : {16, 20, 24, 32, 48, 64}) {
        icon.addPixmap(animatePixmap(side, accent ? colors.borderAccent : colors.textPrimary), QIcon::Normal);
        icon.addPixmap(animatePixmap(side, colors.textDisabled), QIcon::Disabled);
    }
    return icon;
}

AnimateForm::AnimateForm(QAbstractScrollArea* host)
    : QWidget(host)
    , m_host(host)
    , m_reveal(new QVariantAnimation(this))
    , m_opacity(new QGraphicsOpacityEffect(this))
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    setObjectName(QStringLiteral("animateForm"));
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
    icon->setPixmap(AnimateForm::icon(true).pixmap(metrics.iconSize, metrics.iconSize));
    header->addWidget(icon);
    auto* title = new QLabel(tr("Animate image"), m_panel);
    Ardoise::Theme::applyTypography(title, Ardoise::TextRole::BodyStrong);
    header->addWidget(title);
    header->addStretch();
    m_model = new QComboBox(m_panel);
    m_model->setObjectName(QStringLiteral("animateModel"));
    m_model->setAccessibleName(tr("Animation model"));
    m_model->setToolTip(tr("Animation model"));
    m_model->hide();
    header->addWidget(m_model);
    m_loop = new QCheckBox(tr("Loop"), m_panel);
    m_loop->setObjectName(QStringLiteral("animateLoop"));
    m_loop->setToolTip(tr("Play the motion forward, then backward, so that the video loops seamlessly"));
    m_loop->hide();
    header->addWidget(m_loop);
    header->addSpacing(metrics.spacing4);
    auto* close = new Ardoise::IconButton(Ardoise::Icon::Close, tr("Close"), m_panel);
    close->setObjectName(QStringLiteral("animateClose"));
    close->setProperty("kiraFlat", true);
    header->addWidget(close, 0, Qt::AlignTop);
    layout->addLayout(header);
    // The length choice: three exclusive buttons, and what the chosen length means.
    auto* lengthRow = new QHBoxLayout;
    lengthRow->setSpacing(metrics.spacing8);
    m_length = new QButtonGroup(this);
    m_length->setExclusive(true);
    const QString labels[] = {tr("Short"), tr("Medium"), tr("Long")};
    for (int i = 0; i < 3; ++i) {
        auto* button = new QPushButton(labels[i], m_panel);
        button->setObjectName(QStringLiteral("animateLength%1").arg(i));
        button->setCheckable(true);
        button->setProperty("kiraFlat", true);
        button->setToolTip(tr("Length of the animation"));
        m_length->addButton(button, i);
        lengthRow->addWidget(button);
    }
    m_length->button(MediumPreset)->setChecked(true);
    m_summary = new QLabel(m_panel);
    m_summary->setObjectName(QStringLiteral("animateSummary"));
    lengthRow->addSpacing(metrics.spacing4);
    lengthRow->addWidget(m_summary, 1);
    layout->addLayout(lengthRow);
    // One end image at most, shown without a number like a base image.
    m_endImage = new ReferenceImageStrip(QStringLiteral("animateRemoveEndImage"), m_panel);
    m_endImage->setObjectName(QStringLiteral("animateEndImage"));
    m_endImage->setMaximum(1);
    m_endImage->setBase(true);
    m_endImage->setCountVisible(false);
    layout->addWidget(m_endImage);
    auto* inputRow = new QHBoxLayout;
    inputRow->setSpacing(metrics.spacing8);
    m_prompt = new Ardoise::PlainTextEdit(m_panel);
    m_prompt->setObjectName(QStringLiteral("animatePrompt"));
    m_prompt->setPlaceholderText(tr("Describe the motion…"));
    m_prompt->setAccessibleName(tr("Motion prompt"));
    const int fieldHeight = qMax(m_prompt->heightForLines(3), 2 * metrics.controlHeight + metrics.spacing8);
    m_prompt->setFixedHeight(fieldHeight);
    m_prompt->installEventFilter(this);
    const int tile = (fieldHeight - metrics.spacing8) / 2;
    m_attach = new Ardoise::IconButton(Ardoise::Icon::Paperclip, tr("Attach an end image"), m_panel);
    m_attach->setObjectName(QStringLiteral("animateAttach"));
    m_attach->setToolTip(tr("Attach an end image: the video moves from the selected image to it"));
    m_attach->setFixedSize(tile, tile);
    auto* tools = new QVBoxLayout;
    tools->addWidget(m_attach);
    tools->addStretch();
    inputRow->addLayout(tools);
    inputRow->addWidget(m_prompt, 1);
    m_submit = new Ardoise::PrimaryButton(tr("Animate"), m_panel);
    m_submit->setObjectName(QStringLiteral("animateButton"));
    m_submit->setIcon(AnimateForm::icon());
    m_submit->setFixedHeight(fieldHeight);
    m_submit->setMinimumWidth(qMax(m_submit->sizeHint().width(), fieldHeight + metrics.spacing24));
    m_submit->setEnabled(false);
    inputRow->addWidget(m_submit);
    layout->addLayout(inputRow);

    connect(m_endImage, &ReferenceImageStrip::referencesChanged, this, [this] {
        m_attach->setEnabled(!m_endImage->isFull());
        m_panel->layout()->invalidate();
        relayout();
    });
    connect(m_attach, &QToolButton::clicked, this, &AnimateForm::endImageRequested);
    connect(close, &QToolButton::clicked, this, &AnimateForm::closeRequested);
    connect(m_prompt, &QPlainTextEdit::textChanged, this, &AnimateForm::updateSubmit);
    connect(m_submit, &QPushButton::clicked, this, &AnimateForm::submitted);
    connect(m_model, &QComboBox::currentIndexChanged, this, &AnimateForm::selectModel);
    connect(m_length, &QButtonGroup::idClicked, this, &AnimateForm::updateSummary);
    connect(m_loop, &QCheckBox::toggled, this, &AnimateForm::updateSummary);
    updateSummary();

    m_reveal->setStartValue(0.0);
    m_reveal->setEndValue(1.0);
    connect(m_reveal, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_revealValue = value.toReal();
        m_opacity->setOpacity(m_revealValue);
        relayout();
    });
    connect(m_reveal, &QVariantAnimation::finished, this, [this] {
        if (!m_active) hide();
        m_opacity->setEnabled(m_revealValue < 1);
    });
    m_host->installEventFilter(this);
    m_host->viewport()->installEventFilter(this);
    hide();
}

QImage AnimateForm::endImage() const
{
    const auto images = m_endImage->references();
    return images.isEmpty() ? QImage() : images.first();
}

bool AnimateForm::setEndImage(const QImage& image)
{
    return m_endImage->addReference(image);
}

void AnimateForm::setModels(const QList<Model>& models)
{
    const QString previous = modelId();
    m_models = models;
    {
        const QSignalBlocker blocker(m_model);
        m_model->clear();
        for (const auto& model : models)
            m_model->addItem(model.label, model.id);
        const int kept = m_model->findData(previous);
        m_model->setCurrentIndex(kept >= 0 ? kept : 0);
    }
    // The selector only offers a choice when there is one.
    m_model->setVisible(models.size() > 1);
    if (modelId() != previous)
        selectModel(m_model->currentIndex());
    // The kept model's defaults may have changed: the summary follows them, the choice stays.
    updateSummary();
    updateSubmit();
}

QString AnimateForm::modelId() const
{
    return m_model->currentData().toString();
}

void AnimateForm::selectModel(int index)
{
    // What the switch shows is what runs: it starts at the model's own default.
    if (index >= 0 && index < m_models.size()) {
        const auto& model = m_models.at(index);
        m_loop->setChecked(model.pingPong);
        // Likewise the length: the preset nearest to the model's frame count, the shorter on a tie.
        int preset = MediumPreset;
        if (model.videoFrames) {
            preset = 0;
            for (int i = 1; i < 3; ++i)
                if (qAbs(PresetFrames[i] - *model.videoFrames) < qAbs(PresetFrames[preset] - *model.videoFrames))
                    preset = i;
        }
        m_length->button(preset)->setChecked(true);
    }
    updateSummary();
}

int AnimateForm::frameCount() const
{
    return PresetFrames[qBound(0, m_length->checkedId(), 2)];
}

int AnimateForm::frameRate() const
{
    const int index = m_model->currentIndex();
    return index >= 0 && index < m_models.size() ? m_models.at(index).fps.value_or(DefaultFrameRate)
                                                 : DefaultFrameRate;
}

QAbstractButton* AnimateForm::lengthButton(int index) const
{
    return m_length->button(index);
}

void AnimateForm::updateSummary()
{
    const auto line = [this](int frames) {
        return tr("%n frame(s) · %1 s", nullptr, frames)
            .arg(QLocale().toString(double(frames) / frameRate(), 'f', 1));
    };
    QString text = line(frameCount());
    // A loop plays the frames forward, then backward without repeating the two ends.
    if (loop())
        text += QLatin1Char('\n') + tr("Loop: %1").arg(line(2 * frameCount() - 2));
    m_summary->setText(text);
    m_panel->layout()->invalidate();
    relayout();
}

void AnimateForm::setPingPongAvailable(bool available)
{
    m_pingPongAvailable = available;
    m_loop->setVisible(available);
    updateSummary();
}

bool AnimateForm::loop() const
{
    return m_pingPongAvailable && m_loop->isChecked();
}

void AnimateForm::setSubmitAvailable(bool available, const QString& issue)
{
    m_submitAvailable = available;
    m_submitIssue = issue;
    updateSubmit();
}

void AnimateForm::updateSubmit()
{
    const bool written = !m_prompt->toPlainText().trimmed().isEmpty();
    m_submit->setEnabled(m_submitAvailable && written && !modelId().isEmpty());
    m_submit->setToolTip(!m_submitIssue.isEmpty() ? m_submitIssue
        : !written ? tr("Describe the motion to animate the image.") : QString());
}

void AnimateForm::clearDraft()
{
    m_prompt->clear();
    m_endImage->clearReferences();
}

void AnimateForm::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (active) {
        show();
        raise();
        m_prompt->setFocus(Qt::OtherFocusReason);
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

void AnimateForm::relayout()
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    const int margin = shadowMargin();
    const QRect viewport = m_host->viewport()->geometry();
    const int width = qMax(qMin(viewport.width() - 2 * metrics.spacing16, MaximumWidth), 240);
    const QSize size(width, m_panel->sizeHint().height());
    const int bottom = viewport.bottom() - metrics.spacing16 + qRound((1 - m_revealValue) * RevealTravel);
    const QRect shape(QPoint(viewport.center().x() - size.width() / 2, bottom - size.height() + 1), size);
    setGeometry(shape.adjusted(-margin, -margin, margin, margin));
    m_panel->setGeometry(margin, margin, size.width(), size.height());
    emit coveredRectChanged(isVisible() && m_revealValue > 0 ? shape.translated(-viewport.topLeft()) : QRect());
    update();
}

bool AnimateForm::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == m_host || watched == m_host->viewport()) && event->type() == QEvent::Resize)
        relayout();
    if (watched == m_prompt) {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)
            update();
        if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                emit closeRequested();
                return true;
            }
            // Enter submits only when Animate is enabled; Shift+Enter inserts a new line.
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
                && (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
                if (m_submit->isEnabled()) m_submit->click();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AnimateForm::wheelEvent(QWheelEvent* event) { event->accept(); }

void AnimateForm::mousePressEvent(QMouseEvent* event) { event->accept(); }

void AnimateForm::paintEvent(QPaintEvent*)
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    const int margin = shadowMargin();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF shape = QRectF(rect()).adjusted(margin + 0.5, margin + 0.5, -margin - 0.5, -margin - 0.5);
    FloatingSurface::paint(painter, shape, metrics.radiusLarge + metrics.spacing4, margin, m_prompt->hasFocus());
}
