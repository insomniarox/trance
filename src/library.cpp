#include "library.h"

#include "preset.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

Library::Library(const QString &userDirectory)
    : m_userDirectory(userDirectory.isEmpty()
                          ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/trance/presets"
                          : userDirectory) {
    reload();
}

void Library::reload() {
    m_presets.clear();
    auto scan = [&](const QString &directory, bool builtin) {
        const QDir dir(directory);
        for (const auto &file : dir.entryList({"*.json"}, QDir::Files, QDir::Name)) {
            PresetInfo info;
            info.path = dir.filePath(file);
            info.builtin = builtin;
            // A file that doesn't parse still shows up, under its file name,
            // so it can be opened and fixed rather than silently vanish.
            const QJsonObject preset = readFile(info.path, nullptr);
            info.name = preset.value("name").toString().trimmed();
            if (info.name.isEmpty())
                info.name = QFileInfo(file).completeBaseName();
            info.author = preset.value("author").toString();
            info.family = presets::colorFamily(presets::presetPalette(preset));
            info.features = presets::features(preset);
            const int existing = indexOf(info.name);
            if (existing >= 0) {
                info.shadowsBuiltin = m_presets[existing].builtin;
                m_presets[existing] = info;
            } else {
                m_presets.append(info);
            }
        }
    };
    scan(":/presets", true);
    scan(m_userDirectory, false);
    std::sort(m_presets.begin(), m_presets.end(), [](const PresetInfo &a, const PresetInfo &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
}

int Library::indexOf(const QString &name) const {
    for (int i = 0; i < m_presets.size(); ++i)
        if (m_presets[i].name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

QJsonObject Library::readFile(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QString("Could not read %1").arg(path);
        return {};
    }
    QJsonParseError parseError;
    const QByteArray text = file.readAll();
    const QJsonDocument document = QJsonDocument::fromJson(text, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            const int line = int(text.left(parseError.offset).count('\n')) + 1;
            *error = parseError.error != QJsonParseError::NoError
                         ? QString("%1 (line %2)").arg(parseError.errorString()).arg(line)
                         : QString("A preset is a JSON object");
        }
        return {};
    }
    return document.object();
}

QJsonObject Library::load(int index, QString *error) const {
    if (index < 0 || index >= m_presets.size()) {
        if (error)
            *error = "No such preset";
        return {};
    }
    return readFile(m_presets[index].path, error);
}

QJsonObject Library::find(const QString &nameOrPath, QString *error) const {
    const int index = indexOf(nameOrPath);
    if (index >= 0)
        return load(index, error);
    if (QFileInfo(nameOrPath).isFile())
        return readFile(nameOrPath, error);
    if (error)
        *error = QString("No preset or file named '%1'").arg(nameOrPath);
    return {};
}

QString Library::slug(const QString &name) {
    QString result;
    for (const QChar c : name.trimmed().toLower()) {
        if (c.isLetterOrNumber())
            result += c;
        else if (!result.isEmpty() && !result.endsWith('-'))
            result += '-';
    }
    while (result.endsWith('-'))
        result.chop(1);
    return result.isEmpty() ? QString("preset") : result;
}

QString Library::save(const QJsonObject &preset, QString *error) {
    const QString name = preset.value("name").toString().trimmed();
    if (name.isEmpty()) {
        if (error)
            *error = "A preset needs a name";
        return {};
    }
    // Saving over the user's own preset keeps its file; anything else,
    // a changed built-in included, becomes a new file in their folder.
    const int existing = indexOf(name);
    QString path;
    if (existing >= 0 && !m_presets[existing].builtin) {
        path = m_presets[existing].path;
    } else {
        // Two names can come down to the same file name ("Night!" and
        // "Night?"). The newcomer takes another one rather than write over
        // a preset that isn't it.
        const QString base = m_userDirectory + "/" + slug(name);
        path = base + ".json";
        for (int n = 2; QFileInfo::exists(path); ++n)
            path = base + "-" + QString::number(n) + ".json";
    }
    QSaveFile file(path);
    const QByteArray text = QJsonDocument(preset).toJson(QJsonDocument::Indented);
    if (!QDir().mkpath(m_userDirectory) || !file.open(QIODevice::WriteOnly) || file.write(text) != text.size() ||
        !file.commit()) {
        if (error)
            *error = QString("Could not write %1").arg(path);
        return {};
    }
    reload();
    return path;
}

bool Library::remove(const QString &name, QString *error) {
    const int index = indexOf(name);
    if (index < 0 || m_presets[index].builtin) {
        if (error)
            *error = index < 0 ? QString("No such preset") : QString("Built-in presets can't be deleted");
        return false;
    }
    if (!QFile::remove(m_presets[index].path)) {
        if (error)
            *error = QString("Could not delete %1").arg(m_presets[index].path);
        return false;
    }
    reload();
    return true;
}

QList<QJsonObject> Library::visitors() {
    QList<QJsonObject> list;
    const QDir directory(":/visitors");
    for (const auto &file : directory.entryList({"*.json"}, QDir::Files, QDir::Name)) {
        const QJsonObject visitor = readFile(directory.filePath(file), nullptr);
        if (!visitor.isEmpty())
            list.append(visitor);
    }
    return list;
}
