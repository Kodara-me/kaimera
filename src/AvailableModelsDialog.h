#pragma once
#include <Ardoise/Dialog.h>
class CanvasServiceController;
class QLabel;
class QPushButton;
class QStandardItemModel;
class QTableWidget;
namespace Ardoise
{
class TreeView;
}

class AvailableModelsDialog final : public Ardoise::Dialog
{
    Q_OBJECT
  public:
    explicit AvailableModelsDialog(CanvasServiceController* client, QWidget* parent = nullptr);

  private:
    void populateModels();
    void showSelection();
    void updateConnection();
    CanvasServiceController* m_client;
    Ardoise::TreeView* m_tree;
    QStandardItemModel* m_model;
    QLabel* m_status;
    QLabel* m_details;
    QLabel* m_empty;
    QTableWidget* m_options;
    QPushButton* m_refresh;
};
