#pragma once

#include <QIcon>
#include <QStyle>
#include <QWidget>

inline QIcon systemIcon(const QString &themeName, QStyle::StandardPixmap fallback, const QWidget *widget)
{
    QStyle *style = widget->style();
#if defined(Q_OS_MACOS)
    Q_UNUSED(themeName)
    return style->standardIcon(fallback);
#else
    return QIcon::fromTheme(themeName, style->standardIcon(fallback));
#endif
}
