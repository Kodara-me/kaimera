#include "ImageLibraryWidget.h"

#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTextLayout>
#include <QTextOption>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
constexpr QSize LibraryPreviewZoneSize(136, 96);
constexpr int LibraryHorizontalPadding = 8;
constexpr int LibraryVerticalPadding = 8;
constexpr int LibraryFilenameSpacing = 4;
constexpr QSize LibraryListPreviewSize(48, 36);
constexpr int LibraryListSpacing = 8;
constexpr int AssetPathRole = Qt::UserRole;
constexpr int AssetCategoryRole = Qt::UserRole + 1;
constexpr int AssetImageRole = Qt::UserRole + 2;

QIcon themedIcon(const QString &name, QStyle::StandardPixmap fallback, const QWidget *widget)
{
    return QIcon::fromTheme(name, widget->style()->standardIcon(fallback));
}

QString categoryName(ImageAssetCategory category)
{
    switch (category) {
    case ImageAssetCategory::Generated:
        return QObject::tr("Generated");
    case ImageAssetCategory::Imported:
        return QObject::tr("Imported");
    case ImageAssetCategory::Upscaled:
        return QObject::tr("Upscaled");
    }
    return {};
}

int filenameHeight(const QFont &font, const QString &name, int width)
{
    QTextLayout layout(name, font);
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(textOption);

    qreal height = 0;
    layout.beginLayout();
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(width);
        line.setPosition(QPointF(0, height));
        height += line.height();
    }
    layout.endLayout();
    return static_cast<int>(std::ceil(height));
}

void drawItemBackground(QPainter *painter, const QStyleOptionViewItem &itemOption)
{
    const QWidget *widget = itemOption.widget;
    QStyle *style = widget != nullptr ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &itemOption, painter, widget);
}

void drawPreview(QPainter *painter, const QImage &image, const QRect &previewZone, Qt::LayoutDirection direction)
{
    if (image.isNull()) {
        return;
    }

    const QImage thumbnail = image.scaled(previewZone.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QRect thumbnailRect = QStyle::alignedRect(direction, Qt::AlignCenter, thumbnail.size(), previewZone);
    painter->drawImage(thumbnailRect, thumbnail);
}

QColor itemTextColor(const QStyleOptionViewItem &itemOption)
{
    const QPalette::ColorRole textColor = itemOption.state.testFlag(QStyle::State_Selected)
                                               ? QPalette::HighlightedText
                                               : QPalette::Text;
    return itemOption.palette.color(textColor);
}

class LibraryGridItemDelegate final : public QStyledItemDelegate
{
public:
    explicit LibraryGridItemDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem itemOption(option);
        initStyleOption(&itemOption, index);
        itemOption.icon = QIcon();
        itemOption.text.clear();
        drawItemBackground(painter, itemOption);

        const QRect previewZone = QStyle::alignedRect(option.direction,
                                                       Qt::AlignHCenter | Qt::AlignTop,
                                                       LibraryPreviewZoneSize,
                                                       option.rect.adjusted(0, LibraryVerticalPadding, 0, 0));
        drawPreview(painter, index.data(AssetImageRole).value<QImage>(), previewZone, option.direction);

        const QString name = index.data(Qt::DisplayRole).toString();
        QTextOption textOption(Qt::AlignHCenter | Qt::AlignTop);
        textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        painter->setPen(itemTextColor(itemOption));
        const int textHeight = filenameHeight(itemOption.font, name, LibraryPreviewZoneSize.width());
        const QRect filenameArea(option.rect.left() + LibraryHorizontalPadding,
                                 previewZone.bottom() + LibraryFilenameSpacing + 1,
                                 LibraryPreviewZoneSize.width(),
                                 textHeight);
        painter->drawText(filenameArea, name, textOption);
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem itemOption(option);
        initStyleOption(&itemOption, index);
        const QString name = itemOption.text;
        return {LibraryPreviewZoneSize.width() + 2 * LibraryHorizontalPadding,
                2 * LibraryVerticalPadding + LibraryPreviewZoneSize.height() + LibraryFilenameSpacing
                    + filenameHeight(itemOption.font, name, LibraryPreviewZoneSize.width())};
    }
};

class LibraryListItemDelegate final : public QStyledItemDelegate
{
public:
    explicit LibraryListItemDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem itemOption(option);
        initStyleOption(&itemOption, index);
        itemOption.icon = QIcon();
        itemOption.text.clear();
        drawItemBackground(painter, itemOption);

        const QRect previewZone(option.rect.left() + LibraryHorizontalPadding,
                                option.rect.top() + LibraryVerticalPadding,
                                LibraryListPreviewSize.width(),
                                LibraryListPreviewSize.height());
        drawPreview(painter, index.data(AssetImageRole).value<QImage>(), previewZone, option.direction);

        const QRect filenameArea(previewZone.right() + LibraryListSpacing + 1,
                                 option.rect.top() + LibraryVerticalPadding,
                                 option.rect.right() - previewZone.right() - LibraryListSpacing
                                     - LibraryHorizontalPadding,
                                 option.rect.height() - 2 * LibraryVerticalPadding);
        QTextOption textOption(Qt::AlignVCenter | Qt::AlignLeft);
        textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        painter->setPen(itemTextColor(itemOption));
        painter->drawText(filenameArea, index.data(Qt::DisplayRole).toString(), textOption);
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem itemOption(option);
        initStyleOption(&itemOption, index);
        const auto *view = qobject_cast<const QListView *>(itemOption.widget);
        const int itemWidth = view != nullptr ? view->viewport()->width() : 240;
        const int textWidth = std::max(1,
                                       itemWidth - 2 * LibraryHorizontalPadding - LibraryListPreviewSize.width()
                                           - LibraryListSpacing);
        const int textHeight = filenameHeight(itemOption.font, itemOption.text, textWidth);
        return {itemWidth,
                std::max(LibraryListPreviewSize.height(), textHeight) + 2 * LibraryVerticalPadding};
    }
};
}

ImageLibraryWidget::ImageLibraryWidget(const std::deque<ImageAsset> &assets, QWidget *parent)
    : QGroupBox(tr("Library"), parent)
    , m_assets(assets)
    , m_thumbnailView(new QListWidget(this))
{
    auto *layout = new QVBoxLayout(this);

    auto *searchRow = new QHBoxLayout;
    m_searchField = new QLineEdit(this);
    m_searchField->setObjectName(QStringLiteral("librarySearchField"));
    m_searchField->setPlaceholderText(tr("Search images..."));
    searchRow->addWidget(m_searchField);
    layout->addLayout(searchRow);

    auto *viewRow = new QHBoxLayout;
    m_filterSelector = new QComboBox(this);
    m_filterSelector->setObjectName(QStringLiteral("libraryCategorySelector"));
    m_filterSelector->addItem(tr("All Images"));
    for (const ImageAssetCategory category : {ImageAssetCategory::Generated,
                                              ImageAssetCategory::Imported,
                                              ImageAssetCategory::Upscaled}) {
        m_filterSelector->addItem(categoryName(category), static_cast<int>(category));
    }
    viewRow->addWidget(m_filterSelector, 1);

    auto *viewModeGroup = new QButtonGroup(this);
    viewModeGroup->setExclusive(true);
    auto *gridButton = new QToolButton(this);
    gridButton->setObjectName(QStringLiteral("libraryGridButton"));
    gridButton->setCheckable(true);
    gridButton->setChecked(true);
    gridButton->setIcon(themedIcon(QStringLiteral("view-grid"), QStyle::SP_FileDialogDetailedView, this));
    gridButton->setToolTip(tr("Grid view"));
    viewModeGroup->addButton(gridButton);
    viewRow->addWidget(gridButton);
    auto *listButton = new QToolButton(this);
    listButton->setObjectName(QStringLiteral("libraryListButton"));
    listButton->setCheckable(true);
    listButton->setIcon(themedIcon(QStringLiteral("view-list"), QStyle::SP_FileDialogListView, this));
    listButton->setToolTip(tr("List view"));
    viewModeGroup->addButton(listButton);
    viewRow->addWidget(listButton);
    layout->addLayout(viewRow);

    m_thumbnailView->setObjectName(QStringLiteral("libraryAssetView"));
    m_thumbnailView->setResizeMode(QListView::Adjust);
    m_thumbnailView->setMovement(QListView::Static);
    m_thumbnailView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_thumbnailView->setTextElideMode(Qt::ElideNone);
    m_thumbnailView->setUniformItemSizes(false);
    m_thumbnailView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_gridDelegate = new LibraryGridItemDelegate(m_thumbnailView);
    m_listDelegate = new LibraryListItemDelegate(m_thumbnailView);
    setGridView(true);

    auto *moveToTrashAction = new QAction(
        themedIcon(QStringLiteral("user-trash"), QStyle::SP_DialogDiscardButton, this), tr("Move to Trash"), m_thumbnailView);
    moveToTrashAction->setShortcut(QKeySequence::Delete);
    moveToTrashAction->setShortcutContext(Qt::WidgetShortcut);
    m_thumbnailView->addAction(moveToTrashAction);
    connect(moveToTrashAction, &QAction::triggered, this, &ImageLibraryWidget::requestMoveToTrash);
    connect(m_thumbnailView, &QListWidget::customContextMenuRequested, this, [this, moveToTrashAction](const QPoint &position) {
        QListWidgetItem *item = m_thumbnailView->itemAt(position);
        if (item == nullptr) {
            return;
        }

        m_thumbnailView->setCurrentItem(item);
        QMenu menu(m_thumbnailView);
        menu.addAction(moveToTrashAction);
        menu.exec(m_thumbnailView->viewport()->mapToGlobal(position));
    });
    connect(m_searchField, &QLineEdit::textChanged, this, &ImageLibraryWidget::applyFilters);
    connect(m_filterSelector,
            &QComboBox::currentIndexChanged,
            this,
            [this](int) { applyFilters(); });
    connect(gridButton, &QToolButton::toggled, this, &ImageLibraryWidget::setGridView);

    m_emptyState = new QLabel(this);
    m_emptyState->setObjectName(QStringLiteral("libraryEmptyState"));
    m_emptyState->setAlignment(Qt::AlignCenter);
    for (const ImageAsset &asset : m_assets) {
        addAsset(asset);
    }
    connect(m_thumbnailView, &QListWidget::currentItemChanged, this, [this](const QListWidgetItem *item) {
        emit imageSelected(assetForItem(item));
    });
    connect(m_thumbnailView, &QListWidget::itemDoubleClicked, this, [this](const QListWidgetItem *item) {
        emit imagePreviewRequested(assetForItem(item));
    });
    layout->addWidget(m_thumbnailView, 1);
    layout->addWidget(m_emptyState, 1);
    applyFilters();
}

void ImageLibraryWidget::addAsset(const ImageAsset &asset)
{
    auto *item = new QListWidgetItem(asset.name, m_thumbnailView);
    item->setData(AssetPathRole, asset.filePath);
    item->setData(AssetCategoryRole, static_cast<int>(asset.category));
    item->setData(AssetImageRole, asset.image);
    applyFilters();
}

bool ImageLibraryWidget::removeAsset(const QString &filePath)
{
    for (int index = 0; index < m_thumbnailView->count(); ++index) {
        QListWidgetItem *item = m_thumbnailView->item(index);
        if (item->data(AssetPathRole).toString() != filePath) {
            continue;
        }

        delete m_thumbnailView->takeItem(index);
        applyFilters();
        return true;
    }
    return false;
}

void ImageLibraryWidget::selectAsset(const ImageAsset *asset)
{
    if (asset == nullptr) {
        m_thumbnailView->setCurrentItem(nullptr);
        return;
    }

    for (int index = 0; index < m_thumbnailView->count(); ++index) {
        QListWidgetItem *item = m_thumbnailView->item(index);
        if (item->data(AssetPathRole).toString() == asset->filePath) {
            m_thumbnailView->setCurrentItem(item);
            m_thumbnailView->scrollToItem(item);
            return;
        }
    }
}

const ImageAsset *ImageLibraryWidget::assetForItem(const QListWidgetItem *item) const
{
    if (item == nullptr) {
        return nullptr;
    }

    const QString filePath = item->data(AssetPathRole).toString();
    const auto asset = std::find_if(m_assets.cbegin(), m_assets.cend(), [&filePath](const ImageAsset &candidate) {
        return candidate.filePath == filePath;
    });
    return asset == m_assets.cend() ? nullptr : &*asset;
}

void ImageLibraryWidget::applyFilters()
{
    const QString searchText = m_searchField->text();
    const QVariant categoryData = m_filterSelector->currentData();
    for (int index = 0; index < m_thumbnailView->count(); ++index) {
        QListWidgetItem *item = m_thumbnailView->item(index);
        const bool matchesName = item->text().contains(searchText, Qt::CaseInsensitive);
        const bool matchesCategory = !categoryData.isValid()
                                     || item->data(AssetCategoryRole).toInt() == categoryData.toInt();
        item->setHidden(!matchesName || !matchesCategory);
    }

    QListWidgetItem *currentItem = m_thumbnailView->currentItem();
    if (currentItem != nullptr && currentItem->isHidden()) {
        m_thumbnailView->setCurrentItem(nullptr);
    }
    updateEmptyState();
}

void ImageLibraryWidget::setGridView(bool enabled)
{
    if (enabled) {
        m_thumbnailView->setViewMode(QListView::IconMode);
        m_thumbnailView->setFlow(QListView::LeftToRight);
        m_thumbnailView->setWrapping(true);
        m_thumbnailView->setSpacing(6);
        m_thumbnailView->setItemDelegate(m_gridDelegate);
    } else {
        m_thumbnailView->setViewMode(QListView::ListMode);
        m_thumbnailView->setFlow(QListView::TopToBottom);
        m_thumbnailView->setWrapping(false);
        m_thumbnailView->setSpacing(2);
        m_thumbnailView->setItemDelegate(m_listDelegate);
    }
    m_thumbnailView->setGridSize({});
    m_thumbnailView->doItemsLayout();
}

void ImageLibraryWidget::requestMoveToTrash()
{
    const QListWidgetItem *item = m_thumbnailView->currentItem();
    if (item == nullptr || item->isHidden()) {
        return;
    }
    emit moveToTrashRequested(item->data(AssetPathRole).toString());
}

void ImageLibraryWidget::updateEmptyState()
{
    const bool hasAssets = m_thumbnailView->count() > 0;
    bool hasVisibleAssets = false;
    for (int index = 0; index < m_thumbnailView->count(); ++index) {
        if (!m_thumbnailView->item(index)->isHidden()) {
            hasVisibleAssets = true;
            break;
        }
    }

    m_thumbnailView->setVisible(hasVisibleAssets);
    m_emptyState->setText(hasAssets ? tr("No matching images") : tr("No images yet\nOpen images to add them to the library."));
    m_emptyState->setVisible(!hasVisibleAssets);
}
