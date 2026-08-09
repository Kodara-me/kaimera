#pragma once

#include "ImageAsset.h"

#include <QWidget>

class QLabel;
class QToolButton;

class ReferenceSlotWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ReferenceSlotWidget(const QString &title, QWidget *parent = nullptr);

    void setSelectedAsset(const ImageAsset *asset);
    void clearAsset(const ImageAsset *asset);

signals:
    void assignmentChanged(const QString &title, const ImageAsset *asset);

private:
    void updateDisplay();

    QString m_title;
    const ImageAsset *m_selectedAsset = nullptr;
    const ImageAsset *m_assignedAsset = nullptr;
    QLabel *m_thumbnail = nullptr;
    QToolButton *m_assignButton = nullptr;
    QToolButton *m_clearButton = nullptr;
};
