#pragma once

#include <QWidget>

class MacPanelSurface final : public QWidget
{
public:
    explicit MacPanelSurface(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};
