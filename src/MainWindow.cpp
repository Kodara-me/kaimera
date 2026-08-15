#include "MainWindow.h"

#include "AppPaths.h"
#include "GeneratePanel.h"
#include "ImageLibraryWidget.h"
#include "ImageViewer.h"
#if defined(Q_OS_MACOS)
#include "MacPanelSurface.h"
#include "MacWorkspaceSplitter.h"
#endif
#include "ServiceClient.h"
#include "ServicePreferencesDialog.h"
#include "SystemIcon.h"
#include "UpscaleDialog.h"

#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStackedLayout>
#include <QStyle>
#include <QTabWidget>
#include <QTabBar>
#include <QToolBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <limits>

namespace
{
const QString LibraryAssetPathsKey = QStringLiteral("Library/AssetPaths");

QString imageFileDialogFilter()
{
    QStringList patterns;
    for (const QByteArray &format : QImageReader::supportedImageFormats()) {
        patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
    }
    return QObject::tr("Images (%1)").arg(patterns.join(QLatin1Char(' ')));
}

bool isPathInDirectory(const QString &filePath, const QString &directoryPath)
{
    if (directoryPath.isEmpty()) {
        return false;
    }

    const QString relativePath = QDir(directoryPath).relativeFilePath(filePath);
    return relativePath != QStringLiteral("..") && !relativePath.startsWith(QStringLiteral("../"))
           && !QDir::isAbsolutePath(relativePath);
}

ImageAssetCategory categoryForPath(const QString &filePath)
{
    if (isPathInDirectory(filePath, AppPaths::generatedPath())) {
        return ImageAssetCategory::Generated;
    }
    if (isPathInDirectory(filePath, AppPaths::upscaledPath())) {
        return ImageAssetCategory::Upscaled;
    }
    return ImageAssetCategory::Imported;
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Kira Canvas"));
    resize(1600, 1000);

#if defined(Q_OS_MACOS)
    setUnifiedTitleAndToolBarOnMac(true);
#endif

#if !defined(Q_OS_MACOS)
    auto *toolBar = addToolBar(tr("Main"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
#endif
    m_importAction = new QAction(systemIcon(QStringLiteral("document-open"), QStyle::SP_DialogOpenButton, this),
                                 tr("Import Photo"),
                                 this);
    m_importAction->setShortcut(QKeySequence::Open);
    m_closePreviewAction = new QAction(tr("Close Preview"), this);
    m_closePreviewAction->setShortcut(QKeySequence::Close);
    m_closePreviewAction->setEnabled(false);
    m_upscaleAction = new QAction(systemIcon(QStringLiteral("zoom-in"), QStyle::SP_ArrowUp, this), tr("Upscale"), this);
    m_upscaleAction->setObjectName(QStringLiteral("upscaleAction"));
    m_upscaleAction->setEnabled(false);
    m_importAction->setToolTip(tr("Import Photo"));
    m_upscaleAction->setToolTip(tr("Upscale the active preview"));
#if !defined(Q_OS_MACOS)
    toolBar->addAction(m_importAction);
    toolBar->addAction(m_upscaleAction);
#endif

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_importAction);
    fileMenu->addAction(m_closePreviewAction);
    QMenu *imageMenu = menuBar()->addMenu(tr("&Image"));
    imageMenu->addAction(m_upscaleAction);
    QMenu *settingsMenu = menuBar()->addMenu(tr("&Settings"));
    auto *servicePreferencesAction = settingsMenu->addAction(tr("Service Preferences…"));
#if defined(Q_OS_MACOS)
    servicePreferencesAction->setMenuRole(QAction::PreferencesRole);
#endif

    m_generationProgressBar = new QProgressBar(statusBar());
    m_generationProgressBar->setRange(0, 100);
    m_generationProgressBar->setTextVisible(true);
    m_generationProgressBar->setFormat(QStringLiteral("%p%"));
    m_generationProgressBar->setFixedWidth(120);
    m_generationProgressBar->hide();
    statusBar()->addPermanentWidget(m_generationProgressBar);
#if defined(Q_OS_MACOS)
    statusBar()->setSizeGripEnabled(false);
#endif

    m_library = new ImageLibraryWidget(m_assets, this);
#if defined(Q_OS_MACOS)
    m_library->setMinimumWidth(0);
#else
    m_library->setMinimumWidth(250);
#endif

    auto *workspace = new QWidget(this);
    auto *workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
#if defined(Q_OS_MACOS)
    m_previewWorkspace = workspace;
    m_previewWorkspace->installEventFilter(this);
    m_transientStatusMessage = new QLabel(m_previewWorkspace);
    m_transientStatusMessage->setObjectName(QStringLiteral("transientStatusMessage"));
    m_transientStatusMessage->setAlignment(Qt::AlignCenter);
    m_transientStatusMessage->setForegroundRole(QPalette::ToolTipText);
    m_transientStatusMessage->setBackgroundRole(QPalette::ToolTipBase);
    m_transientStatusMessage->setAutoFillBackground(true);
    m_transientStatusMessage->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_transientStatusMessage->hide();
    m_transientStatusTimer = new QTimer(this);
    m_transientStatusTimer->setSingleShot(true);
    connect(m_transientStatusTimer, &QTimer::timeout, m_transientStatusMessage, &QWidget::hide);
    connect(statusBar(), &QStatusBar::messageChanged, this, &MainWindow::showTransientStatusMessage);
#endif
    m_previewTabs = new QTabWidget(workspace);
    m_previewTabs->setTabsClosable(true);
#if defined(Q_OS_MACOS)
    m_previewTabs->setDocumentMode(true);
    m_workspacePages = new QStackedLayout;
    m_workspacePages->setContentsMargins(0, 0, 0, 0);
    auto *emptyWorkspace = new QLabel(tr("Open an image to begin"), workspace);
    emptyWorkspace->setAlignment(Qt::AlignCenter);
    m_workspacePages->addWidget(emptyWorkspace);
    m_workspacePages->addWidget(m_previewTabs);
    workspaceLayout->addLayout(m_workspacePages, 1);
#else
    workspaceLayout->addWidget(m_previewTabs, 1);
#endif

    m_generatePanel = new GeneratePanel(statusBar(), this);
#if defined(Q_OS_MACOS)
    m_generatePanel->setMinimumWidth(0);
#else
    m_generatePanel->setMinimumWidth(280);
#endif

#if defined(Q_OS_MACOS)
    auto *workspaceContainer = new QWidget(this);
    auto *workspaceContainerLayout = new QVBoxLayout(workspaceContainer);
    workspaceContainerLayout->setContentsMargins(12, 12, 12, 8);

    auto *libraryPanel = new MacPanelSurface(workspaceContainer);
    auto *libraryPanelLayout = new QVBoxLayout(libraryPanel);
    libraryPanelLayout->setContentsMargins(12, 12, 12, 12);
    libraryPanelLayout->addWidget(m_library);
    libraryPanel->setMinimumWidth(260);

    auto *previewPanel = new MacPanelSurface(workspaceContainer);
    auto *previewPanelLayout = new QVBoxLayout(previewPanel);
    previewPanelLayout->setContentsMargins(12, 12, 12, 12);
    previewPanelLayout->addWidget(workspace);

    auto *generatePanel = new MacPanelSurface(workspaceContainer);
    auto *generatePanelLayout = new QVBoxLayout(generatePanel);
    generatePanelLayout->setContentsMargins(12, 12, 12, 12);
    generatePanelLayout->addWidget(m_generatePanel);
    generatePanel->setMinimumWidth(320);

    m_workspaceSplitter = new MacWorkspaceSplitter(workspaceContainer);
    m_workspaceSplitter->addWidget(libraryPanel);
    m_workspaceSplitter->addWidget(previewPanel);
    m_workspaceSplitter->addWidget(generatePanel);
    workspaceContainerLayout->addWidget(m_workspaceSplitter);
    setCentralWidget(workspaceContainer);
#else
    m_workspaceSplitter = new QSplitter(Qt::Horizontal, this);
    m_workspaceSplitter->addWidget(m_library);
    m_workspaceSplitter->addWidget(workspace);
    m_workspaceSplitter->addWidget(m_generatePanel);
    setCentralWidget(m_workspaceSplitter);
#endif
    m_workspaceSplitter->setStretchFactor(0, 0);
    m_workspaceSplitter->setStretchFactor(1, 1);
    m_workspaceSplitter->setStretchFactor(2, 0);

    connect(m_importAction, &QAction::triggered, this, &MainWindow::openImages);
    connect(m_closePreviewAction, &QAction::triggered, this, &MainWindow::closeCurrentPreview);
    connect(m_upscaleAction, &QAction::triggered, this, &MainWindow::showUpscaleDialog);
    connect(servicePreferencesAction, &QAction::triggered, this, &MainWindow::showServicePreferences);
    connect(m_generatePanel, &GeneratePanel::generationRequested, this, &MainWindow::startGeneration);
    connect(m_library, &ImageLibraryWidget::imageSelected, this, [this](const ImageAsset *asset) {
        setCurrentImage(asset, true);
    });
    connect(m_library, &ImageLibraryWidget::imagePreviewRequested, this, &MainWindow::openImagePreview);
    connect(m_library, &ImageLibraryWidget::moveToTrashRequested, this, &MainWindow::moveAssetToTrash);
    connect(m_previewTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closePreview);
    connect(m_previewTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index < 0) {
            updateUpscaleAction();
            updateWorkspacePage();
            return;
        }
        const QString assetPath = m_previewTabs->tabBar()->tabData(index).toString();
        const auto asset = std::find_if(m_assets.cbegin(), m_assets.cend(), [&assetPath](const ImageAsset &candidate) {
            return candidate.filePath == assetPath;
        });
        if (asset == m_assets.cend()) {
            updateUpscaleAction();
            return;
        }
        m_library->selectAsset(&*asset);
        setCurrentImage(&*asset, false);
        updateUpscaleAction();
        updateWorkspacePage();
    });
    connect(m_generatePanel,
            &GeneratePanel::referenceChanged,
            this,
            [this](const QString &title, const ImageAsset *asset) {
                statusBar()->showMessage(asset != nullptr
                                             ? tr("Assigned %1 as Generate %2").arg(asset->name, title)
                                             : tr("Cleared Generate %1").arg(title));
            });
    clearCurrentImage();
    restoreLibrary();
    QString imageDirectoryError;
    const bool imageDirectoriesAvailable = AppPaths::ensureManagedDirectories(&imageDirectoryError);
    if (!imageDirectoriesAvailable) {
        statusBar()->showMessage(imageDirectoryError);
    }

    m_serviceClient = new ServiceClient(this);
    m_generatePanel->setGenerationAvailable(false);
    connect(m_serviceClient, &ServiceClient::generationProgressChanged, this, &MainWindow::updateGenerationProgress);
    connect(m_serviceClient, &ServiceClient::generationFinished, this, &MainWindow::handleGenerationFinished);
    connect(m_serviceClient, &ServiceClient::upscaleFinished, this, &MainWindow::handleUpscaleFinished);
    connect(m_serviceClient, &ServiceClient::generationOutput, this, [](const QString &stream, const QString &text) {
        qInfo().noquote() << QStringLiteral("[Kira Service][%1] %2").arg(stream, text);
    });
    connect(m_serviceClient, &ServiceClient::capabilitiesChanged, this, &MainWindow::updateServiceCapabilities);
    connect(m_serviceClient, &ServiceClient::serviceStateChanged, this, [this](const QString &, const QString &message) {
#if defined(Q_OS_MACOS)
        m_generatePanel->setServiceStatus(message);
#endif
        if (!m_generationRunning && !m_upscaleRunning) statusBar()->showMessage(message);
    });
    m_serviceClient->connectToService();

#if defined(Q_OS_MACOS)
    statusBar()->hide();
#endif

    applyDefaultLayout();
    restoreWindowState();
    updateUpscaleAction();
    updateWorkspacePage();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) {
        saveWindowState();
    }
}

void MainWindow::setCurrentImage(const ImageAsset *asset, bool showStatus)
{
    if (asset == nullptr) {
        clearCurrentImage();
        return;
    }

    m_generatePanel->setSelectedAsset(asset);
    if (showStatus) {
        statusBar()->showMessage(tr("Selected %1").arg(asset->name));
    }
}

void MainWindow::clearCurrentImage()
{
    m_generatePanel->setSelectedAsset(nullptr);
}

void MainWindow::openImagePreview(const ImageAsset *asset)
{
    if (asset == nullptr) {
        return;
    }

    m_library->selectAsset(asset);
    setCurrentImage(asset, false);

    for (int index = 0; index < m_previewTabs->count(); ++index) {
        if (m_previewTabs->tabBar()->tabData(index).toString() == asset->filePath) {
            m_previewTabs->setCurrentIndex(index);
            return;
        }
    }

    auto *preview = new ImageViewer(m_previewTabs);
    preview->setImage(asset->image);
    const int index = m_previewTabs->addTab(preview, asset->name);
    m_previewTabs->tabBar()->setTabData(index, asset->filePath);
    m_previewTabs->setCurrentIndex(index);
    updateWorkspacePage();
    updateUpscaleAction();
}

void MainWindow::openImages()
{
    const QStringList filePaths = QFileDialog::getOpenFileNames(this,
                                                                 tr("Open Images"),
                                                                 QString(),
                                                                 imageFileDialogFilter());
    if (filePaths.isEmpty()) {
        return;
    }

    const ImageAsset *firstSelectedAsset = nullptr;
    int importedCount = 0;
    int failedCount = 0;
    for (const QString &filePath : filePaths) {
        const ImageAsset *asset = importImage(filePath);
        if (asset == nullptr) {
            ++failedCount;
            continue;
        }
        if (firstSelectedAsset == nullptr) {
            firstSelectedAsset = asset;
        }
        ++importedCount;
    }

    if (firstSelectedAsset != nullptr) {
        m_library->selectAsset(firstSelectedAsset);
    }
    if (importedCount > 0) {
        saveLibrary();
        QString message = tr("Imported %1 image%2").arg(importedCount).arg(importedCount == 1 ? QString() : QStringLiteral("s"));
        if (failedCount > 0) {
            message += tr("; %1 file%2 could not be loaded")
                           .arg(failedCount)
                           .arg(failedCount == 1 ? QString() : QStringLiteral("s"));
        }
        statusBar()->showMessage(message);
    } else if (failedCount > 0) {
        statusBar()->showMessage(tr("%1 file%2 could not be loaded")
                                     .arg(failedCount)
                                     .arg(failedCount == 1 ? QString() : QStringLiteral("s")));
    }
}

void MainWindow::closePreview(int index)
{
    if (index < 0 || index >= m_previewTabs->count()) {
        return;
    }

    QWidget *preview = m_previewTabs->widget(index);
    m_previewTabs->removeTab(index);
    preview->deleteLater();
    updateWorkspacePage();
    updateUpscaleAction();
}

void MainWindow::closeCurrentPreview()
{
    closePreview(m_previewTabs->currentIndex());
}

const ImageAsset *MainWindow::importImage(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    const QString canonicalPath = fileInfo.canonicalFilePath().isEmpty() ? fileInfo.absoluteFilePath()
                                                                          : fileInfo.canonicalFilePath();
    const auto existingAsset = std::find_if(m_assets.cbegin(), m_assets.cend(), [&canonicalPath](const ImageAsset &asset) {
        return asset.filePath == canonicalPath;
    });
    if (existingAsset != m_assets.cend()) {
        return &*existingAsset;
    }

    QImageReader reader(canonicalPath);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        return nullptr;
    }

    m_assets.push_back({canonicalPath, fileInfo.fileName(), canonicalPath, image, categoryForPath(canonicalPath)});
    const ImageAsset *asset = &m_assets.back();
    m_library->addAsset(*asset);
    return asset;
}

void MainWindow::restoreLibrary()
{
    QSettings settings;
    const QStringList savedPaths = settings.value(LibraryAssetPathsKey).toStringList();
    for (const QString &path : savedPaths) {
        importImage(path);
    }

    restoreManagedOutputImages();

    // Remove stale or unreadable entries while preserving the original image files.
    saveLibrary();
}

void MainWindow::restoreManagedOutputImages()
{
    const std::array<QString, 2> outputDirectories = {AppPaths::generatedPath(), AppPaths::upscaledPath()};
    for (const QString &directoryPath : outputDirectories) {
        const QDir directory(directoryPath);
        const QFileInfoList files = directory.entryInfoList(QDir::Files | QDir::Readable,
                                                            QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &file : files) {
            importImage(file.absoluteFilePath());
        }
    }
}

void MainWindow::saveLibrary() const
{
    QStringList assetPaths;
    assetPaths.reserve(static_cast<qsizetype>(m_assets.size()));
    for (const ImageAsset &asset : m_assets) {
        assetPaths.append(asset.filePath);
    }

    QSettings settings;
    settings.setValue(LibraryAssetPathsKey, assetPaths);
}

void MainWindow::moveAssetToTrash(const QString &filePath)
{
    const auto asset = std::find_if(m_assets.cbegin(), m_assets.cend(), [&filePath](const ImageAsset &candidate) {
        return candidate.filePath == filePath;
    });
    if (asset == m_assets.cend()) {
        return;
    }
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        removeAssetFromLibrary(filePath);
        statusBar()->showMessage(tr("Removed unavailable image %1 from the library").arg(asset->name));
        return;
    }

    QMessageBox confirmation(this);
    confirmation.setIcon(QMessageBox::Question);
    confirmation.setWindowTitle(tr("Move Image to Trash?"));
    confirmation.setText(tr("Move %1 to the Trash?").arg(asset->name));
    confirmation.setInformativeText(tr("Its source file will be removed from the Library."));
    auto *moveButton = confirmation.addButton(tr("Move to Trash"), QMessageBox::AcceptRole);
    confirmation.addButton(QMessageBox::Cancel);
    confirmation.exec();
    if (confirmation.clickedButton() != moveButton) {
        return;
    }

    QFile file(filePath);
    if (!file.moveToTrash()) {
        statusBar()->showMessage(tr("Could not move image to the Trash: %1").arg(file.errorString()));
        return;
    }

    const QString name = asset->name;
    removeAssetFromLibrary(filePath);
    statusBar()->showMessage(tr("Moved %1 to the Trash").arg(name));
}

void MainWindow::removeAssetFromLibrary(const QString &filePath)
{
    const auto asset = std::find_if(m_assets.cbegin(), m_assets.cend(), [&filePath](const ImageAsset &candidate) {
        return candidate.filePath == filePath;
    });
    if (asset == m_assets.cend()) {
        return;
    }

    const ImageAsset *removedAsset = &*asset;
    m_generatePanel->removeAsset(removedAsset);

    for (int index = m_previewTabs->count() - 1; index >= 0; --index) {
        if (m_previewTabs->tabBar()->tabData(index).toString() != filePath) {
            continue;
        }
        QWidget *preview = m_previewTabs->widget(index);
        m_previewTabs->removeTab(index);
        preview->deleteLater();
    }

    m_library->removeAsset(filePath);
    m_assets.erase(asset);
    saveLibrary();
    updateUpscaleAction();
}

void MainWindow::startGeneration(const ImageGenerationRequest &request)
{
    if (m_serviceClient == nullptr || !m_serviceClient->canGenerate()) {
        statusBar()->showMessage(tr("Image generation service is unavailable"));
        return;
    }

    if (request.prompt.trimmed().isEmpty()) {
        statusBar()->showMessage(tr("Enter a prompt before generating"));
        return;
    }
    QString directoryError;
    if (!AppPaths::ensureManagedDirectories(&directoryError)) {
        statusBar()->showMessage(directoryError);
        return;
    }

    const QString outputPath = QDir(AppPaths::generatedPath())
                             .filePath(QStringLiteral("generated-%1.png")
                                           .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmsszzz"))));
    ImageGenerationRequest serviceRequest = request;
    if (serviceRequest.seed < 0) serviceRequest.seed = QRandomGenerator::global()->bounded(1, std::numeric_limits<int>::max());
    if (!m_serviceClient->startGeneration(serviceRequest, outputPath)) {
        statusBar()->showMessage(tr("Generation is already running"));
        return;
    }
    m_generationRunning = true;
    m_generatePanel->setGenerationRunning(true);
    updateUpscaleAction();
}

void MainWindow::updateGenerationProgress(const GenerationProgress &progress)
{
    m_generationProgressBar->setValue(progress.percentage);
    m_generationProgressBar->show();

    if (progress.stage == GenerationProgressStage::Complete || progress.stage == GenerationProgressStage::Failed) {
        statusBar()->showMessage(progress.statusText);
        return;
    }
    statusBar()->showMessage(tr("Generating image · %1").arg(progress.statusText));
}

void MainWindow::hideGenerationProgress()
{
    m_generationProgressBar->hide();
    m_generationProgressBar->setValue(0);
}

void MainWindow::handleGenerationFinished(const SdCliGenerationResult &result)
{
    m_generationRunning = false;
    m_generatePanel->setGenerationRunning(false);
    updateUpscaleAction();
    if (!result.succeeded) {
        hideGenerationProgress();
        if (result.exitCode < 0) {
            statusBar()->showMessage(tr("Could not start sd-cli"));
        } else {
            statusBar()->showMessage(tr("Image generation failed (sd-cli exited with code %1)").arg(result.exitCode));
        }
        return;
    }

    if (!QFileInfo(result.outputPath).isFile()) {
        hideGenerationProgress();
        statusBar()->showMessage(tr("Image generation finished without an output image"));
        return;
    }

    const ImageAsset *asset = importImage(result.outputPath);
    if (asset == nullptr) {
        hideGenerationProgress();
        statusBar()->showMessage(tr("Generated image could not be opened"));
        return;
    }
    saveLibrary();
    m_library->selectAsset(asset);
    statusBar()->showMessage(tr("Generation complete"));
    QTimer::singleShot(2000, this, [this] {
        if (!m_generationRunning) {
            hideGenerationProgress();
            if (statusBar()->currentMessage() == tr("Generation complete")) {
                statusBar()->clearMessage();
            }
        }
    });
}

const ImageAsset *MainWindow::activePreviewAsset() const
{
    const int index = m_previewTabs->currentIndex();
    if (index < 0) {
        return nullptr;
    }

    const QString filePath = m_previewTabs->tabBar()->tabData(index).toString();
    const auto asset = std::find_if(m_assets.cbegin(), m_assets.cend(), [&filePath](const ImageAsset &candidate) {
        return candidate.filePath == filePath;
    });
    return asset == m_assets.cend() ? nullptr : &*asset;
}

void MainWindow::updateUpscaleAction()
{
    m_upscaleAction->setEnabled(m_upscaleAvailable && !m_generationRunning && !m_upscaleRunning && activePreviewAsset() != nullptr);
    m_closePreviewAction->setEnabled(m_previewTabs->currentIndex() >= 0);
}

void MainWindow::showUpscaleDialog()
{
    const ImageAsset *source = activePreviewAsset();
    if (source == nullptr) {
        updateUpscaleAction();
        return;
    }

    UpscaleDialog dialog(source->image.size(), this);
    if (dialog.exec() == QDialog::Accepted) {
        startUpscale(*source, dialog.scale());
    }
}

void MainWindow::startUpscale(const ImageAsset &source, int scale)
{
    if (m_serviceClient == nullptr || !m_serviceClient->canUpscale()) {
        statusBar()->showMessage(tr("Image upscaling service is unavailable"));
        return;
    }
    if (scale != 2 && scale != 4) {
        statusBar()->showMessage(tr("Unsupported upscale scale"));
        return;
    }
    if (!QFileInfo(source.filePath).isFile()) {
        statusBar()->showMessage(tr("The source image is unavailable"));
        return;
    }

    QString directoryError;
    if (!AppPaths::ensureManagedDirectories(&directoryError)) {
        statusBar()->showMessage(directoryError);
        return;
    }

    const QString outputPath = QDir(AppPaths::upscaledPath())
                                   .filePath(QStringLiteral("upscaled-%1.png")
                                                 .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmsszzz"))));
    if (!m_serviceClient->startUpscale({source.filePath, scale}, outputPath)) {
        statusBar()->showMessage(tr("Upscaling is already running"));
        return;
    }
    m_upscaleRunning = true;
    updateUpscaleAction();
    statusBar()->showMessage(tr("Upscaling image..."));
}

void MainWindow::showServicePreferences()
{
    ServicePreferencesDialog dialog(m_serviceClient, this);
    if (dialog.exec() == QDialog::Accepted) m_serviceClient->connectToService();
}

void MainWindow::updateServiceCapabilities(bool generationAvailable, bool upscaleAvailable)
{
    m_generationAvailable = generationAvailable;
    m_upscaleAvailable = upscaleAvailable;
    m_generatePanel->setGenerationAvailable(generationAvailable);
    updateUpscaleAction();
}

void MainWindow::handleUpscaleFinished(const SdCliUpscaleResult &result)
{
    m_upscaleRunning = false;
    updateUpscaleAction();
    if (!result.succeeded) {
        statusBar()->showMessage(tr("Image upscaling failed"));
        return;
    }
    if (!QFileInfo(result.outputPath).isFile()) {
        statusBar()->showMessage(tr("Image upscaling finished without an output image"));
        return;
    }

    const ImageAsset *asset = importImage(result.outputPath);
    if (asset == nullptr) {
        statusBar()->showMessage(tr("Upscaled image could not be opened"));
        return;
    }
    saveLibrary();
    m_library->selectAsset(asset);
    statusBar()->showMessage(tr("Upscaling complete"));
}

void MainWindow::applyDefaultLayout()
{
    resize(1600, 1000);
#if defined(Q_OS_MACOS)
    m_workspaceSplitter->setSizes({260, 1020, 320});
#else
    m_workspaceSplitter->setSizes({340, 880, 380});
#endif
}

void MainWindow::updateWorkspacePage()
{
#if defined(Q_OS_MACOS)
    if (m_workspacePages != nullptr) {
        m_workspacePages->setCurrentIndex(m_previewTabs->count() == 0 ? 0 : 1);
    }
#endif
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
#if defined(Q_OS_MACOS)
    if (watched == m_previewWorkspace && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        positionTransientStatusMessage();
    }
#else
    Q_UNUSED(watched)
    Q_UNUSED(event)
#endif
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::showTransientStatusMessage(const QString &message)
{
#if defined(Q_OS_MACOS)
    if (message.isEmpty()) {
        m_transientStatusMessage->hide();
        return;
    }

    m_transientStatusMessage->setText(message);
    m_transientStatusMessage->adjustSize();
    positionTransientStatusMessage();
    m_transientStatusMessage->show();
    m_transientStatusMessage->raise();
    m_transientStatusTimer->start(4000);
#else
    Q_UNUSED(message)
#endif
}

void MainWindow::positionTransientStatusMessage()
{
#if defined(Q_OS_MACOS)
    if (m_previewWorkspace == nullptr || m_transientStatusMessage == nullptr) {
        return;
    }

    const QSize preferredSize = m_transientStatusMessage->sizeHint().expandedTo({160, 24});
    const int width = qMin(preferredSize.width() + 24, m_previewWorkspace->width() - 24);
    m_transientStatusMessage->resize(width, preferredSize.height() + 8);
    m_transientStatusMessage->move((m_previewWorkspace->width() - width) / 2,
                                   m_previewWorkspace->height() - m_transientStatusMessage->height() - 16);
#endif
}

void MainWindow::restoreWindowState()
{
    QSettings settings;
    const QByteArray geometry = settings.value(QStringLiteral("MainWindow/Geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    const QByteArray workspaceSplitterState = settings.value(QStringLiteral("MainWindow/WorkspaceSplitter")).toByteArray();
    if (!workspaceSplitterState.isEmpty()) {
        m_workspaceSplitter->restoreState(workspaceSplitterState);
    }
}

void MainWindow::saveWindowState()
{
    saveLibrary();
    QSettings settings;
    settings.setValue(QStringLiteral("MainWindow/Geometry"), saveGeometry());
    settings.setValue(QStringLiteral("MainWindow/WorkspaceSplitter"), m_workspaceSplitter->saveState());
    settings.remove(QStringLiteral("MainWindow/LeftSplitter"));
}
