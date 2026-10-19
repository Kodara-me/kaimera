#include "ServicePreferencesDialog.h"

#include "CanvasServiceController.h"
#include "OutputSettingsWidget.h"
#include <nukleus/ServiceProtocol.h>

#include <QDialogButtonBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>
#include <QFormLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>

#include <optional>

namespace
{
bool validServiceHost(const QString& host)
{
    if (host == QStringLiteral("localhost"))
        return true;
    QHostAddress address;
    return address.setAddress(host) && address.protocol() == QAbstractSocket::IPv4Protocol;
}

QUrl endpointForHost(const QString& host)
{
    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("ws"));
    endpoint.setHost(host);
    endpoint.setPort(Nukleus::Protocol::DefaultPort);
    return endpoint;
}
}

ServicePreferencesDialog::ServicePreferencesDialog(CanvasServiceController* client, QWidget* parent)
    : Ardoise::Dialog(parent)
    , m_client(client)
    , m_originalEndpoint(client->endpoint())
    , m_originalToken(client->token())
{
    setWindowTitle(tr("Service Preferences"));
    auto* content = new QWidget(this);
    auto* layout = new QFormLayout(content);
    setContentWidget(content);
    const QString host
        = client->endpoint().host().isEmpty() ? QStringLiteral("127.0.0.1") : client->endpoint().host();
    m_host = new QLineEdit(host, this);
    m_host->setObjectName(QStringLiteral("serviceHostInput"));
    m_host->setPlaceholderText(QStringLiteral("127.0.0.1"));
    layout->addRow(tr("Service IP"), m_host);
    m_token = new QLineEdit(client->token(), this);
    m_token->setEchoMode(QLineEdit::Password);
    layout->addRow(tr("Shared token"), m_token);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("serviceStatusLabel"));
    m_status->setWordWrap(true);
    layout->addRow(m_status);
    m_allowExplicit = new QCheckBox(tr("Allow models with explicit content"), content);
    m_allowExplicit->setObjectName(QStringLiteral("allowExplicitModels"));
    m_allowExplicit->setChecked(m_client->allowsExplicitModels());
    layout->addRow(m_allowExplicit);
    m_llm = new QComboBox(content);
    m_llm->setObjectName(QStringLiteral("defaultLlmModel"));
    layout->addRow(tr("Default LLM model"), m_llm);
    // Retain unavailable saved IDs during catalogue refreshes without selecting another model.
    m_llm->setProperty("selectedModelId", m_client->defaultLlmModel());
    connect(m_llm, &QComboBox::currentIndexChanged, this, [this] {
        m_llm->setProperty("selectedModelId", m_llm->currentData().toString());
    });
    refreshModels();
    connect(m_client, &CanvasServiceController::modelsChanged, this, &ServicePreferencesDialog::refreshModels);
    layout->addRow(new QLabel(tr("Output"), content));
    m_output = new OutputSettingsWidget(content);
    layout->addRow(m_output);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto* test = buttons->addButton(tr("Test Connection"), QDialogButtonBox::ActionRole);
    layout->addRow(buttons);
    const auto endpointFromInput = [this]() -> std::optional<QUrl>
    {
        const QString host = m_host->text().trimmed();
        if (!validServiceHost(host))
        {
            m_status->setText(tr("Enter an IPv4 address or localhost, without ws:// or a port."));
            return std::nullopt;
        }
        return endpointForHost(host);
    };
    connect(test, &QPushButton::clicked, this,
        [this, endpointFromInput]
        {
            const std::optional<QUrl> endpoint = endpointFromInput();
            if (!endpoint.has_value())
                return;
            m_client->setConfiguration(*endpoint, m_token->text(), false);
            m_client->connectToService();
        });
    connect(buttons, &QDialogButtonBox::accepted, this,
        [this]
        {
            const QString host = m_host->text().trimmed();
            if (!validServiceHost(host))
            {
                m_status->setText(tr("Enter an IPv4 address or localhost, without ws:// or a port."));
                return;
            }
            const QString llm = m_llm->currentData().toString();
            m_client->setConfiguration(endpointForHost(host), m_token->text());
            m_output->savePreferences();
            m_client->setAllowsExplicitModels(m_allowExplicit->isChecked());
            m_client->setDefaultLlmModel(llm);
            accept();
        });
    connect(buttons, &QDialogButtonBox::rejected, this, &ServicePreferencesDialog::reject);
    m_status->setMinimumHeight(fontMetrics().lineSpacing() * 4);
    setSizeGripEnabled(false);
    setFixedSize(sizeHint().expandedTo(QSize(460, 200)));
    connect(m_client, &CanvasServiceController::serviceStateChanged, this,
        [this](const QString&, const QString& message) { m_status->setText(message); });
}

void ServicePreferencesDialog::reject()
{
    m_client->setConfiguration(m_originalEndpoint, m_originalToken, false);
    Ardoise::Dialog::reject();
}

void ServicePreferencesDialog::refreshModels()
{
    const auto populate = [this](QComboBox* combo, Nukleus::ModelCategory category) {
        const QString selected = combo->property("selectedModelId").toString();
        const QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItem(tr("None"), QString());
        for (const auto& model : m_client->models(category))
            combo->addItem(model.displayLabel(), model.id);
        const int index = combo->findData(selected);
        combo->setCurrentIndex(index < 0 ? 0 : index);
    };
    populate(m_llm, Nukleus::ModelCategory::LlmChat);
}
