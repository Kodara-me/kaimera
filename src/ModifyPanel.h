#pragma once

#include <QWidget>

struct ImageAsset;
class QLabel;
class QPushButton;
class QStatusBar;
class ReferenceSlotWidget;

class ModifyPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit ModifyPanel(QStatusBar *statusBar, QWidget *parent = nullptr);

    void setSourceImage(const ImageAsset &asset);
    void clearSourceImage();
    void setSelectedAsset(const ImageAsset *asset);
    void removeAsset(const ImageAsset *asset);

signals:
    void referenceChanged(const QString &title, const ImageAsset *asset);

private:
    QLabel *m_sourceName;
    QLabel *m_sourceSize;
    QPushButton *m_modifyButton;
    ReferenceSlotWidget *m_faceReference = nullptr;
    ReferenceSlotWidget *m_styleReference = nullptr;
    ReferenceSlotWidget *m_maskReference = nullptr;
    QString m_sourcePath;
};
