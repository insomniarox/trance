#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class Library;
struct PresetInfo;

// Which presets the changes come from, and in what order: the browser's
// filters, kept to or not, the user's lists of presets, and the rounds a
// shuffle goes in. It only chooses; showing the preset chosen is the
// backend's.
class Rotation {
public:
    // One of the user's lists: presets by name, in the order they were put
    // in. A name may be of a preset that is gone, and comes back with it.
    struct List {
        QString name;
        QStringList presets;
    };
    // The lists are kept in that file, which need not be there yet.
    explicit Rotation(const Library &library, const QString &listsFile = {});

    // The filters: a family of colors (empty for any), features a preset
    // must all have, and a list it must be in (empty for none). Kept to, the
    // changes move among those that match. Each setter keeps to what there
    // is, and says whether anything changed.
    QString color() const { return m_color; }
    QStringList features() const { return m_features; }
    QString list() const { return m_list; }
    bool keep() const { return m_keep; }
    bool setColor(const QString &family);
    bool setFeatures(const QStringList &features);
    bool setList(const QString &name);
    bool setKeep(bool keep);
    bool filtering() const { return !m_color.isEmpty() || !m_features.isEmpty() || !m_list.isEmpty(); }
    bool matches(const PresetInfo &preset) const;
    int matchCount() const;

    // Where the changes come from: the presets that match when the filters
    // are kept to and any do, otherwise all of them; either way leaving out
    // the skipped, unless that would leave none. In library order.
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

    // The lists, Favorites and Skipped first, which are always there.
    // Changes to them are written at once; false with a message when that,
    // or the change, can't be done.
    static QString favorites();
    // The presets in it never come by themselves: not on a change, nor in a
    // mash-up, unless the changes are kept to Skipped itself.
    static QString skipped();
    const QList<List> &lists() const { return m_lists; }
    // The lists a preset is in.
    QStringList listsOf(const QString &preset) const;
    bool contains(const QString &list, const QString &preset) const;
    // Puts the preset in the list, or takes it out if it is in already.
    bool toggle(const QString &list, const QString &preset, QString *error);
    bool addList(const QString &name, QString *error);
    bool renameList(const QString &from, const QString &to, QString *error);
    bool removeList(const QString &name, QString *error);
    // A preset saved under a new name keeps its place in every list; one
    // deleted for good leaves them.
    void renamed(const QString &from, const QString &to);
    void removed(const QString &preset);

private:
    int indexOfList(const QString &name) const;
    bool skipping(const PresetInfo &preset) const;
    bool write(QString *error);

    const Library &m_library;
    QString m_listsFile;
    QList<List> m_lists;
    QString m_color, m_list;
    QStringList m_features;
    bool m_keep = false;
    bool m_unreadable = false; // the file is there but isn't a list of lists
    // The library's presets shown in this round, in the order they were,
    // and the pool's round before. A random change shows every preset in
    // the pool once before any comes round again.
    QStringList m_round, m_lastRound;
};
