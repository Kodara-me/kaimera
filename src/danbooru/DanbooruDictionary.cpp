#include "DanbooruDictionary.h"
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>

namespace
{
// Splits one CSV line; quoted fields may hold commas and doubled quotes.
QStringList csvFields(const QString& line)
{
    QStringList fields;
    QString field;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quoted) {
            if (c == QLatin1Char('"') && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) { field += c; ++i; }
            else if (c == QLatin1Char('"')) quoted = false;
            else field += c;
        } else if (c == QLatin1Char('"')) {
            quoted = true;
        } else if (c == QLatin1Char(',')) {
            fields.append(field);
            field.clear();
        } else {
            field += c;
        }
    }
    fields.append(field);
    return fields;
}
}

bool DanbooruDictionary::load(const QString& path, qint64 minimumPosts, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    QList<Tag> tags;
    QList<QStringList> aliases;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QStringList fields = csvFields(stream.readLine());
        if (fields.size() < 3 || fields.at(0).trimmed().isEmpty()) continue;
        bool category = false, posts = false;
        Tag tag{fields.at(0).trimmed(), fields.at(1).toInt(&category), fields.at(2).toLongLong(&posts)};
        if (!category || !posts) continue; // A header or a malformed line.
        if (tag.posts < minimumPosts) continue;
        tags.append(tag);
        aliases.append(fields.size() > 3 ? fields.at(3).split(QLatin1Char(','), Qt::SkipEmptyParts) : QStringList());
    }
    if (tags.isEmpty()) {
        if (error) *error = QStringLiteral("No Danbooru tag in %1").arg(path);
        return false;
    }
    QHash<QString, qsizetype> index;
    for (qsizetype i = 0; i < tags.size(); ++i) index.insert(key(tags.at(i).name), i);
    // Aliases never shadow a real tag. "/lh"-style entries are autocomplete shortcuts, not aliases.
    for (qsizetype i = 0; i < tags.size(); ++i)
        for (const QString& alias : aliases.at(i))
            if (!alias.startsWith(QLatin1Char('/')) && !index.contains(key(alias))) index.insert(key(alias), i);
    QHash<QString, QList<qsizetype>> wordIndex;
    for (qsizetype i = 0; i < tags.size(); ++i)
        for (const QString& word : words(tags.at(i).name)) wordIndex[word].append(i);
    m_tags = std::move(tags);
    m_index = std::move(index);
    m_words = std::move(wordIndex);
    return true;
}

QList<const DanbooruDictionary::Tag*> DanbooruDictionary::suggestions(const QString& tag, int limit, int category) const
{
    QHash<qsizetype, int> shared; // Candidate → words in common.
    for (const QString& word : words(tag))
        for (const qsizetype i : m_words.value(word))
            if (m_tags.at(i).category == category) ++shared[i];
    QList<qsizetype> candidates = shared.keys();
    std::sort(candidates.begin(), candidates.end(), [&](qsizetype a, qsizetype b) {
        const int sa = shared.value(a), sb = shared.value(b);
        return sa != sb ? sa > sb : m_tags.at(a).posts > m_tags.at(b).posts;
    });
    QList<const Tag*> result;
    for (qsizetype i = 0; i < candidates.size() && result.size() < limit; ++i)
        result.append(&m_tags.at(candidates.at(i)));
    return result;
}

const DanbooruDictionary::Tag* DanbooruDictionary::find(const QString& tag) const
{
    const auto it = m_index.constFind(key(tag));
    return it == m_index.cend() ? nullptr : &m_tags.at(*it);
}

DanbooruDictionary::Result DanbooruDictionary::normalize(const QString& prompt) const
{
    // "(tag:1.2)" or "(tag)" around a tag; an escaped "\(" belongs to the tag itself.
    static const QRegularExpression weighted(QStringLiteral("^\\((.+?)(:\\s*\\d+(?:\\.\\d+)?)?\\)$"));
    Result result;
    QStringList written;
    for (const QString& part : prompt.split(QLatin1Char(','))) {
        const QString token = part.trimmed();
        if (token.isEmpty()) continue;
        const auto match = weighted.match(token);
        const QString inner = match.hasMatch() ? match.captured(1).trimmed() : token;
        const Tag* tag = find(inner);
        if (!tag) {
            result.unknown.append(inner);
            written.append(token);
            continue;
        }
        const QString form = promptForm(tag->name);
        written.append(match.hasMatch() ? QStringLiteral("(%1%2)").arg(form, match.captured(2)) : form);
    }
    result.prompt = written.join(QStringLiteral(", "));
    return result;
}

QString DanbooruDictionary::promptForm(const QString& name)
{
    QString form = name;
    form.replace(QLatin1Char('_'), QLatin1Char(' '));
    form.replace(QLatin1Char('('), QStringLiteral("\\("));
    form.replace(QLatin1Char(')'), QStringLiteral("\\)"));
    return form;
}

QStringList DanbooruDictionary::words(const QString& tag)
{
    static const QStringList linking{"on", "of", "in", "at", "to", "the", "a", "an", "and", "with", "own", "by"};
    static const QRegularExpression separators(QStringLiteral("[_()\\-]+"));
    QStringList result;
    for (QString word : key(tag).split(separators, Qt::SkipEmptyParts)) {
        if (word.size() < 2 || linking.contains(word)) continue;
        if (word.size() > 3 && word.endsWith(QLatin1Char('s'))) word.chop(1); // "hands" → "hand".
        if (!result.contains(word)) result.append(word);
    }
    return result;
}

QString DanbooruDictionary::key(const QString& tag)
{
    QString key = tag.trimmed().toLower();
    key.remove(QLatin1Char('\\'));
    key.replace(QLatin1Char(' '), QLatin1Char('_'));
    static const QRegularExpression repeated(QStringLiteral("_+"));
    key.replace(repeated, QStringLiteral("_"));
    return key;
}
