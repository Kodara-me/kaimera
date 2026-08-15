#pragma once

#include <QSplitter>

class MacWorkspaceSplitter final : public QSplitter
{
public:
    explicit MacWorkspaceSplitter(QWidget *parent = nullptr);

protected:
    QSplitterHandle *createHandle() override;
};
