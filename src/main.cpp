#include "MainWindow.h"

#include <QApplication>


int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("Kira Canvas"));
    application.setApplicationName(QStringLiteral("Kira Canvas"));

    MainWindow mainWindow;
    mainWindow.show();

    return application.exec();
}
