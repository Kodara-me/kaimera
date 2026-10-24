#pragma once
#include <QWidget>
#include <QImage>
#include <functional>
#include <QTemporaryDir>
#include <QUuid>
#include <nukleus/Identity.h>
#include <nukleus/ImageTypes.h>
#include <nukleus/ModelRouting.h>
#include "AnimateForm.h"
class EditBubble;
class GenerationCard;
class GenerationInspectorWidget;
class GenerationParametersWidget;
class IdentityPanel;
class JobActivityIndicator;
class KiraSparkle;
class QualityStepSlider;
class ReferenceImageStrip;
class QLabel;
class QToolButton;
class QPushButton;
namespace Ardoise { class PlainTextEdit; }
class GenerationPreviewWidget;
class GenerationHistoryWidget;
class GenerationWorkspaceWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit GenerationWorkspaceWidget(QWidget* parent = nullptr);
    GenerationPreviewWidget* preview() const { return m_preview; }
    JobActivityIndicator* activity() const { return m_activity; }
    QImage currentImage() const { return m_current; }
    // The final prompt the selected image was generated from, or empty.
    QString currentImagePrompt() const;
    KiraSparkle* sparkle() const { return m_sparkle; }
    // Generated images keep the service's final prompts for their tooltip and Copy prompt.
    void appendImage(const QImage& image, const QString& label, const QString& prompt = {},
        const QString& negativePrompt = {});
    // Queued generations hold a progress tile in the history that their image replaces,
    // without selecting it; failures stay as removable tiles with their message.
    void addPendingImage(const QUuid& id, const QString& label, const QString& status);
    void setPendingProgress(const QUuid& id, const QString& status, int percentage);
    void completePendingImage(const QUuid& id, const QImage& image, const QString& prompt = {},
        const QString& negativePrompt = {});
    void failPendingImage(const QUuid& id, const QString& message);
    // Turns a queued animation's entry into an animation once its first frame is decoded, or into
    // a failed entry when the job failed or the video cannot be read.
    void completePendingAnimation(const QUuid& id, const Nukleus::AnimationResult& result, const QString& prompt);
    // The selected animation entry, or null when none (a still image or nothing is selected).
    QUuid currentAnimation() const { return m_currentAnimation; }
    // Writes the selected animation's video bytes unchanged.
    bool saveAnimation(const QString& path, QString* error = nullptr) const;
    // The grid, columns by rows, of the selected animation's sprite sheet; empty without one.
    QSize animationSpriteGrid() const;
    // Writes the selected animation as a PNG sprite sheet once its frames are decoded; done runs once
    // with an error message, empty on success.
    void saveAnimationSpriteSheet(const QString& path, std::function<void(const QString&)> done);
    AnimateForm* animateForm() const { return m_animateForm; }
    // Why the service cannot animate (empty when it can), the usable animation models and whether
    // it makes ping-pong loops; Animate needs a selected still image besides.
    void setAnimateAvailability(const QString& issue, const QList<AnimateForm::Model>& models, bool pingPong);
    // Whether the Animate form can submit now, apart from its prompt (a single job blocks it).
    void setAnimateSubmitAvailable(bool available, const QString& issue = {});
    // Shows a queued history image in Kira Sparkle's conversation, in the size's ratio; the
    // conversation then follows its progress.
    void showSparkleResult(const QUuid& id, const QSize& size);
    void removeImage(const QUuid& id);
    bool openImage(const QString& path, QString* error = nullptr);
    bool saveImage(const QString& path, const QByteArray& format, QString* error = nullptr) const;
    void clearSelection();
    void setDefaultModels(const QList<Nukleus::ModelInfo>& models, const QString& edition,
        const QString& upscaler);
    void setEditEnabled(bool enabled);
    // Why the edit instruction cannot be improved now, or empty when it can.
    void setEditImproveIssue(const QString& issue);
    // Replaces the edit instruction (with an improved one).
    void setEditInstruction(const QString& text);
    void setEditRouting(const std::optional<Nukleus::ModelRouting> &routing,
        const QList<Nukleus::ModelInfo> &models);
    // The editing quality selected in the edit bubble.
    QString editQualityId() const;
    // Locks the edit quality selection while a job runs.
    void setRoutingLocked(bool locked);
    void setUpscaleEnabled(bool enabled);
    void setSharpenEnabled(bool enabled);
    // Hosts the generation controls in a card centred over the preview; the New Image
    // toolbar button toggles it over any image, and it starts closed.
    void setGenerationPanel(GenerationInspectorWidget* panel);
    void showGenerationCard();
    // Slides the identity panel out beside the toolbar; its toolbar button toggles it.
    void setIdentityPanel(IdentityPanel* panel);
    void showIdentityPanel();
signals:
    // translatePrompt is false for identity instructions, which are already English.
    void editRequested(const QImage& image, const QString& prompt, const QList<QImage>& references,
        const QString& qualityId, bool translatePrompt);
    void upscaleRequested(const QImage& image);
    void sharpenRequested(const QImage& image);
    // The Animate form's submission: the selected image, the end image (null without one) and,
    // when the service makes ping-pong loops (sendPingPong), the Loop switch's state, then the
    // chosen length in frames and the frame rate it plays at.
    void animateRequested(const QImage& start, const QImage& end, const QString& prompt, const QString& modelId,
        bool sendPingPong, bool pingPong, int frames, int fps);
    // Sparkles in the edit bubble asks to improve the current instruction.
    void editImproveRequested(const QString& instruction);
    // Kira Sparkle sends a request with its base image (null without one) and references.
    void sparkleRequested(const QString& request, const QImage& base, const QList<QImage>& attachments);
    void imageSelectionChanged(bool hasImage);
    void saveAsRequested();
    void openRequested();
    void servicePreferencesRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void openExternally(const QImage& image);
    // Offers the card's presets and the edit bubble's qualities in Kira Sparkle's engine button.
    void updateSparklePresets();
    void updateSparkleQualities();
    // Open, Save, Upscale, Copy prompt and Delete for a history image (or one shown by Kira Sparkle).
    // Kira Sparkle's images also offer to become its base image or a reference.
    void showImageMenu(const QUuid& id, const QImage& image, const QPoint& globalPos, bool sparkle = false);
    // The Qt file dialog filter for the readable image formats.
    static QString imageFileFilter();
    void addReferenceImages(ReferenceImageStrip* strip, bool allOrNothing = false);
    // Attaches images to strip, warning about failures and images beyond its capacity; with
    // allOrNothing, images that do not all fit are all rejected.
    void addReferences(ReferenceImageStrip* strip, const QList<QImage>& images, QStringList failures = {},
        bool allOrNothing = false);
    // Loads a user-selected .kiraid with its decoded references; nothing when cancelled or unreadable.
    std::optional<Nukleus::IdentityPackage> chooseIdentity(QList<QImage>* images);
    void savePng(const QImage& image);
    // Open, Save, Copy prompt and Delete for an animation entry.
    void showAnimationMenu(const QUuid& id, const QPoint& globalPos);
    void openVideoExternally(const QByteArray& video);
    void saveVideo(const QByteArray& video);
    void selectAnimation(const QUuid& id);
    void updateImageTools();
    // The Animate toolbar button toggles the form over the selected still image.
    void setAnimateOpen(bool open);
    QToolButton* m_animateButton;
    AnimateForm* m_animateForm;
    bool m_animateOpen = false;
    QString m_animateIssue;
    QUuid m_currentAnimation;
    QList<QToolButton*> m_imageTools;
    QToolButton* m_fitButton;
    QToolButton* m_cropButton;
    QToolButton* m_upscaleButton;
    QToolButton* m_downscaleButton;
    QToolButton* m_sharpenButton;
    QToolButton* m_identityEditButton;
    QTemporaryDir m_externalImages;
    GenerationPreviewWidget* m_preview;
    GenerationHistoryWidget* m_history;
    EditBubble* m_editBubble;
    KiraSparkle* m_sparkle;
    JobActivityIndicator* m_activity;
    GenerationCard* m_generationCard = nullptr;
    IdentityPanel* m_identityPanel = nullptr;
    QWidget* m_toolbar;
    QWidget* m_content;
    QToolButton* m_identityButton;
    QToolButton* m_newImageButton;
    bool m_generationOpen = false;
    void setGenerationOpen(bool open);
    bool m_editOffered = false;
    void updateEditBubble();
    bool m_routedEditing = false;
    bool m_routingLocked = false;
    QualityStepSlider* m_editQuality;
    QStringList m_editQualityIds;
    QStringList m_editQualityLabels;
    GenerationParametersWidget* m_parameters = nullptr;
    // Why each editing quality cannot edit, shown on the Edit button; empty when it can.
    QStringList m_editQualityIssues;
    QString m_selectedEditQuality;
    QString m_defaultEditQuality;
    Ardoise::PlainTextEdit* m_editPrompt;
    void updateEditButton();
    bool m_editEnabled = false;
    QString m_editImproveIssue = tr("The default prompt enhancer is unavailable.");
    bool m_editAvailable = false;
    bool m_upscaleAvailable = false;
    bool m_upscaleEnabled = true;
    bool m_sharpenEnabled = false;
    QPushButton* m_editButton;
    QImage m_current;
};
