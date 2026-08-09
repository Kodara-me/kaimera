#include "CuratedResourceManager.h"
#include "MainWindow.h"
#include "StartupPreparationWidget.h"

#include <QApplication>

#include <algorithm>
#include <memory>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("Kira Canvas"));
    application.setApplicationName(QStringLiteral("Kira Canvas"));

    CuratedResourceManager resourceManager;
    const QList<CuratedResourceId> requiredResources = resourceManager.requiredAtStartupResourceIds();
    const bool requiresPreparation = std::any_of(requiredResources.cbegin(), requiredResources.cend(), [&resourceManager](CuratedResourceId id) {
        return !resourceManager.isInstalled(id);
    });

    std::unique_ptr<MainWindow> mainWindow;
    if (!requiresPreparation) {
        mainWindow = std::make_unique<MainWindow>(resourceManager);
        mainWindow->show();
    } else {
        auto *preparationWidget = new StartupPreparationWidget(&resourceManager);
        QObject::connect(preparationWidget, &StartupPreparationWidget::preparationFinished, &application, [&mainWindow, &resourceManager, preparationWidget] {
            mainWindow = std::make_unique<MainWindow>(resourceManager);
            mainWindow->show();
            preparationWidget->close();
            preparationWidget->deleteLater();
        });
        preparationWidget->show();
        preparationWidget->beginPreparation();
    }

    return application.exec();
}
