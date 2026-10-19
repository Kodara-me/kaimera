#pragma once
#include <QImage>
#include <QWidget>
class QHBoxLayout;
class QLabel;
// Numbered, removable thumbnails of up to four (by default) in-memory reference images, followed
// by a count (which can be hidden); hidden while empty.
class ReferenceImageStrip final : public QWidget
{
    Q_OBJECT
public:
    static constexpr int MaximumReferences = 4;
    static constexpr int ThumbnailSize = 44;
    // removeButtonName names each thumbnail's remove button; without one the strip is
    // read-only, with no remove buttons.
    ReferenceImageStrip(const QString& removeButtonName, QWidget* parent);
    QList<QImage> references() const { return m_references; }
    // The default maximum is MaximumReferences; Kira Sparkle's chat allows more.
    void setMaximum(int maximum) { m_maximum = maximum; }
    int maximum() const { return m_maximum; }
    bool isFull() const { return m_references.size() >= m_maximum; }
    bool addReference(const QImage& image);
    // Adds all the images, or none when one is null or they do not all fit.
    bool addReferences(const QList<QImage>& images);
    void removeReference(int index);
    void clearReferences();
    void setCountVisible(bool visible);
    // Shows the images as base images: an accent border instead of a number.
    void setBase(bool base) { m_base = base; }
    // Dims the thumbnails with a warning border and explains with reason why they are
    // ignored; an empty reason shows them normally.
    void setIgnored(const QString& reason);
    QString ignoredReason() const { return m_ignored; }
signals:
    void referencesChanged();

private:
    void updateReferences();
    void showIgnored();
    QString m_removeButtonName;
    QHBoxLayout* m_layout;
    QLabel* m_count;
    QList<QImage> m_references;
    int m_maximum = MaximumReferences;
    bool m_base = false;
    QString m_ignored;
};
