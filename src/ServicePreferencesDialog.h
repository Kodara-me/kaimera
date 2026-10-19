#pragma once

#include <Ardoise/Dialog.h>
#include <QUrl>

class QLineEdit;
class QCheckBox;
class QComboBox;
class QLabel;
class CanvasServiceController;
class OutputSettingsWidget;

class ServicePreferencesDialog final : public Ardoise::Dialog
{
    Q_OBJECT
public:
    explicit ServicePreferencesDialog(CanvasServiceController* client, QWidget* parent = nullptr);

public slots:
    void reject() override;

private:
    CanvasServiceController* m_client;
    void refreshModels();
    QComboBox* m_llm;
    QCheckBox* m_allowExplicit;
    QLineEdit* m_host;
    QLineEdit* m_token;
    QLabel* m_status;
    OutputSettingsWidget* m_output;
    QUrl m_originalEndpoint;
    QString m_originalToken;
};
