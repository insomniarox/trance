#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

struct PresetInfo {
    QString name;
    QString author;
    QString path;
    bool builtin = true;
    // A preset of the user's that stands in for a built-in of the same name.
    bool shadowsBuiltin = false;
    // Its colors' family and what it is made of, for the browser's filters:
    // see presets::colorFamily and presets::features.
    QString family;
    QStringList features;
};

// The presets on offer: those compiled into the binary and those in the
// user's own folder, which is the only place anything is ever written.
class Library {
public:
    explicit Library(const QString &userDirectory = {});

    void reload();
    const QList<PresetInfo> &presets() const { return m_presets; }
    int indexOf(const QString &name) const;
    QString userDirectory() const { return m_userDirectory; }

    QJsonObject load(int index, QString *error = nullptr) const;
    // A preset by name, or from a file when the argument is a path.
    QJsonObject find(const QString &nameOrPath, QString *error) const;
    static QJsonObject readFile(const QString &path, QString *error);

    // Writes the preset to the user's folder, atomically, and returns the
    // path; an empty string and a message when it can't.
    QString save(const QJsonObject &preset, QString *error);
    bool remove(const QString &name, QString *error);
    static QString slug(const QString &name);

    // The built-in visitors: small scenes that drop in over any preset.
    static QList<QJsonObject> visitors();

private:
    QString m_userDirectory;
    QList<PresetInfo> m_presets;
};
