#include "rotation.h"

#include <QRandomGenerator>
#include <algorithm>

#include "library.h"
#include "preset.h"

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
    return true;
}

int Rotation::matchCount() const {
    const auto &all = m_library.presets();
    return int(std::count_if(all.begin(), all.end(), [this](const PresetInfo &preset) { return matches(preset); }));
}

QList<int> Rotation::pool() const {
    QList<int> all, kept;
    for (int i = 0; i < m_library.presets().size(); ++i) {
        all << i;
        if (m_keep && filtering() && matches(m_library.presets()[i]))
            kept << i;
    }
    return kept.isEmpty() ? all : kept;
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
