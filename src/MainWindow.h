#pragma once

#include "ImageAsset.h"

#include <QMainWindow>
#include <deque>

class ImageLibraryWidget;
class GeneratePanel;
class QCloseEvent;
class QAction;
class QProgressBar;
class QSplitter;
class ServiceClient;
class QTabWidget;
class ImageViewer;
struct ImageGenerationRequest;
struct GenerationProgress;
struct SdCliGenerationResult;
struct SdCliUpscaleResult;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setCurrentImage(const ImageAsset *asset, bool showStatus);
    void clearCurrentImage();
    void openImagePreview(const ImageAsset *asset);
    const ImageAsset *importImage(const QString &filePath);
    void restoreLibrary();
    void restoreManagedOutputImages();
    void saveLibrary() const;
    void moveAssetToTrash(const QString &filePath);
    void removeAssetFromLibrary(const QString &filePath);
    void openImages();
    void closePreview(int index);
    void closeCurrentPreview();
    void startGeneration(const ImageGenerationRequest &request);
    void updateGenerationProgress(const GenerationProgress &progress);
    void hideGenerationProgress();
    void handleGenerationFinished(const SdCliGenerationResult &result);
    const ImageAsset *activePreviewAsset() const;
    void updateUpscaleAction();
    void showUpscaleDialog();
    void startUpscale(const ImageAsset &source, int scale);
    void showServicePreferences();
    void updateServiceCapabilities(bool generationAvailable, bool upscaleAvailable);
    void handleUpscaleFinished(const SdCliUpscaleResult &result);
    void applyDefaultLayout();
    void restoreWindowState();
    void saveWindowState();

    std::deque<ImageAsset> m_assets;
    QTabWidget *m_previewTabs = nullptr;
    QAction *m_importAction = nullptr;
    QAction *m_closePreviewAction = nullptr;
    QAction *m_upscaleAction = nullptr;
    GeneratePanel *m_generatePanel = nullptr;
    ImageLibraryWidget *m_library = nullptr;
    QProgressBar *m_generationProgressBar = nullptr;
    ServiceClient *m_serviceClient = nullptr;
    QSplitter *m_workspaceSplitter = nullptr;
    bool m_generationRunning = false;
    bool m_upscaleRunning = false;
    bool m_generationAvailable = false;
    bool m_upscaleAvailable = false;
};
