#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "store.h"

// Keeping the same Needful on several of one person's computers, through a
// folder a sync tool (Syncthing, Dropbox, iCloud Drive, Nextcloud) keeps in
// step. Needful never moves bytes between machines; it makes the data safe
// for a sync tool to move.
//
// Each machine writes only its own file, machines/<id>.json, so no two
// machines ever write the same file and the sync tool never makes a conflict
// copy. The file holds the latest value this machine gave each field of each
// record, stamped with when it was given. Every machine reads every file and
// keeps, field by field, the value with the latest stamp, so every machine
// builds the same list from the same files, whatever order they arrived in.
//
// Stamps are hybrid logical clocks: the wall clock, never allowed to go
// backwards, and always past every stamp already seen. A machine whose clock
// jumps back still orders its own changes right, and a change made after
// seeing another machine's always wins over it. Ties go to the machine id, so
// the order is the same everywhere.
//
// Records are keyed by ids made up on the machine that made them, so new
// tasks made on two machines at once never become one task. The window and
// the command line still use small numbers; when two machines picked the same
// number, the record made first keeps it.
class SyncFolder {
public:
    struct Machine {
        QString id;
        QString name;
        QDateTime changed; // When it last recorded a change; invalid for never.
        bool forgotten = false;
        int records = 0;   // How many records it has changed.
    };

    // The data file of a synced machine points at the folder instead of
    // holding the data. Its version is past what older Needfuls read, so they
    // refuse it instead of starting empty and writing over it.
    //
    // The pointer also says which computer it belongs to. If a sync tool
    // copied it to another computer, that one would write as this machine,
    // and two computers writing one file is what this exists to prevent.
    struct Pointer {
        QString folder;
        QString machine;
        QString computer; // Empty in pointers from before it was kept.
        bool elsewhere() const { return !computer.isEmpty() && computer != thisComputer(); }
    };
    static bool readPointer(const QByteArray &dataFile, Pointer *pointer);
    static QByteArray pointerJson(const Pointer &pointer);
    static QString thisComputer();
    static QString elsewhereError(const QString &dataPath);
    // The sync tool keeping a folder that holds `path` in step ("Syncthing",
    // "Dropbox", ...), or empty. `root` is set to that folder.
    static QString syncedBy(const QString &path, QString *root = nullptr);

    SyncFolder(const QString &folder, const QString &machine);

    QString folder() const { return m_folder; }
    QString machine() const { return m_machine; }
    QString machinesPath() const;
    QString ownPath() const;
    QStringList watchPaths() const;

    // Reads every machine's file and builds the store from them all.
    bool read(Store *store, QString *error);
    // Records what differs between the store last read and `store`, as
    // changes made at `now`, then reads again, so `store` ends up as every
    // machine will build it.
    bool write(Store *store, const QDateTime &now, QString *error);

    // As of the last read, sorted by name.
    QList<Machine> machines() const;
    QString name() const;
    bool rename(const QString &name, QString *error);
    bool forget(const QString &machineId, QString *error);

    // Setting up: points the data file at the folder, moving in what it held.
    // Joining a folder another machine already uses merges in only what isn't
    // there already, so data copied between the machines before isn't doubled.
    struct Setup {
        bool joined = false;
        bool already = false;
        QStringList others; // The other machines' names.
        QString name;
        int tasks = 0, contexts = 0, history = 0; // Moved in, or merged in.
    };
    // Refuses a data file that a sync tool shares, or that sits inside the
    // folder; defaultDataPath is where the message suggests keeping it.
    static bool setup(const QString &dataPath, const QString &folder, const QString &name, const QDateTime &now,
                      Setup *result, QString *error, const QString &defaultDataPath = {});
    // Adds to `folder` what only `local` has; counts what it added.
    static Store combine(const Store &folder, const Store &local, Setup *counts);

private:
    using Fields = QJsonObject;                          // field → [value, stamp]
    using Records = QHash<QString, QHash<QString, Fields>>; // list → record id → fields

    QString stamp(const QDateTime &now);
    void absorb(const QString &stamp);
    bool saveOwn(const Records &records, const QJsonObject &meta, QString *error);
    void materialize(Store *store);

    QString m_folder, m_machine;
    QJsonObject m_meta; // This machine's name, last change, clock, and forgotten machines.
    qint64 m_clockMs = 0;
    int m_clockCount = 0;

    QHash<QString, QJsonObject> m_files; // Each machine's file as last read well.
    Records m_own, m_merged;
    Store m_base;                                       // The store as last built.
    QHash<QString, QHash<int, QString>> m_ids;          // list → number → record id
    QHash<QString, QStringList> m_folded;               // completion → completions folded into it
};
