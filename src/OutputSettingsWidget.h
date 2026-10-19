#pragma once
#include <QWidget>
class QComboBox;
class QLineEdit;
class OutputSettingsWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit OutputSettingsWidget(QWidget* parent = nullptr);
    QByteArray format() const;
    QString directory() const;
    void resetDefaults();
    void savePreferences() const;
    static QByteArray preferredFormat();
    static QString preferredDirectory();

private:
    QComboBox* m_format;
    QLineEdit* m_directory;
};
