#pragma once
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QTemporaryDir>
#include <QWidget>
#include <nukleus/ImageTypes.h>
#include <nukleus/Identity.h>

class CanvasServiceController;
class GenerationParametersWidget;
class JobActivityIndicator;
class QGraphicsOpacityEffect;
class QHBoxLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QVariantAnimation;
class PortraitSlot;

// Floating identity workshop that slides out beside the editing toolbar. A guided
// path picks the style and views, describes the face and the hair, then generates
// the selected portrait views sequentially, translating the description first with
// the default prompt translator when there is one.
class IdentityPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit IdentityPanel(CanvasServiceController* service, QWidget* parent = nullptr);
    // Opens beside toolbar, matching its height, over area.
    void setAnchors(QWidget* toolbar, QWidget* area);
    void setOpen(bool open);
    bool isOpen() const { return m_open; }
    // Replaces both editors (with improved descriptions).
    void setDescriptions(const QString& face, const QString& hair);
    bool awaitingTranslation() const { return m_awaitingTranslation; }
    void setAwaitingTranslation(bool pending);
    // Continues with the English description, or restores availability after a failure.
    void receiveTranslation(bool succeeded, const QString& description);
    bool awaitingPortrait() const { return m_awaitingPortrait; }
    void setAwaitingPortrait(bool pending);
    void setPortrait(const QImage& image);
    // Returns whether another view of the set was requested.
    bool receivePortrait(const QImage& image);
    QImage portrait(int index) const;
    Nukleus::IdentityPackage portraitIdentity() const;
    JobActivityIndicator* activity() const { return m_activity; }
signals:
    void openChanged(bool open);
    // The description needs an LLM translation into English before the portraits.
    void translationRequested(const QString& description);
    // Sparkles asks to improve the face and hair descriptions.
    void improveRequested(const QString& face, const QString& hair);
    void portraitRequested(const Nukleus::ImageGenerationRequest& request, int view, int views);
    void identityRequested(const Nukleus::IdentityPackage& identity);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    // Sizes the panel so the portrait grid exactly spans the steps' height, then lays out the slots.
    void relayout(bool animateSlots = false);
    int displayedViewCount() const;
    // Width / height of the portrait slots.
    qreal portraitAspect() const;
    void animateReveal();
    void generate();
    void requestPortraits(const QString& description);
    void updateAvailability();
    void updatePortraitGeometry(bool animated);
    int selectedViewCount() const;
    void openExternally(const QImage& image);
    CanvasServiceController* m_service;
    QPointer<QWidget> m_toolbar;
    QPointer<QWidget> m_area;
    QWidget* m_panel;
    QHBoxLayout* m_header;
    QHBoxLayout* m_footer;
    QWidget* m_controls;
    // Columns × rows of the portrait grid.
    QSize m_grid;
    bool m_inRelayout = false;
    QVariantAnimation* m_reveal;
    QGraphicsOpacityEffect* m_opacity;
    qreal m_revealValue = 0;
    bool m_open = false;
    GenerationParametersWidget* m_parameters;
    QToolButton* m_improve;
    QPlainTextEdit* m_prompt;
    QPlainTextEdit* m_hairPrompt;
    QPushButton* m_generate;
    QPushButton* m_save;
    QWidget* m_portraitArea;
    QList<PortraitSlot*> m_slots;
    QVariantAnimation* m_slotMotion;
    QList<QRect> m_slotStart;
    QList<QRect> m_slotTarget;
    JobActivityIndicator* m_activity;
    QTemporaryDir m_externalImages;
    QList<Nukleus::ImageGenerationRequest> m_portraitRequests;
    Nukleus::ImageGenerationRequest m_pendingRequest;
    int m_portraitIndex = 0;
    int m_portraitViewCount = 0;
    QString m_portraitIdentityPrompt;
    QString m_portraitHairPrompt;
    QString m_pendingIdentityPrompt;
    QString m_pendingHairPrompt;
    int m_portraitSeed = -1;
    bool m_awaitingTranslation = false;
    bool m_awaitingPortrait = false;
};
