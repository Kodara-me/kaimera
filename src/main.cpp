#include "LegacySettings.h"
#include "MainWindow.h"
#include <Ardoise/Theme.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    Ardoise::Theme::apply(application);
    const QString legacyName = QStringLiteral("Kira Canvas");
    application.setOrganizationName(legacyName);
    application.setApplicationName(legacyName);
    const QString legacyDataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    application.setOrganizationName(QStringLiteral("Kaimera"));
    application.setApplicationName(QStringLiteral("Kaimera"));
    application.setApplicationVersion(QStringLiteral(KAIMERA_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Create and edit images with AI models."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(application);
    {
        const QSettings legacy(legacyName, legacyName);
        QSettings current;
        migrateLegacySettings(legacy, current);
    }
    migrateLegacyDataFile(legacyDataDirectory,
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), QStringLiteral("danbooru.csv"));
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale::system(), QStringLiteral("qtbase"), QStringLiteral("_"),
            QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
    {
        application.installTranslator(&qtTranslator);
    }
    QTranslator translator;
    const auto catalog = QLocale::system().language() == QLocale::French
        ? QStringLiteral(":/translations/kaimera_fr.qm")
        : QStringLiteral(":/translations/kaimera_en.qm");
    if (translator.load(catalog))
    {
        application.installTranslator(&translator);
    }

    application.setDesktopFileName(QStringLiteral("me.kodara.kaimera"));
    QIcon icon;
    for (const int size : { 16, 32, 48, 64, 128, 256, 512, 1024 })
    {
        icon.addFile(QStringLiteral(":/app-icon/%1.png").arg(size), QSize(size, size));
    }
    application.setWindowIcon(icon);

    MainWindow mainWindow;
    mainWindow.show();

    return application.exec();
}
