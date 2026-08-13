#include "ServicePreferencesDialog.h"

#include "ServiceClient.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>

ServicePreferencesDialog::ServicePreferencesDialog(ServiceClient *client, QWidget *parent)
    : QDialog(parent), m_client(client), m_originalEndpoint(client->endpoint()), m_originalToken(client->token())
{
    setWindowTitle(tr("Service Preferences"));
    auto *layout = new QFormLayout(this);
    m_endpoint = new QLineEdit(client->endpoint().toString(), this); m_endpoint->setPlaceholderText(QStringLiteral("ws://server.local:9384")); layout->addRow(tr("Service address"), m_endpoint);
    m_token = new QLineEdit(client->token(), this); m_token->setEchoMode(QLineEdit::Password); layout->addRow(tr("Shared token"), m_token);
    m_status = new QLabel(this); m_status->setWordWrap(true); layout->addRow(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto *test = buttons->addButton(tr("Test Connection"), QDialogButtonBox::ActionRole); layout->addRow(buttons);
    connect(test, &QPushButton::clicked, this, [this] { m_client->setConfiguration(QUrl::fromUserInput(m_endpoint->text()), m_token->text(), false); m_client->connectToService(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { m_client->setConfiguration(QUrl::fromUserInput(m_endpoint->text()), m_token->text()); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, [this] { m_client->setConfiguration(m_originalEndpoint, m_originalToken, false); reject(); });
    connect(m_client, &ServiceClient::serviceStateChanged, this, [this](const QString &, const QString &message) { m_status->setText(message); });
}
