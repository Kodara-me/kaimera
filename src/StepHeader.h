#pragma once
#include <QString>
class QHBoxLayout;
class QWidget;
// Header of a numbered step in a guided path: an accent disc with the step
// number, a bold title and an optional muted caption, followed by a stretch.
namespace StepHeader
{
QHBoxLayout* create(int number, const QString& title, const QString& caption, const QString& objectName,
    QWidget* parent);
}
