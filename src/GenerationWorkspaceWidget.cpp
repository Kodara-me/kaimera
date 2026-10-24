#include "GenerationWorkspaceWidget.h"
#include <cmath>
#include "EditBubble.h"
#include "GenerationCard.h"
#include "GenerationInspectorWidget.h"
#include "GenerationParametersWidget.h"
#include "IdentityPanel.h"
#include "IdentityPrompts.h"
#include "JobActivityIndicator.h"
#include "KiraSparkle.h"
#include "QualityStepSlider.h"
#include "VideoFrames.h"
#include <nukleus/Identity.h>
#include <QFile>
#include <QSignalBlocker>
#include "GenerationHistoryWidget.h"
#include "GenerationHistoryView.h"
#include "GenerationHistoryModel.h"
#include "OutputSettingsWidget.h"
#include <QClipboard>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QFileDialog>
#include <QDesktopServices>
#include <QMessageBox>
#include <QUrl>
#include "GenerationPreviewWidget.h"
#include <Ardoise/IconButton.h>
#include <Ardoise/Panel.h>
#include <Ardoise/Theme.h>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QImageWriter>
#include <Ardoise/PlainTextEdit.h>
#include <Ardoise/PrimaryButton.h>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
double lanczos(double x)
{
    constexpr double a = 3.0;
    if (x == 0.0) return 1.0;
    if (x <= -a || x >= a) return 0.0;
    const double px = M_PI * x;
    return a * std::sin(px) * std::sin(px / a) / (px * px);
}

// Separable Lanczos-3 resampling (Qt has no Lanczos filter); alpha is premultiplied.
QImage lanczosResize(const QImage& source, const QSize& size)
{
    const QImage src = source.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const int sw = src.width(), sh = src.height(), dw = size.width(), dh = size.height();

    const auto resample = [](int srcLen, int dstLen) {
        struct Tap { int first; QList<float> weights; };
        QList<Tap> taps;
        const double scale = double(srcLen) / dstLen;
        const double filterScale = qMax(1.0, scale);
        for (int i = 0; i < dstLen; ++i) {
            const double center = (i + 0.5) * scale;
            const int first = qMax(0, int(std::floor(center - 3.0 * filterScale)));
            const int last = qMin(srcLen - 1, int(std::ceil(center + 3.0 * filterScale)));
            Tap tap{ first, {} };
            double sum = 0.0;
            for (int j = first; j <= last; ++j) {
                const double w = lanczos((j + 0.5 - center) / filterScale);
                tap.weights.append(float(w));
                sum += w;
            }
            for (float& w : tap.weights) w = float(w / sum);
            taps.append(tap);
        }
        return taps;
    };
    const auto xTaps = resample(sw, dw);
    const auto yTaps = resample(sh, dh);

    QList<float> tmp(qsizetype(dw) * sh * 4);
    for (int y = 0; y < sh; ++y) {
        const uchar* row = src.constScanLine(y);
        for (int x = 0; x < dw; ++x) {
            float acc[4] = { 0, 0, 0, 0 };
            const auto& tap = xTaps[x];
            for (int k = 0; k < tap.weights.size(); ++k)
                for (int c = 0; c < 4; ++c)
                    acc[c] += tap.weights[k] * row[(tap.first + k) * 4 + c];
            for (int c = 0; c < 4; ++c) tmp[(qsizetype(y) * dw + x) * 4 + c] = acc[c];
        }
    }
    QImage result(size, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < dh; ++y) {
        uchar* out = result.scanLine(y);
        const auto& tap = yTaps[y];
        for (int x = 0; x < dw; ++x) {
            float acc[4] = { 0, 0, 0, 0 };
            for (int k = 0; k < tap.weights.size(); ++k)
                for (int c = 0; c < 4; ++c)
                    acc[c] += tap.weights[k] * tmp[(qsizetype(tap.first + k) * dw + x) * 4 + c];
            const int a = qBound(0, int(std::lround(acc[3])), 255);
            out[x * 4 + 3] = uchar(a);
            for (int c = 0; c < 3; ++c) out[x * 4 + c] = uchar(qBound(0, int(std::lround(acc[c])), a));
        }
    }
    return result;
}
}

GenerationWorkspaceWidget::GenerationWorkspaceWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumWidth(360);
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(metrics.spacing8);
    auto* content = new Ardoise::Panel(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(metrics.spacing12, metrics.spacing12, metrics.spacing12, metrics.spacing12);
    layout->setSpacing(metrics.spacing12);
    m_preview = new GenerationPreviewWidget(this);
    auto* tools = new Ardoise::Panel(this);
    tools->setObjectName("imageEditingToolbar");
    auto* toolsLayout = new QVBoxLayout(tools);
    toolsLayout->setContentsMargins(metrics.spacing4, metrics.spacing4, metrics.spacing4, metrics.spacing4);
    toolsLayout->setSpacing(metrics.spacing2);
    tools->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    const auto tool = [&](Ardoise::Icon icon, const QString& label, const QString& name) {
        auto* button = new Ardoise::IconButton(icon, label, tools);
        button->setObjectName(name);
        button->setProperty("kiraFlat", true);
        toolsLayout->addWidget(button);
        m_imageTools.append(button);
        return button;
    };
    const auto separator = [&] {
        auto* line = new QFrame(tools);
        line->setFrameShape(QFrame::HLine);
        toolsLayout->addWidget(line);
    };
    const auto alwaysEnabledTool = [&](Ardoise::Icon icon, const QString& label, const QString& name) {
        auto* button = new Ardoise::IconButton(icon, label, tools);
        button->setObjectName(name);
        button->setProperty("kiraFlat", true);
        toolsLayout->addWidget(button);
        return button;
    };
    m_newImageButton = alwaysEnabledTool(Ardoise::Icon::New, tr("New Image"), "imageNew");
    m_newImageButton->setCheckable(true);
    connect(m_newImageButton, &QToolButton::toggled, this, &GenerationWorkspaceWidget::setGenerationOpen);
    connect(alwaysEnabledTool(Ardoise::Icon::Open, tr("Open…"), "imageOpen"), &QToolButton::clicked,
        this, &GenerationWorkspaceWidget::openRequested);
    connect(tool(Ardoise::Icon::Save, tr("Save as…"), "imageSaveAs"), &QToolButton::clicked,
        this, &GenerationWorkspaceWidget::saveAsRequested);
    separator();
    m_fitButton = tool(Ardoise::Icon::Fullscreen, tr("Fit image / actual size"), "imageFitToggle");
    m_fitButton->setCheckable(true);
    const bool fitToView = QSettings().value(QStringLiteral("Preview/FitToView"), false).toBool();
    if (fitToView) m_preview->fitImage();
    m_fitButton->setChecked(fitToView);
    connect(m_fitButton, &QToolButton::toggled, this, [this](bool fit) {
        if (fit) m_preview->fitImage(); else m_preview->actualSize();
    });
    connect(m_preview, &GenerationPreviewWidget::viewModeChanged, this, [this](bool fit) {
        QSignalBlocker blocker(m_fitButton);
        m_fitButton->setChecked(fit);
        QSettings().setValue(QStringLiteral("Preview/FitToView"), fit);
    });
    connect(tool(Ardoise::Icon::ZoomOut, tr("Zoom out"), "imageZoomOut"), &QToolButton::clicked,
        m_preview, &GenerationPreviewWidget::zoomOut);
    connect(tool(Ardoise::Icon::ZoomIn, tr("Zoom in"), "imageZoomIn"), &QToolButton::clicked,
        m_preview, &GenerationPreviewWidget::zoomIn);
    connect(tool(Ardoise::Icon::ActualSize, tr("Actual size (100%)"), "imageActualSize"), &QToolButton::clicked,
        m_preview, &GenerationPreviewWidget::actualSize);
    separator();
    m_cropButton = tool(Ardoise::Icon::Crop, tr("Crop: drag a rectangle; Escape to cancel"), "imageCrop");
    m_cropButton->setCheckable(true);
    connect(m_cropButton, &QToolButton::toggled, m_preview, &GenerationPreviewWidget::setCropEnabled);
    connect(m_preview, &GenerationPreviewWidget::cropModeChanged, this, [this](bool enabled) {
        QSignalBlocker blocker(m_cropButton);
        m_cropButton->setChecked(enabled);
    });
    connect(m_preview, &GenerationPreviewWidget::imageCropped, this, [this](const QImage& image) {
        appendImage(image, tr("Cropped image"));
    });
    m_upscaleButton = tool(Ardoise::Icon::Upscale, tr("Upscale 2×"), "imageUpscale");
    connect(m_upscaleButton, &QToolButton::clicked, this, [this] { emit upscaleRequested(m_current); });
    m_downscaleButton = tool(Ardoise::Icon::Downscale, tr("Downscale 2×"), "imageDownscale");
    connect(m_downscaleButton, &QToolButton::clicked, this, [this] {
        const QSize size(m_current.width() / 2, m_current.height() / 2);
        if (size.isEmpty()) return;
        appendImage(lanczosResize(m_current, size), tr("Downscaled image"));
    });
    // Sharpens the selected image with the default editing model and fixed prompts.
    m_sharpenButton = tool(Ardoise::Icon::Sharpen, tr("Improve sharpness"), "imageSharpen");
    connect(m_sharpenButton, &QToolButton::clicked, this, [this] { emit sharpenRequested(m_current); });
    // Toggles the Animate form over the selected still image.
    m_animateButton = tool(Ardoise::Icon::Image, tr("Animate"), "imageAnimate");
    m_animateButton->setIcon(AnimateForm::icon());
    m_animateButton->setCheckable(true);
    connect(m_animateButton, &QToolButton::toggled, this, &GenerationWorkspaceWidget::setAnimateOpen);
    separator();
    m_identityButton = alwaysEnabledTool(Ardoise::Icon::Person, tr("Create identity from a prompt…"), "imageCreateIdentity");
    m_identityButton->setCheckable(true);
    // Edits the selected image with an identity's references and edit instruction, no questions asked.
    m_identityEditButton = tool(Ardoise::Icon::IdentityEdit, tr("Edit with an identity…"), "imageEditWithIdentity");
    connect(m_identityEditButton, &QToolButton::clicked, this, [this] {
        QList<QImage> references;
        const QImage source = m_current;
        // Without a quality choice, identity edits use the service's default quality.
        if (const auto identity = chooseIdentity(&references); identity && !source.isNull())
            emit editRequested(source, identity->editPrompt, references, m_defaultEditQuality, false);
    });
    toolsLayout->addStretch();
    separator();
    connect(alwaysEnabledTool(Ardoise::Icon::Settings, tr("Service Preferences…"), "servicePreferencesButton"),
        &QToolButton::clicked, this, &GenerationWorkspaceWidget::servicePreferencesRequested);
    outer->addWidget(tools);
    outer->addWidget(content, 1);
    m_toolbar = tools;
    m_content = content;
    layout->addWidget(m_preview, 1);
    m_editBubble = new EditBubble(m_preview);
    m_editPrompt = m_editBubble->prompt();
    m_editButton = m_editBubble->submitButton();
    m_editQuality = m_editBubble->quality();
    connect(m_editQuality, &QSlider::valueChanged, this, [this] {
        if (!editQualityId().isEmpty()) m_selectedEditQuality = editQualityId();
        updateEditButton();
        updateSparkleQualities();
    });
    connect(m_editBubble, &EditBubble::coveredRectChanged, m_preview,
        [this](const QRect& rect) { m_preview->setOverlayObstacle(rect, m_editBubble); });
    m_sparkle = new KiraSparkle(m_preview);
    m_sparkle->setActive(true);
    // Kira Sparkle's base image comes from disk or the displayed image; its references from
    // disk, the displayed image or an identity, rejecting any addition beyond four.
    connect(m_sparkle, &KiraSparkle::baseFileRequested, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Choose a Base Image"), {}, imageFileFilter());
        if (path.isEmpty()) return;
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const auto image = reader.read();
        if (image.isNull())
            QMessageBox::warning(this, tr("Could not add the base image"),
                QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), reader.errorString()));
        else
            m_sparkle->setBaseImage(image);
    });
    connect(m_sparkle, &KiraSparkle::baseContextRequested, this, [this] {
        if (!m_current.isNull()) m_sparkle->setBaseImage(m_current);
    });
    connect(m_sparkle, &KiraSparkle::referenceFilesRequested, this,
        [this] { addReferenceImages(m_sparkle->referenceStrip(), true); });
    connect(m_sparkle, &KiraSparkle::referenceContextRequested, this, [this] {
        if (!m_current.isNull()) addReferences(m_sparkle->referenceStrip(), {m_current}, {}, true);
    });
    connect(m_sparkle, &KiraSparkle::referenceIdentityRequested, this, [this] {
        QList<QImage> references;
        if (chooseIdentity(&references)) addReferences(m_sparkle->referenceStrip(), references, {}, true);
    });
    connect(m_sparkle, &KiraSparkle::submitted, this, &GenerationWorkspaceWidget::sparkleRequested);
    connect(m_sparkle, &KiraSparkle::resultOpened, this,
        [this](const QUuid&, const QImage& image) { openExternally(image); });
    connect(m_sparkle, &KiraSparkle::resultMenuRequested, this,
        [this](const QUuid& id, const QImage& image, const QPoint& globalPos) { showImageMenu(id, image, globalPos, true); });
    // Kira Sparkle's engine button shares the edit bubble's quality (and the card's preset).
    connect(m_sparkle, &KiraSparkle::qualitySelected, this, [this](const QString& id) {
        const int index = int(m_editQualityIds.indexOf(id));
        if (index >= 0) m_editQuality->setValue(index);
    });
    // Kira Sparkle and the edit bubble share the bottom of the preview: only one stays open.
    connect(m_sparkle, &KiraSparkle::expandedChanged, this, [this](bool expanded) {
        if (expanded) m_editBubble->setExpanded(false);
    });
    connect(m_editBubble, &EditBubble::expandedChanged, this, [this](bool expanded) {
        if (expanded) m_sparkle->setExpanded(false);
    });
    m_animateIssue = tr("Connect to the service to animate images.");
    m_animateForm = new AnimateForm(m_preview);
    connect(m_animateForm, &AnimateForm::coveredRectChanged, m_preview,
        [this](const QRect& rect) { m_preview->setOverlayObstacle(rect, m_animateForm); });
    connect(m_animateForm, &AnimateForm::closeRequested, this, [this] { setAnimateOpen(false); });
    connect(m_animateForm, &AnimateForm::endImageRequested, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Choose an End Image"), {}, imageFileFilter());
        if (path.isEmpty()) return;
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const auto image = reader.read();
        if (image.isNull())
            QMessageBox::warning(this, tr("Could not add the end image"),
                QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), reader.errorString()));
        else
            m_animateForm->setEndImage(image);
    });
    connect(m_animateForm, &AnimateForm::submitted, this, [this] {
        if (m_current.isNull()) return;
        const QString prompt = m_animateForm->prompt()->toPlainText();
        const QImage end = m_animateForm->endImage();
        const QString model = m_animateForm->modelId();
        const bool sendPingPong = m_animateForm->pingPongAvailable();
        const bool pingPong = m_animateForm->loop();
        const int frames = m_animateForm->frameCount();
        const int fps = m_animateForm->frameRate();
        m_animateForm->clearDraft();
        setAnimateOpen(false);
        emit animateRequested(m_current, end, prompt, model, sendPingPong, pingPong, frames, fps);
    });
    // Kira Sparkle's panel and the Animate form share the bottom of the preview.
    connect(m_sparkle, &KiraSparkle::expandedChanged, this, [this](bool expanded) {
        if (expanded) setAnimateOpen(false);
    });
    m_activity = new JobActivityIndicator(m_preview);
    m_editPrompt->installEventFilter(this);
    connect(m_editPrompt, &QPlainTextEdit::textChanged, this, [this] {
        updateEditButton();
    });
    connect(m_editBubble->improveButton(), &QToolButton::clicked, this,
        [this] { emit editImproveRequested(m_editPrompt->toPlainText()); });
    connect(m_editBubble->attachButton(), &QToolButton::clicked, this,
        [this] { addReferenceImages(m_editBubble->referenceStrip()); });
    connect(m_editButton, &QPushButton::clicked, this, [this] {
        const QString prompt = m_editPrompt->toPlainText();
        const QList<QImage> references = m_editBubble->references();
        m_editPrompt->clear();
        m_editBubble->clearReferences();
        m_editBubble->setExpanded(false);
        emit editRequested(m_current, prompt, references, editQualityId(), true);
    });
    m_history = new GenerationHistoryWidget(this);
    layout->addWidget(m_history);
    m_preview->viewport()->installEventFilter(this);
    m_history->view()->viewport()->installEventFilter(this);
    connect(m_history, &GenerationHistoryWidget::imageSelected, this,
        [this](const QImage& image)
        {
            m_current = image;
            m_currentAnimation = {};
            updateEditButton();
            updateEditBubble();
            m_preview->setImage(image);
            updateImageTools();
            emit imageSelectionChanged(!image.isNull());
        });
    connect(m_history, &GenerationHistoryWidget::animationSelected, this, &GenerationWorkspaceWidget::selectAnimation);
    updateImageTools();
}

void GenerationWorkspaceWidget::selectAnimation(const QUuid& id)
{
    // An animation plays in the preview; it is not an image to edit, crop or animate again.
    m_current = {};
    m_currentAnimation = id;
    updateEditButton();
    updateEditBubble();
    const auto animation = m_history->model()->animation(id);
    m_preview->setAnimation(animation.video, animation.size, animation.frames, animation.fps);
    updateImageTools();
    emit imageSelectionChanged(true);
}

void GenerationWorkspaceWidget::completePendingAnimation(const QUuid& id, const Nukleus::AnimationResult& result,
    const QString& prompt)
{
    if (!result.succeeded) {
        failPendingImage(id, result.message);
        return;
    }
    // The thumbnail shows the first frame, decoded before the entry is ready.
    VideoFrames::readFirstFrame(result.videoData, this, [this, id, result, prompt](const QImage& frame) {
        if (frame.isNull()) {
            failPendingImage(id, tr("The service returned an unreadable video."));
            return;
        }
        m_history->model()->completeAnimation(id,
            {result.videoData, result.mimeType, result.frames, result.fps, frame.size()}, frame, prompt);
    });
}

bool GenerationWorkspaceWidget::saveAnimation(const QString& path, QString* error) const
{
    const QByteArray video = m_history->model()->animation(m_currentAnimation).video;
    if (video.isEmpty()) {
        if (error) *error = tr("No animation selected.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(video) != video.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QSize GenerationWorkspaceWidget::animationSpriteGrid() const
{
    return VideoFrames::spriteSheetGrid(m_history->model()->animation(m_currentAnimation).frames);
}

void GenerationWorkspaceWidget::saveAnimationSpriteSheet(const QString& path, std::function<void(const QString&)> done)
{
    const auto animation = m_history->model()->animation(m_currentAnimation);
    if (animation.video.isEmpty()) {
        done(tr("No animation selected."));
        return;
    }
    VideoFrames::readAllFrames(animation.video, this, [path, expected = animation.frames, done](const QList<QImage>& frames) {
        if (frames.isEmpty() || frames.size() != expected) {
            done(tr("The animation's frames could not be decoded."));
            return;
        }
        const QImage sheet = VideoFrames::spriteSheet(frames);
        QSaveFile file(path);
        if (sheet.isNull() || !file.open(QIODevice::WriteOnly) || !sheet.save(&file, "PNG") || !file.commit()) {
            done(sheet.isNull() ? tr("The animation's frames could not be decoded.") : file.errorString());
            return;
        }
        done({});
    });
}

void GenerationWorkspaceWidget::setAnimateAvailability(const QString& issue, const QList<AnimateForm::Model>& models,
    bool pingPong)
{
    m_animateIssue = issue;
    m_animateForm->setModels(models);
    m_animateForm->setPingPongAvailable(pingPong);
    updateImageTools();
}

void GenerationWorkspaceWidget::setAnimateSubmitAvailable(bool available, const QString& issue)
{
    m_animateForm->setSubmitAvailable(available, issue);
}

void GenerationWorkspaceWidget::setAnimateOpen(bool open)
{
    m_animateOpen = open && m_animateButton->isEnabled();
    {
        const QSignalBlocker blocker(m_animateButton);
        m_animateButton->setChecked(m_animateOpen);
    }
    // The Animate form, the edit bubble and the New image card never coexist.
    if (m_animateOpen) {
        setGenerationOpen(false);
        m_sparkle->setExpanded(false);
    }
    updateEditBubble();
}

void GenerationWorkspaceWidget::appendImage(const QImage& image, const QString& label, const QString& prompt,
    const QString& negativePrompt)
{
    m_history->appendImage(image, label, prompt, negativePrompt);
}

QString GenerationWorkspaceWidget::currentImagePrompt() const
{
    return m_current.isNull() ? QString()
        : m_history->view()->currentIndex().data(GenerationHistoryModel::PromptRole).toString();
}

void GenerationWorkspaceWidget::addPendingImage(const QUuid& id, const QString& label, const QString& status)
{
    m_history->appendPending(id, label, status);
}

void GenerationWorkspaceWidget::showSparkleResult(const QUuid& id, const QSize& size)
{
    auto* model = m_history->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row);
        if (index.data(GenerationHistoryModel::IdRole).toUuid() != id)
            continue;
        // Start from the tile's current state: a submission may already have reported progress.
        const QString status = index.data(GenerationHistoryModel::StatusRole).toString();
        m_sparkle->addResult(id, size, status);
        m_sparkle->setResultProgress(id, status, index.data(GenerationHistoryModel::ProgressRole).toInt());
        if (model->state(id) == GenerationHistoryModel::State::Failed)
            m_sparkle->failResult(id, status);
        else if (model->state(id) == GenerationHistoryModel::State::Ready)
            m_sparkle->completeResult(id, model->image(id));
        return;
    }
}

// Kira Sparkle mirrors the progress of the images it queued in its conversation.
void GenerationWorkspaceWidget::setPendingProgress(const QUuid& id, const QString& status, int percentage)
{
    m_history->model()->setProgress(id, status, percentage);
    m_sparkle->setResultProgress(id, status, percentage);
}

void GenerationWorkspaceWidget::completePendingImage(const QUuid& id, const QImage& image, const QString& prompt,
    const QString& negativePrompt)
{
    m_history->model()->completePending(id, image, prompt, negativePrompt);
    m_sparkle->completeResult(id, image);
}

void GenerationWorkspaceWidget::failPendingImage(const QUuid& id, const QString& message)
{
    m_history->model()->failPending(id, message);
    m_sparkle->failResult(id, message);
}

void GenerationWorkspaceWidget::removeImage(const QUuid& id)
{
    m_history->removeImage(id);
}

bool GenerationWorkspaceWidget::openImage(const QString& path, QString* error)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto image = reader.read();
    if (image.isNull())
    {
        if (error)
            *error = reader.errorString();
        return false;
    }
    appendImage(image, QFileInfo(path).fileName());
    return true;
}

QString GenerationWorkspaceWidget::imageFileFilter()
{
    QStringList patterns;
    for (const auto& format : QImageReader::supportedImageFormats())
        patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
    return tr("Images (%1)").arg(patterns.join(QLatin1Char(' ')));
}

void GenerationWorkspaceWidget::addReferenceImages(ReferenceImageStrip* strip, bool allOrNothing)
{
    const auto paths = QFileDialog::getOpenFileNames(this, tr("Add Reference Images"), { }, imageFileFilter());
    QStringList failures;
    QList<QImage> images;
    for (const auto& path : paths) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const auto image = reader.read();
        if (image.isNull())
            failures.append(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), reader.errorString()));
        else
            images.append(image);
    }
    addReferences(strip, images, failures, allOrNothing);
}

void GenerationWorkspaceWidget::addReferences(ReferenceImageStrip* strip, const QList<QImage>& images,
    QStringList failures, bool allOrNothing)
{
    if (allOrNothing) {
        const int attached = int(strip->references().size());
        if (!images.isEmpty() && !strip->addReferences(images))
            failures.append(tr("%n image(s) cannot be added: at most %1 images can be attached in total, %2 already are.",
                nullptr, int(images.size())).arg(strip->maximum()).arg(attached));
        if (!failures.isEmpty())
            QMessageBox::warning(this, tr("Could not add reference images"), failures.join(QLatin1Char('\n')));
        return;
    }
    int skipped = 0;
    for (const auto& image : images)
        if (!strip->addReference(image)) ++skipped;
    if (skipped > 0)
        failures.append(tr("Only %n reference image(s) can be attached.", nullptr, strip->maximum()));
    if (!failures.isEmpty())
        QMessageBox::warning(this, tr("Could not add reference images"), failures.join(QLatin1Char('\n')));
}

std::optional<Nukleus::IdentityPackage> GenerationWorkspaceWidget::chooseIdentity(QList<QImage>* images)
{
    const auto path = QFileDialog::getOpenFileName(this, tr("Attach Identity"), {}, tr("Kira identity (*.kiraid)"));
    if (path.isEmpty()) return std::nullopt;
    QFile file(path);
    Nukleus::IdentityPackage identity;
    QString error;
    if (!file.open(QIODevice::ReadOnly)) error = file.errorString();
    else Nukleus::unpackIdentity(file.readAll(), identity, &error);
    images->clear();
    for (const auto& png : std::as_const(identity.references)) {
        const auto image = QImage::fromData(png, "PNG");
        if (image.isNull()) { error = tr("An identity reference image could not be read."); break; }
        images->append(image);
    }
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Could not attach identity"),
            QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error));
        return std::nullopt;
    }
    // Identities saved before their prompts were stored use the current defaults.
    if (identity.generationPrompt.trimmed().isEmpty())
        identity.generationPrompt = QString::fromLatin1(IdentityPrompts::DefaultGenerationPrompt);
    if (identity.editPrompt.trimmed().isEmpty())
        identity.editPrompt = QString::fromLatin1(IdentityPrompts::DefaultEditPrompt);
    return identity;
}

bool GenerationWorkspaceWidget::saveImage(
    const QString& path, const QByteArray& format, QString* error) const
{
    if (m_current.isNull())
    {
        if (error)
            *error = tr("No image selected.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    QImageWriter writer(&file, format);
    if (!writer.write(m_current))
    {
        if (error)
            *error = writer.errorString();
        return false;
    }
    if (!file.commit())
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

void GenerationWorkspaceWidget::clearSelection()
{
    m_history->clearSelection();
    m_current = { };
    m_currentAnimation = { };
    updateEditButton();
    updateEditBubble();
    m_preview->clear();
    updateImageTools();
    emit imageSelectionChanged(false);
}

bool GenerationWorkspaceWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_editPrompt) {
        if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
                && (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
                m_editButton->click();
                return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }
    const bool thumbnail = watched == m_history->view()->viewport();
    if (!thumbnail && m_preview->cropEnabled() && event->type() == QEvent::MouseButtonDblClick)
        return true;
    if ((thumbnail || watched == m_preview->viewport())
        && (event->type() == QEvent::ContextMenu || event->type() == QEvent::MouseButtonDblClick))
    {
        const bool context = event->type() == QEvent::ContextMenu;
        auto* mouse = context ? nullptr : static_cast<QMouseEvent*>(event);
        if (mouse && mouse->button() != Qt::LeftButton)
            return QWidget::eventFilter(watched, event);
        const QPoint point = context ? static_cast<QContextMenuEvent*>(event)->pos() : mouse->position().toPoint();
        const auto index = thumbnail ? m_history->view()->indexAt(point) : m_history->view()->currentIndex();
        const QUuid id = index.data(GenerationHistoryModel::IdRole).toUuid();
        const QImage image = thumbnail ? m_history->model()->image(id) : m_current;
        if (thumbnail && context && m_history->model()->state(id) == GenerationHistoryModel::State::Failed) {
            QMenu menu(this);
            auto* copyError = menu.addAction(tr("Copy error"));
            auto* remove = menu.addAction(tr("Delete"));
            const auto* chosen = menu.exec(static_cast<QContextMenuEvent*>(event)->globalPos());
            if (chosen == copyError)
                QGuiApplication::clipboard()->setText(index.data(GenerationHistoryModel::StatusRole).toString());
            else if (chosen == remove)
                m_history->removeImage(id);
            return true;
        }
        if (m_history->model()->kind(id) == GenerationHistoryModel::Kind::Animation) {
            if (!context)
                openVideoExternally(m_history->model()->animation(id).video);
            else
                showAnimationMenu(id, static_cast<QContextMenuEvent*>(event)->globalPos());
            return true;
        }
        if (image.isNull())
            return QWidget::eventFilter(watched, event);
        if (!context)
            openExternally(image);
        else
            showImageMenu(id, image, static_cast<QContextMenuEvent*>(event)->globalPos());
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void GenerationWorkspaceWidget::showImageMenu(const QUuid& id, const QImage& image, const QPoint& globalPos,
    bool sparkle)
{
    QMenu menu(this);
    auto* open = menu.addAction(tr("Open"));
    auto* save = menu.addAction(tr("Save"));
    QAction* upscale = nullptr;
    if (m_upscaleAvailable) {
        upscale = menu.addAction(tr("Upscale"));
        upscale->setEnabled(m_upscaleEnabled);
    }
    const QString prompt = m_history->model()->prompt(id);
    QAction* copyPrompt = nullptr;
    if (!prompt.isEmpty()) {
        copyPrompt = menu.addAction(tr("Copy prompt"));
        copyPrompt->setObjectName(QStringLiteral("copyPromptAction"));
    }
    // Kira Sparkle's images go back into its conversation, to keep working on them.
    QAction* useAsBase = nullptr;
    QAction* addAsReference = nullptr;
    if (sparkle) {
        menu.addSeparator();
        useAsBase = menu.addAction(Ardoise::IconProvider::icon(Ardoise::Icon::Image), tr("Use as base image"));
        useAsBase->setObjectName(QStringLiteral("sparkleUseAsBaseAction"));
        addAsReference = menu.addAction(Ardoise::IconProvider::icon(Ardoise::Icon::Image), tr("Add as reference"));
        addAsReference->setObjectName(QStringLiteral("sparkleAddAsReferenceAction"));
        addAsReference->setEnabled(!m_sparkle->referenceStrip()->isFull());
    }
    // An image already deleted from the history (shown by Kira Sparkle) has nothing to delete.
    QAction* remove = nullptr;
    if (!m_history->model()->image(id).isNull())
        remove = menu.addAction(tr("Delete"));
    const auto* chosen = menu.exec(globalPos);
    if (copyPrompt && chosen == copyPrompt)
        QGuiApplication::clipboard()->setText(prompt);
    else if (chosen == open)
        openExternally(image);
    else if (chosen == save)
        savePng(image);
    else if (upscale && chosen == upscale)
        emit upscaleRequested(image);
    else if (useAsBase && chosen == useAsBase)
        m_sparkle->setBaseImage(image);
    else if (addAsReference && chosen == addAsReference)
        addReferences(m_sparkle->referenceStrip(), {image}, {}, true);
    else if (remove && chosen == remove)
        m_history->removeImage(id);
}

void GenerationWorkspaceWidget::showAnimationMenu(const QUuid& id, const QPoint& globalPos)
{
    QMenu menu(this);
    auto* open = menu.addAction(tr("Open"));
    auto* save = menu.addAction(tr("Save"));
    const QString prompt = m_history->model()->prompt(id);
    QAction* copyPrompt = nullptr;
    if (!prompt.isEmpty()) {
        copyPrompt = menu.addAction(tr("Copy prompt"));
        copyPrompt->setObjectName(QStringLiteral("copyPromptAction"));
    }
    auto* remove = menu.addAction(tr("Delete"));
    const QByteArray video = m_history->model()->animation(id).video;
    const auto* chosen = menu.exec(globalPos);
    if (chosen == open)
        openVideoExternally(video);
    else if (chosen == save)
        saveVideo(video);
    else if (copyPrompt && chosen == copyPrompt)
        QGuiApplication::clipboard()->setText(prompt);
    else if (chosen == remove)
        m_history->removeImage(id);
}

void GenerationWorkspaceWidget::openVideoExternally(const QByteArray& video)
{
    const QString path = m_externalImages.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".mp4");
    bool written = false;
    if (m_externalImages.isValid()) {
        QFile file(path);
        written = file.open(QIODevice::WriteOnly) && file.write(video) == video.size();
    }
    if (!written || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        QMessageBox::warning(this, tr("Could not open video"), tr("Could not open the video in the default video player."));
}

void GenerationWorkspaceWidget::saveVideo(const QByteArray& video)
{
    QFileDialog dialog(this, tr("Save Animation"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDirectory(OutputSettingsWidget::preferredDirectory());
    dialog.setNameFilter(tr("MP4 video (*.mp4)"));
    dialog.setDefaultSuffix("mp4");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    QSaveFile file(dialog.selectedFiles().first());
    if (!file.open(QIODevice::WriteOnly) || file.write(video) != video.size() || !file.commit())
        QMessageBox::warning(this, tr("Could not save animation"), file.errorString());
}

void GenerationWorkspaceWidget::openExternally(const QImage& image)
{
    const QString path = m_externalImages.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png");
    if (!m_externalImages.isValid() || !image.save(path, "PNG")
        || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        QMessageBox::warning(this, tr("Could not open image"), tr("Could not open the image in the default image application."));
}

void GenerationWorkspaceWidget::savePng(const QImage& image)
{
    QFileDialog dialog(this, tr("Save Image"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDirectory(OutputSettingsWidget::preferredDirectory());
    dialog.setNameFilter(tr("PNG image (*.png)"));
    dialog.setDefaultSuffix("png");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    QSaveFile file(dialog.selectedFiles().first());
    QImageWriter writer(&file, "PNG");
    if (!file.open(QIODevice::WriteOnly) || !writer.write(image) || !file.commit())
        QMessageBox::warning(this, tr("Could not save image"),
            file.error() == QFileDevice::NoError ? writer.errorString() : file.errorString());
}

void GenerationWorkspaceWidget::setDefaultModels(const QList<Nukleus::ModelInfo>& models,
    const QString& edition, const QString& upscaler)
{
    m_editAvailable = false;
    m_upscaleAvailable = false;
    for (const auto& model : models) {
        if (model.id.isEmpty())
            continue;
        if (model.category == Nukleus::ModelCategory::ImageGeneration && model.id == edition
            && model.recommendedOptions.value("canEdit").toBool(true))
            m_editAvailable = true;
        if (model.category == Nukleus::ModelCategory::Upscaler && model.id == upscaler)
            m_upscaleAvailable = true;
    }
    if (!m_routedEditing) m_editOffered = m_editAvailable;
    updateImageTools();
    updateEditButton();
    updateEditBubble();
}

void GenerationWorkspaceWidget::setEditRouting(const std::optional<Nukleus::ModelRouting> &routing,
    const QList<Nukleus::ModelInfo> &models)
{
    m_routedEditing = true;
    const auto previous = editQualityId().isEmpty() ? m_selectedEditQuality : editQualityId();
    const QSignalBlocker blocker(m_editQuality);
    QStringList labels;
    m_editQualityIds.clear();
    m_editQualityIssues.clear();
    m_defaultEditQuality.clear();
    QList<bool> available;
    if (routing) for (const auto &quality : routing->editingQualities) {
        if (quality.isDefault) m_defaultEditQuality = quality.id;
        const auto &id = quality.model.modelId;
        QString issue = id.isEmpty() ? tr("Unassigned — configure this quality in Service Manager")
                                     : tr("Assigned model is unavailable");
        for (const auto &model : models)
            if (!id.isEmpty() && model.id == id && model.category == Nukleus::ModelCategory::ImageGeneration
                && model.recommendedOptions.value("canEdit").toBool(true)) {
                issue.clear(); break;
            }
        m_editQualityIds.append(quality.id);
        m_editQualityIssues.append(issue);
        labels.append(quality.displayLabel());
        available.append(issue.isEmpty());
    }
    // The tooltip names the assigned model when the quality can edit, or why it cannot.
    QStringList toolTips = m_editQualityIssues;
    if (routing) for (int i = 0; i < toolTips.size(); ++i)
        if (toolTips[i].isEmpty())
            for (const auto &model : models)
                if (model.id == routing->editingQualities[i].model.modelId) toolTips[i] = model.displayLabel();
    m_editQuality->setSteps(labels, available, toolTips);
    // Keep the user's quality across refreshes and New Image; start from the service default.
    int index = int(m_editQualityIds.indexOf(previous));
    if (index < 0) index = int(m_editQualityIds.indexOf(m_defaultEditQuality));
    m_editQuality->setValue(qMax(0, index));
    if (!editQualityId().isEmpty()) m_selectedEditQuality = editQualityId();
    m_editQuality->setEnabled(!m_routingLocked && !m_editQualityIds.isEmpty());
    // An unassigned or unavailable quality keeps the bubble, explaining why Edit is disabled.
    m_editOffered = routing.has_value();
    m_editQualityLabels = labels;
    updateEditButton();
    updateEditBubble();
    updateSparkleQualities();
}

void GenerationWorkspaceWidget::updateSparkleQualities()
{
    QList<KiraSparkle::Choice> choices;
    for (int i = 0; i < m_editQualityIds.size(); ++i)
        choices.append({m_editQualityIds.at(i), m_editQualityLabels.value(i), m_editQualityIssues.value(i)});
    m_sparkle->setQualities(choices, editQualityId());
}

void GenerationWorkspaceWidget::updateSparklePresets()
{
    QList<KiraSparkle::Choice> choices;
    for (const auto& preset : m_parameters->presets())
        choices.append({preset.id, preset.displayLabel(), m_parameters->presetIssue(preset.id),
            m_parameters->presetUsesReferences(preset.id)});
    const auto* selected = m_parameters->selectedPreset();
    m_sparkle->setPresets(choices, selected ? selected->id : QString());
}

void GenerationWorkspaceWidget::updateEditBubble()
{
    // The open New image card offers creation instead of editing, over any image.
    // The identity panel covers the preview, so both step aside while it is open.
    const bool identityOpen = m_identityPanel && m_identityPanel->isOpen();
    const bool generationOpen = m_generationCard && m_generationOpen;
    // The Animate form takes the edit bubble's place while it is open.
    const bool animateOpen = m_animateOpen && !m_current.isNull();
    m_editBubble->setActive(m_editOffered && !m_current.isNull() && !identityOpen && !generationOpen && !animateOpen);
    m_animateForm->setActive(animateOpen && !identityOpen && !generationOpen);
    m_sparkle->setActive(!identityOpen);
    if (m_generationCard)
        m_generationCard->setActive(generationOpen && !identityOpen);
}

void GenerationWorkspaceWidget::setGenerationOpen(bool open)
{
    m_generationOpen = open;
    {
        const QSignalBlocker blocker(m_newImageButton);
        m_newImageButton->setChecked(open);
    }
    // Opening the card puts the identity panel away so the card is not hidden behind it.
    if (open && m_identityPanel)
        m_identityPanel->setOpen(false);
    if (open && m_animateOpen) {
        m_animateOpen = false;
        const QSignalBlocker blocker(m_animateButton);
        m_animateButton->setChecked(false);
    }
    updateEditBubble();
}

void GenerationWorkspaceWidget::showGenerationCard()
{
    setGenerationOpen(true);
}

void GenerationWorkspaceWidget::setIdentityPanel(IdentityPanel* panel)
{
    m_identityPanel = panel;
    panel->setParent(this);
    panel->setAnchors(m_toolbar, m_content);
    connect(m_identityButton, &QToolButton::toggled, panel, &IdentityPanel::setOpen);
    connect(panel, &IdentityPanel::openChanged, this, [this](bool open) {
        const QSignalBlocker blocker(m_identityButton);
        m_identityButton->setChecked(open);
        updateEditBubble();
    });
}

void GenerationWorkspaceWidget::showIdentityPanel()
{
    if (m_identityPanel)
        m_identityPanel->setOpen(true);
}

void GenerationWorkspaceWidget::setEditEnabled(bool enabled)
{
    m_editEnabled = enabled;
    updateEditButton();
}

QString GenerationWorkspaceWidget::editQualityId() const { return m_editQualityIds.value(m_editQuality->value()); }

void GenerationWorkspaceWidget::setRoutingLocked(bool locked)
{
    m_routingLocked = locked;
    m_editQuality->setEnabled(!locked && !m_editQualityIds.isEmpty());
    m_sparkle->setEngineLocked(locked);
}

void GenerationWorkspaceWidget::updateEditButton()
{
    // Routed editing follows the selected quality; identity edits follow the default one.
    const auto issue = [this](const QString &id) {
        const int index = int(m_editQualityIds.indexOf(id));
        return index < 0 ? tr("No editing quality is available") : m_editQualityIssues.value(index);
    };
    const QString editIssue = m_routedEditing ? issue(editQualityId()) : QString();
    const QString identityIssue = m_routedEditing ? issue(m_defaultEditQuality) : QString();
    const bool editAvailable = m_routedEditing ? editIssue.isEmpty() : m_editAvailable;
    const bool identityAvailable = m_routedEditing ? identityIssue.isEmpty() : m_editAvailable;
    m_editButton->setEnabled(editAvailable && m_editEnabled && !m_current.isNull()
        && !m_editPrompt->toPlainText().trimmed().isEmpty());
    m_editButton->setToolTip(editIssue);
    m_identityEditButton->setEnabled(identityAvailable && m_editEnabled && !m_current.isNull());
    m_identityEditButton->setToolTip(identityIssue.isEmpty() || m_current.isNull() ? tr("Edit with an identity…")
        : tr("Edit with an identity: %1").arg(identityIssue));
    auto* improve = m_editBubble->improveButton();
    const bool written = !m_editPrompt->toPlainText().trimmed().isEmpty();
    improve->setEnabled(m_editImproveIssue.isEmpty() && written);
    improve->setToolTip(!m_editImproveIssue.isEmpty() ? m_editImproveIssue
        : !written ? tr("Write an instruction to improve it.")
        : tr("Improve the instruction: more detailed and structured, in the same language, without inventing anything"));
}

void GenerationWorkspaceWidget::setEditImproveIssue(const QString& issue)
{
    m_editImproveIssue = issue;
    updateEditButton();
}

void GenerationWorkspaceWidget::setEditInstruction(const QString& text)
{
    m_editPrompt->setPlainText(text);
    m_editPrompt->moveCursor(QTextCursor::End);
}

void GenerationWorkspaceWidget::setGenerationPanel(GenerationInspectorWidget* panel)
{
    m_generationCard = new GenerationCard(panel, m_preview);
    m_parameters = panel->parameters();
    connect(m_parameters, &GenerationParametersWidget::parametersChanged, this,
        &GenerationWorkspaceWidget::updateSparklePresets);
    connect(m_sparkle, &KiraSparkle::presetSelected, m_parameters, &GenerationParametersWidget::selectPreset);
    updateSparklePresets();
    connect(m_generationCard, &GenerationCard::closeRequested, this, [this] { setGenerationOpen(false); });
    connect(panel->attachButton(), &QToolButton::clicked, this, [this, panel] { addReferenceImages(panel->references()); });
    connect(panel->attachIdentityButton(), &QToolButton::clicked, this, [this, panel] {
        QList<QImage> references;
        const auto identity = chooseIdentity(&references);
        if (!identity) return;
        addReferences(panel->references(), references);
        // Appended at submission, after the preset's prefix, without touching the editors.
        if (!panel->references()->references().isEmpty())
            panel->setIdentityPrompt(identity->generationPrompt);
    });
    connect(m_activity, &JobActivityIndicator::coveredHeightChanged, m_generationCard, &GenerationCard::setTopInset);
    m_activity->raise();
    updateEditBubble();
}

void GenerationWorkspaceWidget::setUpscaleEnabled(bool enabled)
{
    m_upscaleEnabled = enabled;
    updateImageTools();
}

void GenerationWorkspaceWidget::setSharpenEnabled(bool enabled)
{
    m_sharpenEnabled = enabled;
    updateImageTools();
}

void GenerationWorkspaceWidget::updateImageTools()
{
    // An animation can be viewed, zoomed and saved; the tools that need a still image stay disabled.
    const bool still = !m_current.isNull();
    for (auto* button : m_imageTools) button->setEnabled(still || !m_currentAnimation.isNull());
    m_cropButton->setEnabled(still);
    const bool animate = still && m_animateIssue.isEmpty();
    m_animateButton->setEnabled(animate);
    m_animateButton->setToolTip(animate ? tr("Animate")
        : still ? m_animateIssue : tr("Select a still image to animate it."));
    if (!animate && m_animateOpen) setAnimateOpen(false);
    m_sparkle->setContextAvailable(!m_current.isNull());
    m_upscaleButton->setEnabled(!m_current.isNull() && m_upscaleAvailable && m_upscaleEnabled);
    m_downscaleButton->setEnabled(m_current.width() >= 2 && m_current.height() >= 2);
    m_sharpenButton->setEnabled(!m_current.isNull() && m_sharpenEnabled);
    m_sharpenButton->setToolTip(m_sharpenEnabled || m_current.isNull() ? tr("Improve sharpness")
        : tr("Improve sharpness: requires the default editing quality's model and no running job"));
    updateEditButton();
}
