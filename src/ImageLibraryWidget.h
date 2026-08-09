#pragma once

#include "ImageAsset.h"

#include <QGroupBox>

#include <deque>

class QListWidget;
class QListWidgetItem;
class QAbstractItemDelegate;
class QLabel;
class QComboBox;
class QLineEdit;

class ImageLibraryWidget final : public QGroupBox
{
    Q_OBJECT

public:
    explicit ImageLibraryWidget(const std::deque<ImageAsset> &assets, QWidget *parent = nullptr);

    void addAsset(const ImageAsset &asset);
    bool removeAsset(const QString &filePath);
    void selectAsset(const ImageAsset *asset);

signals:
    void imageSelected(const ImageAsset *asset);
    void imagePreviewRequested(const ImageAsset *asset);
    void moveToTrashRequested(const QString &filePath);

private:
    const ImageAsset *assetForItem(const QListWidgetItem *item) const;
    void applyFilters();
    void setGridView(bool enabled);
    void requestMoveToTrash();
    void updateEmptyState();

    const std::deque<ImageAsset> &m_assets;
    QListWidget *m_thumbnailView;
    QLabel *m_emptyState;
    QLineEdit *m_searchField = nullptr;
    QComboBox *m_filterSelector = nullptr;
    QAbstractItemDelegate *m_gridDelegate = nullptr;
    QAbstractItemDelegate *m_listDelegate = nullptr;
};
