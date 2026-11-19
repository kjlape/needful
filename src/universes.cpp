#include "universes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

#include "backend.h"

QString Universe::label() const {
    if (name == Universes::Mine)
        return QObject::tr("Mine");
    return name.isEmpty() ? name : name.left(1).toUpper() + name.mid(1);
}

Universes::Universes(const QString &usualDataPath)
    : m_usual(QFileInfo(usualDataPath.isEmpty() ? Backend::defaultDataPath() : usualDataPath).absoluteFilePath()) {}

QString Universes::registryPath() const { return QFileInfo(m_usual).absolutePath() + "/universes.json"; }

bool Universes::validName(const QString &name) {
    static const QRegularExpression pattern("^[a-z0-9][a-z0-9-]{0,31}$");
    return pattern.match(name).hasMatch();
}

QList<Universe> Universes::others() const {
    QFile file(registryPath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject names = QJsonDocument::fromJson(file.readAll()).object().value("universes").toObject();
    QList<Universe> found;
    for (auto it = names.begin(); it != names.end(); ++it)
        if (validName(it.key()) && it.key() != Mine && !it.value().toString().isEmpty())
            found.append({it.key(), it.value().toString()});
    std::sort(found.begin(), found.end(), [](const Universe &a, const Universe &b) { return a.name < b.name; });
    return found;
}

QList<Universe> Universes::list() const { return QList<Universe>{{Mine, m_usual}} + others(); }

QString Universes::dataPath(const QString &name) const {
    for (const Universe &u : list())
        if (u.name == name)
            return u.dataPath;
    return {};
}

QString Universes::nameOf(const QString &dataPath) const {
    const QString wanted = QFileInfo(dataPath).absoluteFilePath();
    for (const Universe &u : list())
        if (QFileInfo(u.dataPath).absoluteFilePath() == wanted)
            return u.name;
    return {};
}

QString Universes::add(const QString &name, QString *error) {
    if (!validName(name) || name == Mine) {
        if (error)
            *error = name == Mine ? QString("\"mine\" is always this computer's own list.")
                                  : QString("Name a universe with lower-case letters, digits, and hyphens, like hers.");
        return {};
    }
    QList<Universe> list = others();
    for (const Universe &u : list)
        if (u.name == name)
            return u.dataPath; // Already here.
    const QString path = QFileInfo(registryPath()).absolutePath() + "/universes/" + name + ".json";
    list.append({name, path});
    return save(list, error) ? path : QString();
}

bool Universes::remove(const QString &name, QString *error) {
    if (name == Mine) {
        if (error) *error = "\"mine\" is this computer's own list, and can't be removed.";
        return false;
    }
    QList<Universe> list = others();
    const qsizetype before = list.size();
    list.removeIf([&](const Universe &u) { return u.name == name; });
    if (list.size() == before) {
        if (error) *error = QString("No universe named %1. See: needful universe").arg(name);
        return false;
    }
    return save(list, error);
}

bool Universes::save(const QList<Universe> &others, QString *error) const {
    QJsonObject names;
    for (const Universe &u : others)
        names[u.name] = u.dataPath;
    QSaveFile file(registryPath());
    if (!QDir().mkpath(QFileInfo(registryPath()).absolutePath()) || !file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(QJsonObject{{"universes", names}}).toJson()) < 0 || !file.commit()) {
        if (error) *error = QString("Could not save %1: %2").arg(registryPath(), file.errorString());
        return false;
    }
    return true;
}
