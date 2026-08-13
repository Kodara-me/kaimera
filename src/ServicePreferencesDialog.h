#pragma once

#include <QDialog>
#include <QUrl>

class QLineEdit;
class QLabel;
class ServiceClient;

class ServicePreferencesDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit ServicePreferencesDialog(ServiceClient *client, QWidget *parent = nullptr);
private:
    ServiceClient *m_client;
    QLineEdit *m_endpoint;
    QLineEdit *m_token;
    QLabel *m_status;
    QUrl m_originalEndpoint;
    QString m_originalToken;
};
