#include "MacSystemAppearance.h"

#import <AppKit/AppKit.h>

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QPointer>
#include <QStyleHints>

namespace
{
QColor qtColor(NSColor *color)
{
    NSColor *srgbColor = [color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    return QColor::fromRgbF(srgbColor.redComponent,
                            srgbColor.greenComponent,
                            srgbColor.blueComponent,
                            srgbColor.alphaComponent);
}

void setColor(QPalette &palette, QPalette::ColorRole role, NSColor *color)
{
    palette.setColor(role, qtColor(color));
}

void setColor(QPalette &palette, QPalette::ColorGroup group, QPalette::ColorRole role, NSColor *color)
{
    palette.setColor(group, role, qtColor(color));
}

void applyMacSystemAppearance(QApplication &application)
{
    QPalette palette = application.palette();
    setColor(palette, QPalette::Window, NSColor.windowBackgroundColor);
    setColor(palette, QPalette::Base, NSColor.textBackgroundColor);
    setColor(palette, QPalette::AlternateBase, NSColor.controlBackgroundColor);
    setColor(palette, QPalette::Button, NSColor.controlBackgroundColor);
    setColor(palette, QPalette::Light, NSColor.separatorColor);
    setColor(palette, QPalette::Mid, NSColor.separatorColor);
    setColor(palette, QPalette::Dark, NSColor.separatorColor);
    setColor(palette, QPalette::Text, NSColor.textColor);
    setColor(palette, QPalette::WindowText, NSColor.labelColor);
    setColor(palette, QPalette::ButtonText, NSColor.labelColor);
    setColor(palette, QPalette::PlaceholderText, NSColor.placeholderTextColor);
    setColor(palette, QPalette::Highlight, NSColor.controlAccentColor);
    setColor(palette, QPalette::HighlightedText, NSColor.selectedTextColor);
    setColor(palette, QPalette::ToolTipBase, NSColor.controlBackgroundColor);
    setColor(palette, QPalette::ToolTipText, NSColor.labelColor);
    setColor(palette, QPalette::Disabled, QPalette::Text, NSColor.disabledControlTextColor);
    setColor(palette, QPalette::Disabled, QPalette::WindowText, NSColor.disabledControlTextColor);
    setColor(palette, QPalette::Disabled, QPalette::ButtonText, NSColor.disabledControlTextColor);
    application.setPalette(palette);
}
}

void installMacSystemAppearance(QApplication &application)
{
    applyMacSystemAppearance(application);
    QObject::connect(QGuiApplication::styleHints(),
                     &QStyleHints::colorSchemeChanged,
                     &application,
                     [&application](Qt::ColorScheme) { applyMacSystemAppearance(application); });

    const QPointer<QApplication> guardedApplication(&application);
    auto *notificationCenter = NSNotificationCenter.defaultCenter;
    void (^refreshAppearance)(NSNotification *) = ^(NSNotification *) {
        if (guardedApplication != nullptr) {
            applyMacSystemAppearance(*guardedApplication);
        }
    };
    [notificationCenter addObserverForName:NSSystemColorsDidChangeNotification
                                    object:nil
                                     queue:NSOperationQueue.mainQueue
                                usingBlock:refreshAppearance];
}
