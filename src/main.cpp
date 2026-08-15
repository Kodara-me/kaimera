#include "MainWindow.h"
#if defined(Q_OS_MACOS)
#include "MacSystemAppearance.h"
#endif

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
#if defined(Q_OS_MACOS)
    installMacSystemAppearance(application);
#endif
    application.setOrganizationName(QStringLiteral("Kira Canvas"));
    application.setApplicationName(QStringLiteral("Kira Canvas"));

    MainWindow mainWindow;
    mainWindow.show();

    return application.exec();
}
