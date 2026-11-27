#include "rotation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <algorithm>

#include "library.h"
#include "preset.h"

namespace {
bool same(const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) == 0; }
} // namespace

// The file is {"lists": [{"name": ..., "presets": [...]}, ...]}. One that is
// there but can't be read is never written over, so nothing in it is lost.
Rotation::Rotation(const Library &library, const QString &listsFile)
    : m_library(library), m_listsFile(listsFile) {
    QFile file(listsFile);
    if (!listsFile.isEmpty() && file.exists()) {
        const QJsonDocument document = QJsonDocument::fromJson(file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray());
        m_unreadable = !document.isObject();
        for (const auto &value : document.object().value("lists").toArray()) {
            const QJsonObject object = value.toObject();
            List list{object.value("name").toString().trimmed(), {}};
            if (list.name.isEmpty() || indexOfList(list.name) >= 0)
                continue;
            for (const auto &name : object.value("presets").toArray())
                if (!name.toString().trimmed().isEmpty() && !list.presets.contains(name.toString().trimmed(), Qt::CaseInsensitive))
                    list.presets << name.toString().trimmed();
            m_lists << list;
        }
    }
    // Favorites first and Skipped second, whatever the file had.
    const QStringList fixed{favorites(), skipped()};
    for (int place = 0; place < fixed.size(); ++place) {
        const int found = indexOfList(fixed[place]);
        if (found < 0)
            m_lists.insert(place, List{fixed[place], {}});
        else
            m_lists.move(found, place);
    }
}

QString Rotation::favorites() { return QStringLiteral("Favorites"); }
QString Rotation::skipped() { return QStringLiteral("Skipped"); }

int Rotation::indexOfList(const QString &name) const {
    for (int i = 0; i < m_lists.size(); ++i)
        if (same(m_lists[i].name, name.trimmed()))
            return i;
    return -1;
}

QStringList Rotation::listsOf(const QString &preset) const {
    QStringList names;
    for (const List &list : m_lists)
        if (list.presets.contains(preset.trimmed(), Qt::CaseInsensitive))
            names << list.name;
    return names;
}

bool Rotation::contains(const QString &list, const QString &preset) const {
    const int index = indexOfList(list);
    return index >= 0 && m_lists[index].presets.contains(preset.trimmed(), Qt::CaseInsensitive);
}

bool Rotation::toggle(const QString &list, const QString &preset, QString *error) {
    const int index = indexOfList(list);
    const int found = m_library.indexOf(preset);
    if (index < 0 || found < 0) {
        if (error)
            *error = index < 0 ? QString("There is no list called %1").arg(list)
                               : QString("Only saved presets can go in a list");
        return false;
    }
    const QString name = m_library.presets()[found].name;
    QStringList &presets = m_lists[index].presets;
    if (!presets.removeIf([&](const QString &in) { return same(in, name); }))
        presets << name;
    return write(error);
}

bool Rotation::addList(const QString &name, QString *error) {
    const QString wanted = name.trimmed();
    if (wanted.isEmpty() || indexOfList(wanted) >= 0) {
        if (error)
            *error = wanted.isEmpty() ? QString("A list needs a name") : QString("There is already a list called %1").arg(wanted);
        return false;
    }
    m_lists << List{wanted, {}};
    return write(error);
}

bool Rotation::renameList(const QString &from, const QString &to, QString *error) {
    const int index = indexOfList(from);
    const QString wanted = to.trimmed();
    QString problem;
    if (index < 0)
        problem = QString("There is no list called %1").arg(from);
    else if (index <= 1)
        problem = QString("%1 keeps its name").arg(m_lists[index].name);
    else if (wanted.isEmpty())
        problem = QString("A list needs a name");
    else if (indexOfList(wanted) >= 0 && indexOfList(wanted) != index)
        problem = QString("There is already a list called %1").arg(wanted);
    if (!problem.isEmpty()) {
        if (error)
            *error = problem;
        return false;
    }
    if (same(m_list, m_lists[index].name))
        m_list = wanted;
    m_lists[index].name = wanted;
    return write(error);
}

bool Rotation::removeList(const QString &name, QString *error) {
    const int index = indexOfList(name);
    if (index <= 1) {
        if (error)
            *error = index < 0 ? QString("There is no list called %1").arg(name)
                               : QString("%1 is always there").arg(m_lists[index].name);
        return false;
    }
    if (same(m_list, m_lists[index].name))
        m_list.clear();
    m_lists.removeAt(index);
    return write(error);
}

void Rotation::renamed(const QString &from, const QString &to) {
    bool changed = false;
    for (List &list : m_lists)
        for (QString &name : list.presets)
            if (same(name, from)) {
                name = to.trimmed();
                changed = true;
            }
    if (changed)
        write(nullptr);
}

void Rotation::removed(const QString &preset) {
    // A built-in a copy of the user's stood in for is back under the name.
    if (m_library.indexOf(preset) >= 0)
        return;
    bool changed = false;
    for (List &list : m_lists)
        changed = list.presets.removeIf([&](const QString &name) { return same(name, preset); }) > 0 || changed;
    if (changed)
        write(nullptr);
}

bool Rotation::write(QString *error) {
    if (m_listsFile.isEmpty())
        return true;
    if (m_unreadable) {
        if (error)
            *error = QString("%1 could not be read, so it is left as it is: mend or remove it").arg(m_listsFile);
        return false;
    }
    QJsonArray lists;
    for (const List &list : m_lists)
        lists.append(QJsonObject{{"name", list.name}, {"presets", QJsonArray::fromStringList(list.presets)}});
    const QByteArray text = QJsonDocument(QJsonObject{{"lists", lists}}).toJson(QJsonDocument::Indented);
    QSaveFile file(m_listsFile);
    if (!QDir().mkpath(QFileInfo(m_listsFile).absolutePath()) || !file.open(QIODevice::WriteOnly) ||
        file.write(text) != text.size() || !file.commit()) {
        if (error)
            *error = QString("Could not write %1").arg(m_listsFile);
        return false;
    }
    return true;
}

bool Rotation::setColor(const QString &family) {
    const QString wanted = presets::colorFamilies().contains(family) ? family : QString();
    if (wanted == m_color)
        return false;
    m_color = wanted;
    return true;
}

bool Rotation::setFeatures(const QStringList &features) {
    QStringList wanted;
    for (const QString &feature : presets::featureNames())
        if (features.contains(feature))
            wanted << feature;
    if (wanted == m_features)
        return false;
    m_features = wanted;
    return true;
}

bool Rotation::setList(const QString &name) {
    const int index = name.trimmed().isEmpty() ? -1 : indexOfList(name);
    const QString wanted = index >= 0 ? m_lists[index].name : QString();
    if (wanted == m_list)
        return false;
    m_list = wanted;
    return true;
}

bool Rotation::setKeep(bool keep) {
    if (keep == m_keep)
        return false;
    m_keep = keep;
    return true;
}

bool Rotation::matches(const PresetInfo &preset) const {
    if (!m_color.isEmpty() && preset.family != m_color)
        return false;
    for (const QString &feature : m_features)
        if (!preset.features.contains(feature))
            return false;
    return m_list.isEmpty() || contains(m_list, preset.name);
}

int Rotation::matchCount() const {
    const auto &all = m_library.presets();
    return int(std::count_if(all.begin(), all.end(), [this](const PresetInfo &preset) { return matches(preset); }));
}

// Kept to Skipped itself, the skipped are what the changes are for.
bool Rotation::skipping(const PresetInfo &preset) const {
    return !(m_keep && same(m_list, skipped())) && contains(skipped(), preset.name);
}

QList<int> Rotation::pool() const {
    QList<int> all, left, kept;
    for (int i = 0; i < m_library.presets().size(); ++i) {
        const PresetInfo &preset = m_library.presets()[i];
        all << i;
        if (skipping(preset))
            continue;
        left << i;
        if (m_keep && filtering() && matches(preset))
            kept << i;
    }
    return !kept.isEmpty() ? kept : !left.isEmpty() ? left : all;
}

// From wherever the one named is in the whole list, so leaving the pool
// comes straight back.
int Rotation::step(const QString &current, int direction) const {
    const QList<int> from = pool();
    if (from.isEmpty())
        return -1;
    const int at = m_library.indexOf(current);
    if (direction > 0) {
        for (int index : from)
            if (index > at)
                return index;
        return from.first();
    }
    for (auto it = from.rbegin(); it != from.rend(); ++it)
        if (at < 0 || *it < at)
            return *it;
    return from.last();
}

int Rotation::random(const QString &current) {
    const QList<int> all = pool();
    const auto &presets = m_library.presets();
    const auto without = [&](const QStringList &names) {
        QList<int> left;
        for (const int index : all)
            if (!names.contains(presets[index].name) && presets[index].name != current)
                left << index;
        return left;
    };
    // Of those the round has shown, the ones in the pool, in the order they
    // were. Kept to a filter, the round is of the presets that match.
    QStringList shown;
    for (const QString &name : m_round)
        if (all.contains(m_library.indexOf(name)))
            shown << name;
    QList<int> from = without(m_round);
    // Every one has been shown: the next round begins. What the rest of the
    // library has shown, left out by a filter, is kept for when it is let go
    // of; any deleted since are forgotten.
    if (from.isEmpty()) {
        QStringList rest;
        for (const QString &name : m_round)
            if (!shown.contains(name) && m_library.indexOf(name) >= 0)
                rest << name;
        m_round = rest;
        m_lastRound = shown;
        shown.clear();
        from = without(m_round);
    }
    // The first quarter of a round leaves out the last quarter of the one
    // before, so that a preset shown near the end of one doesn't come back
    // near the start of the next. Only the first quarter, or the two halves
    // would keep to their halves round after round.
    const int gap = int(all.size() / 4);
    if (shown.size() <= gap) {
        const QList<int> rested = without(m_round + m_lastRound.mid(qMax(0, int(m_lastRound.size()) - gap)));
        if (!rested.isEmpty())
            from = rested;
    }
    // The only preset there is.
    if (from.isEmpty())
        from = all;
    return from.isEmpty() ? -1 : from.at(QRandomGenerator::global()->bounded(int(from.size())));
}

void Rotation::shown(const QString &name) {
    // Only the library's own presets are shown in a round: a mash-up, or a
    // file opened from elsewhere, is none of them.
    if (const int index = m_library.indexOf(name); index >= 0 && !m_round.contains(m_library.presets()[index].name))
        m_round << m_library.presets()[index].name;
}
