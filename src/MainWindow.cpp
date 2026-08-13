#include "MainWindow.h"

#include "AppPaths.h"
#include "CuratedResourceManager.h"
#include "GeneratePanel.h"
#include "ImageLibraryWidget.h"
#include "ImageViewer.h"
#include "SdCliRunner.h"
#include "UpscaleDialog.h"

#include <KIO/CopyJob>
#include <KJob>
#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <QTabBar>
#include <QToolBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <limits>

namespace
{
const QString LibraryAssetPathsKey = QStringLiteral("Library/AssetPaths");

QIcon themedIcon(const QString &name, QStyle::StandardPixmap fallback, const QWidget *widget)
{
    return QIcon::fromTheme(name, widget->style()->standardIcon(fallback));
}

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

MainWindow::MainWindow(CuratedResourceManager &curatedResources, QWidget *parent)
    : QMainWindow(parent)
    , m_curatedResources(curatedResources)
{
    setWindowTitle(tr("Kira Canvas"));
    resize(1600, 1000);

    auto *toolBar = addToolBar(tr("Main"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *importPhotoAction = toolBar->addAction(
        themedIcon(QStringLiteral("document-open"), QStyle::SP_DialogOpenButton, this), tr("Import Photo"));
    m_upscaleAction = toolBar->addAction(themedIcon(QStringLiteral("zoom-in"), QStyle::SP_ArrowUp, this), tr("Upscale"));
    m_upscaleAction->setObjectName(QStringLiteral("upscaleAction"));
    m_upscaleAction->setEnabled(false);

    m_generationProgressBar = new QProgressBar(statusBar());
    m_generationProgressBar->setRange(0, 100);
    m_generationProgressBar->setTextVisible(true);
    m_generationProgressBar->setFormat(QStringLiteral("%p%"));
    m_generationProgressBar->setFixedWidth(120);
    m_generationProgressBar->hide();
    statusBar()->addPermanentWidget(m_generationProgressBar);

    m_library = new ImageLibraryWidget(m_assets, this);
    m_library->setMinimumWidth(250);

    auto *workspace = new QWidget(this);
    auto *workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    m_previewTabs = new QTabWidget(workspace);
    m_previewTabs->setTabsClosable(true);
    workspaceLayout->addWidget(m_previewTabs, 1);

    m_generatePanel = new GeneratePanel(statusBar(), this);
    m_generatePanel->setMinimumWidth(280);

    m_workspaceSplitter = new QSplitter(Qt::Horizontal, this);
    m_workspaceSplitter->addWidget(m_library);
    m_workspaceSplitter->addWidget(workspace);
    m_workspaceSplitter->addWidget(m_generatePanel);
    m_workspaceSplitter->setStretchFactor(0, 0);
    m_workspaceSplitter->setStretchFactor(1, 1);
    m_workspaceSplitter->setStretchFactor(2, 0);
    setCentralWidget(m_workspaceSplitter);

    connect(importPhotoAction, &QAction::triggered, this, &MainWindow::openImages);
    connect(m_upscaleAction, &QAction::triggered, this, &MainWindow::showUpscaleDialog);
    connect(m_generatePanel, &GeneratePanel::generationRequested, this, &MainWindow::startGeneration);
    connect(m_library, &ImageLibraryWidget::imageSelected, this, [this](const ImageAsset *asset) {
        setCurrentImage(asset, true);
    });
    connect(m_library, &ImageLibraryWidget::imagePreviewRequested, this, &MainWindow::openImagePreview);
    connect(m_library, &ImageLibraryWidget::moveToTrashRequested, this, &MainWindow::moveAssetToTrash);
    connect(m_previewTabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget *preview = m_previewTabs->widget(index);
        m_previewTabs->removeTab(index);
        preview->deleteLater();
        updateUpscaleAction();
    });
    connect(m_previewTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index < 0) {
            updateUpscaleAction();
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

    const QString sdCliPath = QStandardPaths::findExecutable(QStringLiteral("sd-cli"));
    if (sdCliPath.isEmpty()) {
        statusBar()->showMessage(tr("sd-cli is unavailable"));
    } else {
        m_sdCliRunner = new SdCliRunner(sdCliPath, this);
        connect(m_sdCliRunner,
                &SdCliRunner::generationProgressChanged,
                this,
                &MainWindow::updateGenerationProgress);
        connect(m_sdCliRunner, &SdCliRunner::generationFinished, this, &MainWindow::handleGenerationFinished);
        connect(m_sdCliRunner, &SdCliRunner::upscaleFinished, this, &MainWindow::handleUpscaleFinished);
    }

    applyDefaultLayout();
    restoreWindowState();
    updateUpscaleAction();
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
    if (asset == m_assets.cend() || m_assetsBeingTrashed.contains(filePath)) {
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

    m_assetsBeingTrashed.insert(filePath);
    statusBar()->showMessage(tr("Moving %1 to the Trash...").arg(asset->name));
    KIO::CopyJob *job = KIO::trash(QUrl::fromLocalFile(filePath), KIO::HideProgressInfo);
    connect(job, &KJob::result, this, [this, filePath](KJob *completedJob) {
        m_assetsBeingTrashed.remove(filePath);
        if (completedJob->error() != 0) {
            statusBar()->showMessage(tr("Could not move image to the Trash: %1").arg(completedJob->errorString()));
            return;
        }

        const auto asset = std::find_if(m_assets.cbegin(), m_assets.cend(), [&filePath](const ImageAsset &candidate) {
            return candidate.filePath == filePath;
        });
        const QString name = asset == m_assets.cend() ? QFileInfo(filePath).fileName() : asset->name;
        removeAssetFromLibrary(filePath);
        statusBar()->showMessage(tr("Moved %1 to the Trash").arg(name));
    });
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

void MainWindow::startGeneration(GenerationRequest request)
{
    if (m_sdCliRunner == nullptr) {
        statusBar()->showMessage(tr("sd-cli is unavailable"));
        return;
    }

    if (request.prompt.trimmed().isEmpty()) {
        statusBar()->showMessage(tr("Enter a prompt before generating"));
        return;
    }
    if (!m_curatedResources.isInstalled(CuratedResourceId::ImageGenerationDefault)) {
        statusBar()->showMessage(tr("Image generation model is unavailable"));
        return;
    }
    request.modelPath = m_curatedResources.path(CuratedResourceId::ImageGenerationDefault);
    if (!QFileInfo(request.modelPath).isFile()) {
        statusBar()->showMessage(tr("Image generation model is unavailable"));
        return;
    }

    QString directoryError;
    if (!AppPaths::ensureManagedDirectories(&directoryError)) {
        statusBar()->showMessage(directoryError);
        return;
    }

    request.outputPath = QDir(AppPaths::generatedPath())
                             .filePath(QStringLiteral("generated-%1.png")
                                           .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmsszzz"))));
    if (request.seed < 0) {
        request.seed = QRandomGenerator::global()->bounded(1, std::numeric_limits<int>::max());
    }

    if (!m_sdCliRunner->runGeneration(request)) {
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
    m_upscaleAction->setEnabled(!m_generationRunning && !m_upscaleRunning && activePreviewAsset() != nullptr);
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
    if (m_sdCliRunner == nullptr) {
        statusBar()->showMessage(tr("sd-cli is unavailable"));
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

    const CuratedResourceId resourceId = scale == 4 ? CuratedResourceId::Upscaler4x : CuratedResourceId::Upscaler2x;
    if (!m_curatedResources.isInstalled(resourceId)) {
        statusBar()->showMessage(tr("%1 upscaler is unavailable").arg(scale == 4 ? QStringLiteral("4×") : QStringLiteral("2×")));
        return;
    }
    const QString upscalerPath = m_curatedResources.path(resourceId);
    if (!QFileInfo(upscalerPath).isFile()) {
        statusBar()->showMessage(tr("%1 upscaler is unavailable").arg(scale == 4 ? QStringLiteral("4×") : QStringLiteral("2×")));
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
    const QStringList arguments = {
        QStringLiteral("--mode"), QStringLiteral("upscale"),
        QStringLiteral("--init-img"), source.filePath,
        QStringLiteral("--upscale-model"), upscalerPath,
        QStringLiteral("--upscale-repeats"), QStringLiteral("1"),
        QStringLiteral("--upscale-tile-size"), QStringLiteral("128"),
        QStringLiteral("--output"), outputPath};
    if (!m_sdCliRunner->runUpscale(arguments, outputPath)) {
        statusBar()->showMessage(tr("Upscaling is already running"));
        return;
    }
    m_upscaleRunning = true;
    updateUpscaleAction();
    statusBar()->showMessage(tr("Upscaling image..."));
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
    m_workspaceSplitter->setSizes({340, 880, 380});
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
