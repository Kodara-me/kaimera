#pragma once
#include <QWidget>
#include <nukleus/ImageTypes.h>
#include <nukleus/ModelRouting.h>
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QToolButton;
class ResolutionPicker;
class GenerationParametersWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit GenerationParametersWidget(QWidget *parent = nullptr);
    void setModels(const QList<Nukleus::ModelInfo> &models);
    QString modelId() const;
    QString modelName() const;
    QSize resolution() const;
    // The selected resolution's target ratio, such as "16:9".
    QString resolutionRatio() const;
    // The size offered for a target ratio ("16:9"), or an invalid size when none is.
    QSize resolutionForRatio(const QString &ratio) const;
    int steps() const;
    double cfgScale() const;
    int seed() const;
    Nukleus::ImageGenerationRequest generationRequest() const;
    void setRouting(const std::optional<Nukleus::ModelRouting> &routing, bool supported);
    bool routingAvailable() const;
    // Why the selected preset cannot generate, or empty when it can.
    QString routingIssue() const;
    // The service's presets, and why one of them cannot generate (empty when it can).
    QList<Nukleus::GenerationPreset> presets() const;
    QString presetIssue(const QString &id) const;
    // Whether the preset's model uses reference images (it can edit); a preset without an
    // available model is not reported here, its issue already says why.
    bool presetUsesReferences(const QString &id) const;
    void setRoutingLocked(bool locked);
    void resetDefaults();
    // Selects a preset by opaque ID or a resolution by target ratio ("16:9"); false when absent.
    bool selectPreset(const QString &id);
    bool selectRatio(const QString &ratio);
    // Hides Steps, CFG and Seed, which then follow the selected preset and stay
    // random. Without the resolution picker, the hidden resolution follows the
    // mapped model.
    void showGuidedControls(bool showResolution = true);
    // The selected service preset, or null without routing.
    const Nukleus::GenerationPreset *selectedPreset() const;
signals:
    void parametersChanged();

private:
    void applyModelRecommendations(bool preserveOverrides);
    void resolveModel();
    ResolutionPicker *m_resolutionPicker;
    std::optional<Nukleus::ModelRouting> m_routing;
    bool m_routed = false;
    bool m_locked = false;
    QString m_resolvedModel;
    QString m_resolvedPreset;
    QString m_selectedPreset;
    QString m_routingIssue;
    bool m_stepsOverridden = false;
    bool m_cfgOverridden = false;
    bool m_resolutionOverridden = false;
    QComboBox *m_model;
    QComboBox *m_preset;
    QComboBox *m_resolution;
    QSpinBox *m_steps;
    QDoubleSpinBox *m_cfg;
    QSpinBox *m_seed;
};
