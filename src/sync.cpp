#include "sync.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSysInfo>
#include <QUuid>
#include <algorithm>

namespace {
constexpr int RecordVersion = 1;
// Past the data file's own version, so a Needful from before sync refuses
// the pointer rather than reading it as an empty list.
constexpr int PointerVersion = 2;

const QStringList Lists{"contexts", "routines", "tasks", "completions", "log"};

// Fields that name another record, and the list that record is in.
QString pointsInto(const QString &list, const QString &field) {
    if (field == "context" || (list == "contexts" && field == "parent")) return "contexts";
    if (list == "tasks" && field == "routine") return "routines";
    if ((list == "completions" || list == "log") && field == "task") return "tasks";
    return {};
}

QString newRecordId() { return QUuid::createUuid().toString(QUuid::Id128).left(16); }
QString newMachineId() { return QUuid::createUuid().toString(QUuid::Id128).left(12); }

QString machineOf(const QString &stamp) { return stamp.section('-', 2); }

QJsonValue valueOf(const QJsonObject &fields, const QString &field) { return fields.value(field).toArray().at(0); }
QString stampOf(const QJsonObject &fields, const QString &field) { return fields.value(field).toArray().at(1).toString(); }

QDateTime readTime(const QJsonValue &value) { return QDateTime::fromString(value.toString(), Qt::ISODate); }

QString defaultName() {
    const QString host = QSysInfo::machineHostName().section('.', 0, 0).trimmed().toLower();
    return host.isEmpty() || host == "localhost" ? QString("computer") : host;
}

bool saveFile(const QString &path, const QByteArray &bytes, QString *error) {
    QSaveFile file(path);
    if (!QFileInfo(path).absoluteDir().mkpath(".") || !file.open(QIODevice::WriteOnly) || file.write(bytes) < 0 ||
        !file.commit()) {
        if (error) *error = QString("Could not save %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}
}

bool SyncFolder::readPointer(const QByteArray &dataFile, Pointer *pointer) {
    const QJsonObject root = QJsonDocument::fromJson(dataFile).object();
    const QJsonObject sync = root.value("sync").toObject();
    if (root.value("needful").toInt() != PointerVersion || sync.value("folder").toString().isEmpty() ||
        sync.value("machine").toString().isEmpty())
        return false;
    if (pointer)
        *pointer = {sync.value("folder").toString(), sync.value("machine").toString(), sync.value("computer").toString()};
    return true;
}

QByteArray SyncFolder::pointerJson(const Pointer &pointer) {
    return QJsonDocument(QJsonObject{{"needful", PointerVersion},
                                     {"sync", QJsonObject{{"folder", pointer.folder},
                                                          {"machine", pointer.machine},
                                                          {"computer", pointer.computer}}}})
        .toJson(QJsonDocument::Indented);
}

// A hash of the operating system's id for this computer, which stays the
// same when the host name changes, or of the host name where there is none.
QString SyncFolder::thisComputer() {
    QByteArray id = QSysInfo::machineUniqueId();
    if (id.isEmpty())
        id = QSysInfo::machineHostName().toUtf8();
    return QCryptographicHash::hash(id, QCryptographicHash::Sha256).toHex().left(16);
}

QString SyncFolder::elsewhereError(const QString &dataPath) {
    return QString("%1 was set up for sync on another computer; a sync tool probably copied it here. "
                   "Point Needful at a data file only this computer uses (--data or NEEDFUL_DATA), "
                   "then run needful sync setup <folder> here.")
        .arg(dataPath);
}

QString SyncFolder::syncedBy(const QString &path, QString *root) {
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (absolute.contains("/Library/Mobile Documents/")) {
        if (root) *root = absolute.section("/Library/Mobile Documents/", 0, 0) + "/Library/Mobile Documents";
        return "iCloud Drive";
    }
    // The marks each tool leaves at the top of a folder it keeps.
    static const QList<std::pair<QString, QStringList>> marks{
        {"Syncthing", {".stfolder"}},
        {"Dropbox", {".dropbox", ".dropbox.cache"}},
        {"Nextcloud", {".sync_*.db", "._sync_*.db", ".nextcloudsync.log", ".owncloudsync.log"}},
    };
    QDir dir = QFileInfo(absolute).absoluteDir();
    for (int depth = 0; depth < 64; ++depth) {
        for (const auto &[tool, names] : marks)
            if (!dir.entryList(names, QDir::AllEntries | QDir::Hidden | QDir::System).isEmpty()) {
                if (root) *root = dir.absolutePath();
                return tool;
            }
        if (!dir.cdUp())
            break;
    }
    return {};
}

SyncFolder::SyncFolder(const QString &folder, const QString &machine) : m_folder(folder), m_machine(machine) {}

QString SyncFolder::machinesPath() const { return m_folder + "/machines"; }
QString SyncFolder::ownPath() const { return machinesPath() + "/" + m_machine + ".json"; }

QStringList SyncFolder::watchPaths() const {
    QStringList paths;
    const QDir dir(machinesPath());
    if (!dir.exists())
        return paths;
    paths.append(dir.absolutePath());
    for (const QString &file : dir.entryList({"*.json"}, QDir::Files))
        paths.append(dir.filePath(file));
    return paths;
}

QString SyncFolder::stamp(const QDateTime &now) {
    const qint64 ms = now.toMSecsSinceEpoch();
    if (ms > m_clockMs) {
        m_clockMs = ms;
        m_clockCount = 0;
    } else if (++m_clockCount > 0xffff) {
        ++m_clockMs;
        m_clockCount = 0;
    }
    return QString("%1-%2-%3").arg(m_clockMs, 13, 10, QChar('0')).arg(m_clockCount, 4, 16, QChar('0')).arg(m_machine);
}

void SyncFolder::absorb(const QString &stamp) {
    const qint64 ms = stamp.section('-', 0, 0).toLongLong();
    const int count = stamp.section('-', 1, 1).toInt(nullptr, 16);
    if (ms > m_clockMs || (ms == m_clockMs && count > m_clockCount)) {
        m_clockMs = ms;
        m_clockCount = count;
    }
}

bool SyncFolder::read(Store *store, QString *error) {
    static const QRegularExpression fileName("^([0-9a-f]{12})\\.json$");
    const QDir dir(machinesPath());
    for (const QString &file : dir.entryList({"*.json"}, QDir::Files)) {
        // Anything else in the folder, a sync tool's conflict copy included,
        // was not written by Needful.
        const auto match = fileName.match(file);
        if (!match.hasMatch())
            continue;
        const QString id = match.captured(1);
        QFile handle(dir.filePath(file));
        QJsonParseError parseError;
        const QJsonDocument document =
            handle.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(handle.readAll(), &parseError) : QJsonDocument();
        const QJsonObject record = document.object();
        if (record.value("needfulSync").toInt() > RecordVersion) {
            if (error) *error = "Another computer synced changes from a newer Needful. Update Needful to read them.";
            return false;
        }
        if (!document.isObject()) {
            // Another machine's file may be half way through arriving; keep
            // what it said last. This machine's own must read, or the next
            // save would write over what it held.
            if (id == m_machine) {
                if (error) *error = QString("Could not read %1: %2").arg(handle.fileName(), parseError.errorString());
                return false;
            }
            continue;
        }
        m_files[id] = record;
    }

    m_own.clear();
    m_merged.clear();
    m_meta = {};
    for (auto file = m_files.cbegin(); file != m_files.cend(); ++file) {
        const QJsonObject &record = file.value();
        absorb(record.value("clock").toString());
        const bool own = file.key() == m_machine;
        if (own)
            for (const QString key : {"name", "changed", "clock", "forgotten", "list", "saved", "device"})
                if (record.contains(key))
                    m_meta[key] = record.value(key);
        for (const QString &list : Lists) {
            const QJsonObject records = record.value(list).toObject();
            for (auto r = records.begin(); r != records.end(); ++r) {
                const QJsonObject fields = r.value().toObject();
                Fields &merged = m_merged[list][r.key()];
                for (auto f = fields.begin(); f != fields.end(); ++f) {
                    const QJsonArray entry = f.value().toArray();
                    if (entry.size() == 2 && stampOf(merged, f.key()) < entry.at(1).toString())
                        merged[f.key()] = entry;
                }
                if (own)
                    m_own[list][r.key()] = fields;
            }
        }
    }
    materialize(store);
    return true;
}

// Builds the store from the merged fields: numbers for every record, the
// records' links turned into numbers, and completions made the same day on
// two machines folded into one.
void SyncFolder::materialize(Store *store) {
    QHash<QString, QHash<QString, int>> numbers;
    QHash<QString, QSet<QString>> live;
    QJsonObject highest;
    for (const QString &list : Lists) {
        struct Entry {
            QString id;
            int number;
            QString made;
        };
        QList<Entry> entries;
        for (auto r = m_merged[list].cbegin(); r != m_merged[list].cend(); ++r) {
            // A record whose maker's file hasn't arrived yet waits for it.
            if (!r.value().contains("id"))
                continue;
            entries.append({r.key(), valueOf(r.value(), "id").toInt(), stampOf(r.value(), "id")});
            if (!valueOf(r.value(), "deleted").toBool())
                live[list].insert(r.key());
        }
        auto earlier = [](const Entry &a, const Entry &b) { return a.made != b.made ? a.made < b.made : a.id < b.id; };
        std::sort(entries.begin(), entries.end(), [&](const Entry &a, const Entry &b) {
            return a.number != b.number ? a.number < b.number : earlier(a, b);
        });
        // The record made first keeps a number two machines both picked; the
        // other moves past the highest, the same way on every machine.
        QSet<int> used;
        QList<Entry> moved;
        int top = 0;
        for (const Entry &e : entries) {
            if (e.number > 0 && !used.contains(e.number)) {
                used.insert(e.number);
                numbers[list][e.id] = e.number;
                top = std::max(top, e.number);
            } else {
                moved.append(e);
            }
        }
        std::sort(moved.begin(), moved.end(), earlier);
        for (const Entry &e : moved)
            numbers[list][e.id] = ++top;
        highest[list] = top;
    }

    auto resolve = [&](const QString &target, const QJsonValue &link) -> int {
        QString id = link.toString();
        if (target == "contexts") {
            // What was in a deleted context moves up to its parent, as it
            // does when the context is deleted on this machine.
            for (int depth = 0; depth < 32 && !id.isEmpty(); ++depth) {
                if (live["contexts"].contains(id))
                    return numbers["contexts"].value(id);
                if (!numbers["contexts"].contains(id))
                    return 0;
                id = valueOf(m_merged["contexts"][id], "parent").toString();
            }
            return 0;
        }
        if (target == "routines")
            return live["routines"].contains(id) ? numbers["routines"].value(id) : 0;
        // History keeps pointing at a deleted task.
        return numbers[target].value(id, 0);
    };

    // Done on two machines the same day is one completion, keeping both notes.
    m_folded.clear();
    QSet<QString> folded;
    QHash<QString, QString> notes;
    QHash<QString, QStringList> sameDay;
    for (const QString &id : live["completions"]) {
        const Fields &f = m_merged["completions"][id];
        const QString task = valueOf(f, "task").toString();
        if (!task.isEmpty())
            sameDay[task + "|" + readTime(valueOf(f, "at")).date().toString(Qt::ISODate)].append(id);
    }
    for (QStringList &group : sameDay) {
        if (group.size() < 2)
            continue;
        const auto &records = m_merged["completions"];
        std::sort(group.begin(), group.end(), [&](const QString &a, const QString &b) {
            const QDateTime ta = readTime(valueOf(records[a], "at")), tb = readTime(valueOf(records[b], "at"));
            return ta != tb ? ta < tb : a < b;
        });
        const QString first = group.first();
        const QString maker = machineOf(stampOf(records[first], "id"));
        QSet<QString> from;
        QStringList kept{valueOf(records[first], "note").toString()};
        for (const QString &other : group.mid(1)) {
            const QString otherMaker = machineOf(stampOf(records[other], "id"));
            // Done twice on one machine is done twice.
            if (otherMaker == maker || from.contains(otherMaker))
                continue;
            from.insert(otherMaker);
            folded.insert(other);
            m_folded[first].append(other);
            const QString note = valueOf(records[other], "note").toString();
            if (!note.isEmpty() && !kept.contains(note))
                kept.append(note);
        }
        kept.removeAll(QString());
        if (m_folded.contains(first))
            notes[first] = kept.join("\n");
    }

    QJsonObject root{{"needful", 1}, {"reserved", highest}};
    for (const QString &list : Lists) {
        QList<QJsonObject> items;
        for (const QString &id : live[list]) {
            if (list == "completions" && folded.contains(id))
                continue;
            const Fields &fields = m_merged[list][id];
            QJsonObject item;
            for (auto f = fields.begin(); f != fields.end(); ++f) {
                if (f.key() == "deleted")
                    continue;
                const QString target = pointsInto(list, f.key());
                item[f.key()] = target.isEmpty() ? valueOf(fields, f.key()) : QJsonValue(resolve(target, valueOf(fields, f.key())));
            }
            item["id"] = numbers[list][id];
            if (notes.contains(id))
                item["note"] = notes[id];
            items.append(item);
        }
        std::sort(items.begin(), items.end(),
                  [](const QJsonObject &a, const QJsonObject &b) { return a["id"].toInt() < b["id"].toInt(); });
        QJsonArray array;
        for (const auto &item : items)
            array.append(item);
        root[list] = array;
    }
    store->fromJson(QJsonDocument(root).toJson(QJsonDocument::Compact));
    m_base = *store;
    m_ids.clear();
    for (auto list = numbers.cbegin(); list != numbers.cend(); ++list)
        for (auto n = list.value().cbegin(); n != list.value().cend(); ++n)
            m_ids[list.key()][n.value()] = n.key();
}

bool SyncFolder::write(Store *store, const QDateTime &now, QString *error) {
    const QJsonObject current = QJsonDocument::fromJson(store->toJson()).object();
    const QJsonObject before = QJsonDocument::fromJson(m_base.toJson()).object();
    // Kept apart until saved, so a failed save changes nothing.
    Records own = m_own;
    auto ids = m_ids;
    const QString when = stamp(now);
    bool changed = false;
    auto put = [&](const QString &list, const QString &record, const QString &field, const QJsonValue &value) {
        own[list][record][field] = QJsonArray{value, when};
        changed = true;
    };
    // Links between records are by record id, which every machine shares.
    auto shared = [&](const QString &list, const QString &field, const QJsonValue &value) -> QJsonValue {
        const QString target = pointsInto(list, field);
        if (target.isEmpty())
            return value;
        const int number = value.toInt();
        if (!number)
            return QJsonValue::Null;
        if (!ids[target].contains(number)) {
            // History of a task deleted before sync was set up: a deleted
            // record stands in, so the history still points at something.
            if (target != "tasks")
                return QJsonValue::Null;
            const QString ghost = newRecordId();
            ids[target][number] = ghost;
            put(target, ghost, "id", number);
            put(target, ghost, "deleted", true);
        }
        return ids[target][number];
    };

    // New records get ids first, so others made in the same change can name them.
    for (const QString &list : Lists)
        for (const auto &value : current.value(list).toArray()) {
            const int number = value.toObject().value("id").toInt();
            if (!ids[list].contains(number))
                ids[list][number] = newRecordId();
        }

    for (const QString &list : Lists) {
        QHash<int, QJsonObject> was;
        for (const auto &value : before.value(list).toArray())
            was[value.toObject().value("id").toInt()] = value.toObject();
        QSet<int> still;
        for (const auto &value : current.value(list).toArray()) {
            const QJsonObject item = value.toObject();
            const int number = item.value("id").toInt();
            still.insert(number);
            const QString record = ids[list][number];
            const QJsonObject old = was.value(number);
            const bool known = m_merged.value(list).value(record).contains("id");
            for (auto f = item.begin(); f != item.end(); ++f) {
                if (f.key() == "id") {
                    if (!known)
                        put(list, record, "id", number);
                } else if (old.isEmpty() || old.value(f.key()) != f.value()) {
                    put(list, record, f.key(), shared(list, f.key(), f.value()));
                }
            }
            // Brought back by undo.
            if (old.isEmpty() && known)
                put(list, record, "deleted", false);
        }
        for (auto it = was.cbegin(); it != was.cend(); ++it) {
            if (still.contains(it.key()))
                continue;
            const QString record = ids[list][it.key()];
            put(list, record, "deleted", true);
            for (const QString &other : m_folded.value(record))
                put(list, other, "deleted", true);
        }
    }
    if (!changed)
        return true;

    QJsonObject meta = m_meta;
    meta["changed"] = now.toUTC().toString(Qt::ISODateWithMs);
    meta["clock"] = when;
    if (!saveOwn(own, meta, error))
        return false;
    return read(store, error);
}

bool SyncFolder::saveOwn(const Records &records, const QJsonObject &meta, QString *error) {
    QJsonObject root = meta;
    root["needfulSync"] = RecordVersion;
    root["machine"] = m_machine;
    // Only this machine writes its file, so a count of saves orders its
    // copies wherever they travel.
    root["saved"] = qint64(m_meta.value("saved").toInteger()) + 1;
    if (const QString list = listId(); !list.isEmpty() && !root.contains("list"))
        root["list"] = list;
    if (!root.contains("name"))
        root["name"] = defaultName();
    for (const QString &list : Lists) {
        QJsonObject entries;
        const auto &byId = records.value(list);
        for (auto r = byId.cbegin(); r != byId.cend(); ++r)
            entries[r.key()] = r.value();
        root[list] = entries;
    }
    if (!saveFile(ownPath(), QJsonDocument(root).toJson(QJsonDocument::Compact), error))
        return false;
    m_own = records;
    m_meta = meta;
    m_meta["saved"] = root["saved"];
    if (root.contains("list"))
        m_meta["list"] = root["list"];
    m_files[m_machine] = root;
    return true;
}

// The lowest id any machine gives it, so two machines that each made one
// up before hearing of the other end up agreeing.
QString SyncFolder::listId() const {
    QString lowest;
    for (const auto &file : m_files) {
        const QString id = file.value("list").toString();
        if (!id.isEmpty() && (lowest.isEmpty() || id < lowest))
            lowest = id;
    }
    const QString own = m_meta.value("list").toString();
    if (!own.isEmpty() && (lowest.isEmpty() || own < lowest))
        lowest = own;
    return lowest;
}

bool SyncFolder::ensureListId(QString *error) {
    QJsonObject meta = m_meta;
    const QString id = listId().isEmpty() ? newRecordId() : listId();
    if (meta.value("list").toString() == id && m_files.contains(m_machine))
        return true;
    meta["list"] = id;
    return saveOwn(m_own, meta, error);
}

bool SyncFolder::setDevice(const QString &device, QString *error) {
    if (m_meta.value("device").toString() == device && m_files.contains(m_machine))
        return true;
    QJsonObject meta = m_meta;
    meta["device"] = device;
    return saveOwn(m_own, meta, error);
}

SyncFolder::File SyncFolder::describe(const QJsonObject &file) {
    return {qint64(file.value("saved").toInteger()), file.value("clock").toString(), file.value("device").toString()};
}

QHash<QString, SyncFolder::File> SyncFolder::files() const {
    QHash<QString, File> found;
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it)
        found[it.key()] = describe(it.value());
    return found;
}

QString SyncFolder::name() const {
    return m_meta.value("name").toString(defaultName());
}

QList<SyncFolder::Machine> SyncFolder::machines() const {
    // Forgetting a machine hides it until it changes something again.
    QHash<QString, QDateTime> forgotten;
    for (const auto &record : m_files) {
        const QJsonObject list = record.value("forgotten").toObject();
        for (auto it = list.begin(); it != list.end(); ++it) {
            const QDateTime at = readTime(it.value());
            if (!forgotten.contains(it.key()) || forgotten[it.key()] < at)
                forgotten[it.key()] = at;
        }
    }
    QList<Machine> found;
    for (auto file = m_files.cbegin(); file != m_files.cend(); ++file) {
        const QJsonObject &record = file.value();
        Machine m;
        m.id = file.key();
        m.name = file.key() == m_machine ? name() : record.value("name").toString(file.key());
        m.changed = readTime(record.value("changed"));
        m.forgotten = file.key() != m_machine && forgotten.contains(m.id) && !(m.changed > forgotten[m.id]);
        for (const QString &list : Lists)
            m.records += record.value(list).toObject().size();
        found.append(m);
    }
    if (!m_files.contains(m_machine))
        found.append({m_machine, name(), {}, false, 0});
    std::sort(found.begin(), found.end(),
              [](const Machine &a, const Machine &b) { return a.name.localeAwareCompare(b.name) < 0; });
    return found;
}

bool SyncFolder::rename(const QString &name, QString *error) {
    const QString wanted = name.trimmed();
    if (wanted.isEmpty()) {
        if (error) *error = "Give the machine a name: needful sync rename laptop";
        return false;
    }
    for (const Machine &m : machines())
        if (m.id != m_machine && !m.forgotten && m.name.compare(wanted, Qt::CaseInsensitive) == 0) {
            if (error) *error = QString("Another machine is already called \"%1\".").arg(m.name);
            return false;
        }
    QJsonObject meta = m_meta;
    meta["name"] = wanted;
    return saveOwn(m_own, meta, error);
}

bool SyncFolder::forget(const QString &machineId, QString *error) {
    QJsonObject meta = m_meta;
    QJsonObject forgotten = meta.value("forgotten").toObject();
    forgotten[machineId] = m_files.value(machineId).value("changed");
    meta["forgotten"] = forgotten;
    return saveOwn(m_own, meta, error);
}

Store SyncFolder::combine(const Store &folder, const Store &local, Setup *counts) {
    Store out = folder;
    QHash<int, int> contexts, routines, tasks;

    // Contexts by name under the same parent, parents first.
    QList<Context> pending = local.contexts;
    for (int pass = 0; pass < 64 && !pending.isEmpty(); ++pass) {
        QList<Context> waiting;
        for (const Context &c : pending) {
            if (c.parent && !contexts.contains(c.parent) && pass < 63) {
                waiting.append(c);
                continue;
            }
            const int parent = contexts.value(c.parent, 0);
            int match = 0;
            for (const Context &there : out.contexts)
                if (there.parent == parent && there.name.compare(c.name, Qt::CaseInsensitive) == 0)
                    match = there.id;
            if (!match) {
                match = out.addContext(c.name, parent);
                ++counts->contexts;
            }
            contexts[c.id] = match;
        }
        pending = waiting;
    }

    for (const Routine &r : local.routines) {
        int match = 0;
        for (const Routine &there : out.routines)
            if (there.title == r.title)
                match = there.id;
        if (!match) {
            Routine copy = r;
            copy.id = match = out.nextId("routines");
            out.routines.append(copy);
        }
        routines[r.id] = match;
    }

    QList<std::pair<const Task *, int>> matched;
    for (const Task &t : local.tasks) {
        const int context = contexts.value(t.context, 0), routine = routines.value(t.routine, 0);
        int match = 0;
        for (const Task &there : out.tasks)
            if (there.title == t.title && there.context == context && there.routine == routine)
                match = there.id;
        if (match) {
            matched.append({&t, match});
        } else {
            Task copy = t;
            copy.id = match = out.nextId("tasks");
            copy.context = context;
            copy.routine = routine;
            out.tasks.append(copy);
            ++counts->tasks;
        }
        tasks[t.id] = match;
    }
    // History of tasks deleted here still needs a task number of its own.
    auto taskFor = [&](int id) {
        if (!id)
            return 0;
        if (!tasks.contains(id)) {
            tasks[id] = out.nextId("tasks");
            out.reserved["tasks"] = tasks[id];
        }
        return tasks[id];
    };

    // A task done here more recently than there takes its schedule from here.
    QHash<int, QDateTime> lastThere;
    for (const Completion &c : folder.completions)
        if (!lastThere.contains(c.task) || lastThere[c.task] < c.at)
            lastThere[c.task] = c.at;
    for (const Completion &c : local.completions) {
        const int task = taskFor(c.task);
        bool known = false;
        for (const Completion &there : out.completions)
            if (there.task == task && qAbs(there.at.secsTo(c.at)) < 1)
                known = true;
        if (known)
            continue;
        Completion copy = c;
        copy.id = out.nextId("completions");
        copy.task = task;
        copy.context = contexts.value(c.context, 0);
        out.completions.append(copy);
        ++counts->history;
    }
    for (const auto &[t, id] : matched) {
        const Completion *here = local.lastDone(t->id);
        if (!here || (lastThere.contains(id) && lastThere[id] >= here->at))
            continue;
        Task *there = out.task(id);
        there->due = t->due;
        there->snoozedUntil = t->snoozedUntil;
        there->today = t->today;
        there->archived = t->archived;
    }

    for (const LogEntry &e : local.log) {
        const int context = contexts.value(e.context, 0);
        bool known = false;
        for (const LogEntry &there : out.log)
            if (there.context == context && there.text == e.text && qAbs(there.at.secsTo(e.at)) < 1)
                known = true;
        if (known)
            continue;
        LogEntry copy = e;
        copy.id = out.nextId("log");
        copy.context = context;
        copy.task = taskFor(e.task);
        out.log.append(copy);
        ++counts->history;
    }
    return out;
}

bool SyncFolder::setup(const QString &dataPath, const QString &folder, const QString &name, const QDateTime &now,
                       Setup *result, QString *error, const QString &defaultDataPath, const QString &listId) {
    const QString absolute = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    QByteArray contents;
    if (QFile file(dataPath); file.open(QIODevice::ReadOnly))
        contents = file.readAll();

    // The data file must stay on this computer: if a sync tool shared it,
    // the pointer would reach the other computers and they would all write
    // as this one.
    const QString data = QDir::cleanPath(QFileInfo(dataPath).absoluteFilePath());
    const QString usual = defaultDataPath.isEmpty() ? QString("the usual data folder")
                                                    : QDir::cleanPath(QFileInfo(defaultDataPath).absoluteFilePath());
    if (data.startsWith(absolute + "/")) {
        if (error)
            *error = QString("The data file %1 is inside %2. Keep it somewhere only this computer uses, like %3, "
                             "and run setup again.")
                         .arg(data, absolute, usual);
        return false;
    }
    QString shared;
    if (const QString tool = syncedBy(data, &shared); !tool.isEmpty()) {
        if (error)
            *error = QString("The data file %1 is in %2, which %3 keeps in step with your other computers. "
                             "Copy it somewhere only this computer uses, like %4, point Needful there, "
                             "and run setup again.")
                         .arg(data, shared, tool, usual);
        return false;
    }

    Pointer pointer;
    if (readPointer(contents, &pointer)) {
        if (pointer.elsewhere()) {
            if (error) *error = elsewhereError(dataPath);
            return false;
        }
        if (QDir::cleanPath(pointer.folder) != absolute) {
            if (error) *error = QString("This machine already syncs with %1.").arg(pointer.folder);
            return false;
        }
        SyncFolder sync(absolute, pointer.machine);
        Store store;
        if (!sync.read(&store, error))
            return false;
        result->already = true;
        result->name = sync.name();
        return true;
    }

    Store local;
    if (!contents.isEmpty() && !local.fromJson(contents, error))
        return false;
    if (!QDir().mkpath(absolute + "/machines")) {
        if (error) *error = QString("Could not create %1").arg(absolute);
        return false;
    }
    SyncFolder sync(absolute, newMachineId());
    Store there;
    if (!sync.read(&there, error))
        return false;
    QStringList taken;
    for (const Machine &m : sync.machines())
        if (m.id != sync.m_machine && !m.forgotten) {
            result->others.append(m.name);
            taken.append(m.name.toLower());
        }

    const QString base = name.trimmed().isEmpty() ? defaultName() : name.trimmed();
    QString chosen = base;
    for (int n = 2; taken.contains(chosen.toLower()); ++n)
        chosen = QString("%1-%2").arg(base).arg(n);
    sync.m_meta["name"] = chosen;
    sync.m_meta["changed"] = now.toUTC().toString(Qt::ISODateWithMs); // Setting up counts as being heard from.
    sync.m_meta["list"] = !listId.isEmpty() ? listId : !sync.listId().isEmpty() ? sync.listId() : newRecordId();
    result->name = chosen;
    result->joined = !result->others.isEmpty();

    Store combined = local;
    if (result->joined) {
        combined = combine(there, local, result);
    } else {
        result->tasks = local.tasks.size();
        result->contexts = local.contexts.size();
        result->history = local.completions.size() + local.log.size();
    }
    if (!sync.write(&combined, now, error))
        return false;
    // Written even with nothing to move, so the other machines see this one.
    if (!QFile::exists(sync.ownPath()) && !sync.saveOwn(sync.m_own, sync.m_meta, error))
        return false;

    // The single file stays beside the pointer as it was, as a backup.
    if (!contents.isEmpty()) {
        QFile::remove(dataPath + ".before-sync");
        QFile::copy(dataPath, dataPath + ".before-sync");
    }
    return saveFile(dataPath, pointerJson({absolute, sync.m_machine, thisComputer()}), error);
}
