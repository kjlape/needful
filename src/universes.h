#pragma once

#include <QList>
#include <QString>

// Universes: separate lists on one computer, such as your own, a partner's,
// and the house's. Each is its own data file, which can sync through its own
// folder. "mine" is the usual data file and is always there; the others are
// named here, per computer, and are only shown when asked for, so nobody's
// everyday list fills up with someone else's chores.
//
// The names live beside the usual data file, in universes.json, and each
// other universe's data file in universes/<name>.json.
struct Universe {
    QString name;     // mine, hers, house
    QString dataPath;
    QString label() const; // "Mine", "Hers"
};

class Universes {
public:
    static constexpr const char *Mine = "mine";

    // usualDataPath is this computer's own data file; empty means the usual.
    explicit Universes(const QString &usualDataPath = {});

    QString registryPath() const;
    QList<Universe> list() const; // Mine first, then by name.
    // The data file for a name, or empty when there's no such universe.
    QString dataPath(const QString &name) const;
    // The universe a data file belongs to, or empty for none.
    QString nameOf(const QString &dataPath) const;
    // Lower-case letters, digits, and hyphens.
    static bool validName(const QString &name);

    // Adds a name, with a data file of its own. Returns that file's path.
    QString add(const QString &name, QString *error);
    // Forgets the name on this computer; the data file stays.
    bool remove(const QString &name, QString *error);

private:
    bool save(const QList<Universe> &others, QString *error) const;
    QList<Universe> others() const;

    QString m_usual;
};
