#include "MacWorkspaceSplitter.h"

#include <QSplitterHandle>

namespace
{
class MacWorkspaceSplitterHandle final : public QSplitterHandle
{
public:
    explicit MacWorkspaceSplitterHandle(Qt::Orientation orientation, QSplitter *parent)
        : QSplitterHandle(orientation, parent)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
    }
};
}

MacWorkspaceSplitter::MacWorkspaceSplitter(QWidget *parent)
    : QSplitter(Qt::Horizontal, parent)
{
    setHandleWidth(10);
}

QSplitterHandle *MacWorkspaceSplitter::createHandle()
{
    return new MacWorkspaceSplitterHandle(orientation(), this);
}
