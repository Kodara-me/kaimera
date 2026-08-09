#pragma once

#include <QDialog>
#include <QSize>

class QComboBox;
class QLabel;

class UpscaleDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit UpscaleDialog(const QSize &sourceSize, QWidget *parent = nullptr);

    int scale() const;
    QSize outputSize() const;

private:
    void updateOutputSize();

    QSize m_sourceSize;
    QComboBox *m_scaleSelector = nullptr;
    QLabel *m_outputSizeLabel = nullptr;
};
