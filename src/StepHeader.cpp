#include "StepHeader.h"
#include <Ardoise/Theme.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>

namespace
{
// Numbered accent disc that marks each step of a guided path.
class StepBadge final : public QWidget
{
public:
    StepBadge(int number, QWidget* parent)
        : QWidget(parent)
        , m_number(number)
    {
        const int size = Ardoise::Theme::tokens().metrics.iconSize + Ardoise::Theme::tokens().metrics.spacing2;
        setFixedSize(size, size);
        setFont(Ardoise::Theme::font(Ardoise::TextRole::Caption));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& colors = Ardoise::Theme::tokens().colors;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(colors.borderAccent, 1));
        painter.setBrush(colors.accentMuted);
        painter.drawEllipse(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
        QFont bold = font();
        bold.setBold(true);
        painter.setFont(bold);
        painter.setPen(colors.textPrimary);
        painter.drawText(rect(), Qt::AlignCenter, QString::number(m_number));
    }

private:
    int m_number;
};
}

QHBoxLayout* StepHeader::create(int number, const QString& title, const QString& caption, const QString& objectName,
    QWidget* parent)
{
    const auto& metrics = Ardoise::Theme::tokens().metrics;
    auto* header = new QHBoxLayout;
    header->setSpacing(metrics.spacing8);
    header->addWidget(new StepBadge(number, parent));
    auto* label = new QLabel(title, parent);
    label->setObjectName(objectName);
    Ardoise::Theme::applyTypography(label, Ardoise::TextRole::BodyStrong);
    // Every header has the height of a small action button, so headers with and without
    // actions line up side by side.
    label->setMinimumHeight(metrics.controlHeightSmall);
    header->addWidget(label);
    header->addStretch();
    if (!caption.isEmpty()) {
        auto* hint = new QLabel(caption, parent);
        Ardoise::Theme::applyTypography(hint, Ardoise::TextRole::Caption);
        hint->setForegroundRole(QPalette::PlaceholderText);
        header->addWidget(hint);
    }
    return header;
}
