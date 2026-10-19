#pragma once
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// Danbooru tag dictionary read from a tagcomplete-format CSV (name,category,posts,"alias,alias").
// It brings prompt tags to their exact Danbooru spelling and reports the ones it does not know.
// Self-contained (QtCore only) so it can be copied into other projects: see README.md.
class DanbooruDictionary
{
public:
    // Danbooru tag categories as numbered in the CSV.
    enum Category { General = 0, Artist = 1, Copyright = 3, Character = 4, Meta = 5 };
    struct Tag
    {
        QString name; // Danbooru spelling, with underscores, for example "long_hair".
        int category = General;
        qint64 posts = 0; // Danbooru posts carrying the tag when the CSV was exported.
    };
    // A prompt with its tags in prompt form, and the tags the dictionary does not know.
    struct Result
    {
        QString prompt;
        QStringList unknown;
    };

    // Replaces the dictionary with the CSV at path, keeping only tags with at least minimumPosts
    // posts (rare tags are poorly learned and bloat memory); false (and error) when it cannot be
    // read or keeps no tag.
    bool load(const QString& path, qint64 minimumPosts = 0, QString* error = nullptr);
    bool isEmpty() const { return m_tags.isEmpty(); }
    qsizetype size() const { return m_tags.size(); }
    // The tag named or aliased by tag, in any spelling ("Long Hair", "long_hair", "longhair"), or null.
    const Tag* find(const QString& tag) const;
    // Up to limit existing tags of category closest to an unknown tag ("rock seat" → "on rock",
    // "sitting on rock"…): most words in common first, then most posts. Short linking words
    // ("on", "the", "own"…) and plural endings are ignored when comparing.
    QList<const Tag*> suggestions(const QString& tag, int limit = 4, int category = General) const;
    // Rewrites each comma-separated tag of prompt in its Danbooru prompt form, keeping attention
    // weights such as "(tag:1.2)"; unknown tags stay as written and are listed.
    Result normalize(const QString& prompt) const;
    // How a Danbooru name is written in a prompt: spaces for underscores, escaped parentheses
    // ("thea_(spy_kyoushitsu)" → "thea \(spy kyoushitsu\)").
    static QString promptForm(const QString& name);

private:
    // Lookup key of any spelling: lowercase, unescaped, underscores for spaces.
    static QString key(const QString& tag);
    // The comparable words of a tag for suggestions.
    static QStringList words(const QString& tag);
    QList<Tag> m_tags;
    QHash<QString, qsizetype> m_index; // Names and aliases → index in m_tags.
    QHash<QString, QList<qsizetype>> m_words; // Words of tag names → indexes in m_tags.
};
