#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class Library;
struct PresetInfo;

// Which presets the changes come from, and in what order: the browser's
// filters, kept to or not, and the rounds a shuffle goes in. It only
// chooses; showing the preset chosen is the backend's.
class Rotation {
public:
    explicit Rotation(const Library &library) : m_library(library) {}

    // The filters: a family of colors (empty for any) and features a preset
    // must all have. Kept to, the changes move among those that match. Each
    // setter keeps to what there is, and says whether anything changed.
    QString color() const { return m_color; }
    QStringList features() const { return m_features; }
    bool keep() const { return m_keep; }
    bool setColor(const QString &family);
    bool setFeatures(const QStringList &features);
    bool setKeep(bool keep);
    bool filtering() const { return !m_color.isEmpty() || !m_features.isEmpty(); }
    bool matches(const PresetInfo &preset) const;
    int matchCount() const;

    // Where the changes come from: the presets that match when the filters
    // are kept to and any do, otherwise all of them. In library order.
    QList<int> pool() const;
    // The next or the previous preset in the pool from the one named, which
    // may be outside it; -1 when there is none.
    int step(const QString &current, int direction) const;
    // A preset at random, from those the round has not shown yet, never the
    // one named; -1 when there is none.
    int random(const QString &current);
    // That a preset of the library came on, by whatever means, so the round
    // counts it as shown.
    void shown(const QString &name);

private:
    const Library &m_library;
    QString m_color;
    QStringList m_features;
    bool m_keep = false;
    // The library's presets shown in this round, in the order they were,
    // and the pool's round before. A random change shows every preset in
    // the pool once before any comes round again.
    QStringList m_round, m_lastRound;
};
