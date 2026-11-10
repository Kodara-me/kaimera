#pragma once
#include <QList>
#include <QString>
#include <QWidget>
#include <nukleus/ImageTypes.h>
class QDoubleSpinBox;
class QMenu;
class QPushButton;
class QVBoxLayout;

// The LoRAs of a generation: an "Add a LoRA" menu of the installed LoRAs not yet chosen, and a row per
// chosen LoRA with its weight and a button that removes it. The choice belongs to the New image card's
// draft; the card hands it to the request at Generate.
class LoraPicker final : public QWidget
{
    Q_OBJECT
public:
    static constexpr int MaximumLoras = 8;
    static constexpr double MinimumWeight = 0.0;
    static constexpr double MaximumWeight = 2.0;
    static constexpr double WeightStep = 0.05;
    static constexpr double DefaultWeight = 1.0;

    explicit LoraPicker(QWidget* parent = nullptr);
    // The installed LoRAs: the companions among models whose subtype is "lora". A chosen LoRA that is
    // no longer listed leaves the choice.
    void setModels(const QList<Nukleus::ModelInfo>& companions);
    // Whether the service generates with LoRAs (SDK canGenerateWithLoras()).
    void setSupported(bool supported);
    // Why the control is disabled, or empty when it is enabled.
    QString issue() const;
    // The chosen LoRAs in the order they were added, each with its weight set.
    QList<Nukleus::LoraWeight> chosen() const;
    void clear();
signals:
    void choiceChanged();

private:
    struct Row
    {
        QString id;
        QString label;
        QWidget* widget;
        QDoubleSpinBox* weight;
    };
    void add(const QString& id);
    void remove(const QString& id);
    void refresh();
    QList<Nukleus::ModelInfo> m_installed;
    QList<Row> m_rows;
    bool m_supported = false;
    QPushButton* m_add;
    QMenu* m_menu;
    QVBoxLayout* m_rowsLayout;
};
