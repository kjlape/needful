#include "store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

namespace {
constexpr int FormatVersion = 1;

QString dateText(const QDate &date) { return date.isValid() ? date.toString(Qt::ISODate) : QString(); }
QDate readDate(const QJsonValue &value) { return QDate::fromString(value.toString(), Qt::ISODate); }

template <typename T> int highestId(const QList<T> &items) {
    int highest = 0;
    for (const auto &item : items)
        highest = std::max(highest, item.id);
    return highest;
}

QDate step(const QDate &date, int every, const QString &unit) {
    if (unit == "day") return date.addDays(every);
    if (unit == "month") return date.addMonths(every);
    if (unit == "year") return date.addYears(every);
    return date.addDays(7 * every);
}
}

bool Store::load(const QString &path, QString *error) {
    QFile file(path);
    if (!file.exists()) {
        // A first run starts empty; the file appears on the first save.
        *this = Store();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QString("Could not read %1: %2").arg(path, file.errorString());
        return false;
    }
    return fromJson(file.readAll(), error);
}

bool Store::save(const QString &path, QString *error) const {
    const QFileInfo info(path);
    if (!info.absoluteDir().exists() && !info.absoluteDir().mkpath(".")) {
        if (error) *error = QString("Could not create %1").arg(info.absolutePath());
        return false;
    }
    // One step of undo on disk, for the day the app itself gets it wrong.
    if (info.exists()) {
        QFile::remove(path + ".bak");
        QFile::copy(path, path + ".bak");
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(toJson()) < 0 || !file.commit()) {
        if (error) *error = QString("Could not save %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QByteArray Store::toJson() const {
    QJsonArray contextArray, taskArray, completionArray;
    for (const auto &c : contexts)
        contextArray.append(QJsonObject{{"id", c.id}, {"name", c.name}, {"parent", c.parent}});
    for (const auto &t : tasks) {
        QJsonArray months;
        for (int m : t.months)
            months.append(m);
        static const char *kinds[] = {"once", "every", "ask"};
        taskArray.append(QJsonObject{
            {"id", t.id},
            {"title", t.title},
            {"notes", t.notes},
            {"context", t.context},
            {"priority", t.priority},
            {"schedule", QJsonObject{{"kind", kinds[t.schedule.kind]},
                                     {"every", t.schedule.every},
                                     {"unit", t.schedule.unit},
                                     {"fromDone", t.schedule.fromDone}}},
            {"months", months},
            {"due", dateText(t.due)},
            {"snoozedUntil", dateText(t.snoozedUntil)},
            {"today", dateText(t.today)},
            {"archived", t.archived},
            {"created", dateText(t.created)},
        });
    }
    for (const auto &c : completions)
        completionArray.append(QJsonObject{{"id", c.id},
                                           {"task", c.task},
                                           {"title", c.title},
                                           {"context", c.context},
                                           {"at", c.at.toString(Qt::ISODate)},
                                           {"note", c.note}});
    return QJsonDocument(QJsonObject{{"needful", FormatVersion},
                                     {"contexts", contextArray},
                                     {"tasks", taskArray},
                                     {"completions", completionArray}})
        .toJson(QJsonDocument::Indented);
}

bool Store::fromJson(const QByteArray &json, QString *error) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QString("The data file is not valid JSON: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value("needful").toInt() > FormatVersion) {
        if (error) *error = "The data file was written by a newer Needful. Update Needful to open it.";
        return false;
    }

    Store loaded;
    for (const auto &value : root.value("contexts").toArray()) {
        const QJsonObject o = value.toObject();
        loaded.contexts.append({o.value("id").toInt(), o.value("name").toString(), o.value("parent").toInt()});
    }
    for (const auto &value : root.value("tasks").toArray()) {
        const QJsonObject o = value.toObject();
        const QJsonObject s = o.value("schedule").toObject();
        Task t;
        t.id = o.value("id").toInt();
        t.title = o.value("title").toString();
        t.notes = o.value("notes").toString();
        t.context = o.value("context").toInt();
        t.priority = std::clamp(o.value("priority").toInt(2), 1, 4);
        const QString kind = s.value("kind").toString("every");
        t.schedule.kind = kind == "once" ? Schedule::Once : kind == "ask" ? Schedule::Ask : Schedule::Every;
        t.schedule.every = std::max(1, s.value("every").toInt(1));
        t.schedule.unit = s.value("unit").toString("week");
        t.schedule.fromDone = s.value("fromDone").toBool(true);
        for (const auto &m : o.value("months").toArray())
            if (m.toInt() >= 1 && m.toInt() <= 12)
                t.months.append(m.toInt());
        t.due = readDate(o.value("due"));
        t.snoozedUntil = readDate(o.value("snoozedUntil"));
        t.today = readDate(o.value("today"));
        t.archived = o.value("archived").toBool();
        t.created = readDate(o.value("created"));
        loaded.tasks.append(t);
    }
    for (const auto &value : root.value("completions").toArray()) {
        const QJsonObject o = value.toObject();
        loaded.completions.append({o.value("id").toInt(), o.value("task").toInt(), o.value("title").toString(),
                                   o.value("context").toInt(),
                                   QDateTime::fromString(o.value("at").toString(), Qt::ISODate),
                                   o.value("note").toString()});
    }
    *this = loaded;
    return true;
}

Task *Store::task(int id) {
    for (auto &t : tasks)
        if (t.id == id)
            return &t;
    return nullptr;
}

const Task *Store::task(int id) const { return const_cast<Store *>(this)->task(id); }

const Context *Store::context(int id) const {
    for (const auto &c : contexts)
        if (c.id == id)
            return &c;
    return nullptr;
}

QString Store::contextPath(int id) const {
    QStringList names;
    // The guard stops a hand-edited file with a parent loop from hanging.
    for (const Context *c = context(id); c && names.size() < 32; c = context(c->parent))
        names.prepend(c->name);
    return names.join(QString::fromUtf8(" › "));
}

QList<int> Store::withDescendants(int id) const {
    QList<int> found{id};
    for (int i = 0; i < found.size(); ++i)
        for (const auto &c : contexts)
            if (c.parent == found[i] && !found.contains(c.id))
                found.append(c.id);
    return found;
}

bool Store::inContext(int contextId, int filter) const {
    return filter == 0 || withDescendants(filter).contains(contextId);
}

int Store::findContext(const QString &pathOrName) const {
    const QString wanted = pathOrName.trimmed();
    if (wanted.isEmpty())
        return 0;
    for (const auto &c : contexts) {
        const QString path = contextPath(c.id);
        QString slashed = path;
        slashed.replace(QString::fromUtf8(" › "), "/");
        if (c.name.compare(wanted, Qt::CaseInsensitive) == 0 || slashed.compare(wanted, Qt::CaseInsensitive) == 0 ||
            path.compare(wanted, Qt::CaseInsensitive) == 0)
            return c.id;
    }
    return -1;
}

QList<Completion> Store::history(int taskId) const {
    QList<Completion> found;
    for (const auto &c : completions)
        if (c.task == taskId)
            found.append(c);
    std::sort(found.begin(), found.end(), [](const Completion &a, const Completion &b) { return a.at > b.at; });
    return found;
}

const Completion *Store::lastDone(int taskId) const {
    const Completion *latest = nullptr;
    for (const auto &c : completions)
        if (c.task == taskId && (!latest || c.at > latest->at))
            latest = &c;
    return latest;
}

bool Store::inSeason(const Task &task, const QDate &day) {
    return task.months.isEmpty() || task.months.contains(day.month());
}

int Store::periodDays(const Schedule &schedule) {
    if (schedule.kind != Schedule::Every)
        return 7;
    const int unit = schedule.unit == "day" ? 1 : schedule.unit == "month" ? 30 : schedule.unit == "year" ? 365 : 7;
    return std::max(1, unit * schedule.every);
}

Store::State Store::state(const Task &task, const QDate &today) const {
    if (task.archived) return Archived;
    if (!inSeason(task, today)) return OutOfSeason;
    if (task.snoozedUntil.isValid() && task.snoozedUntil > today) return Snoozed;
    if (!task.due.isValid() || task.due <= today) return Due;
    return Upcoming;
}

int Store::overdueDays(const Task &task, const QDate &today) const {
    if (!task.due.isValid() || task.due >= today)
        return 0;
    // Time out of season doesn't count: the mowing that fell due in October
    // is only as late as the days since this season started.
    QDate start = task.due;
    if (!task.months.isEmpty()) {
        QDate seasonStart = today;
        for (int i = 0; i < 366 && inSeason(task, seasonStart.addDays(-1)); ++i)
            seasonStart = seasonStart.addDays(-1);
        start = std::max(start, seasonStart);
    }
    return int(start.daysTo(today));
}

// How much a task needs doing: its priority, grown by how late it is in
// units of its own rhythm. A weekly task a week late counts as much as a
// monthly one a month late, and each full period late adds the whole
// priority again, so neglected chores rise without burying urgent ones.
double Store::needfulness(const Task &task, const QDate &today) const {
    if (state(task, today) != Due)
        return 0;
    static const double weights[] = {0, 1, 2, 4, 8};
    return weights[std::clamp(task.priority, 1, 4)] *
           (1.0 + double(overdueDays(task, today)) / periodDays(task.schedule));
}

QDate Store::nextDue(const Task &task, const QDate &done) const {
    if (task.schedule.kind != Schedule::Every)
        return {};
    const Schedule &s = task.schedule;
    if (s.fromDone || !task.due.isValid())
        return step(done, s.every, s.unit);
    // A fixed rhythm skips the dates already behind us, and always moves on
    // at least once, so doing it early still counts for this time.
    QDate next = step(task.due, s.every, s.unit);
    for (int i = 0; next <= done && i < 100000; ++i)
        next = step(next, s.every, s.unit);
    return next;
}

int Store::addTask(Task task, const QDate &today) {
    task.id = highestId(tasks) + 1;
    if (!task.created.isValid())
        task.created = today;
    if (!task.due.isValid())
        task.due = today;
    tasks.append(task);
    return task.id;
}

int Store::addContext(const QString &name, int parent) {
    const int id = highestId(contexts) + 1;
    contexts.append({id, name.trimmed(), context(parent) ? parent : 0});
    return id;
}

bool Store::removeContext(int id) {
    const Context *c = context(id);
    if (!c)
        return false;
    const int parent = c->parent;
    for (auto &other : contexts)
        if (other.parent == id)
            other.parent = parent;
    for (auto &t : tasks)
        if (t.context == id)
            t.context = parent;
    for (auto &done : completions)
        if (done.context == id)
            done.context = parent;
    contexts.removeIf([id](const Context &other) { return other.id == id; });
    return true;
}

bool Store::removeTask(int id) {
    return tasks.removeIf([id](const Task &t) { return t.id == id; }) > 0;
}

bool Store::complete(int id, const QDateTime &at, const QString &note, const QDate &askedNext) {
    Task *t = task(id);
    if (!t)
        return false;
    completions.append({highestId(completions) + 1, t->id, t->title, t->context, at, note.trimmed()});
    t->snoozedUntil = {};
    t->today = {};
    switch (t->schedule.kind) {
    case Schedule::Once:
        t->archived = true;
        break;
    case Schedule::Ask:
        t->archived = !askedNext.isValid();
        t->due = askedNext;
        break;
    case Schedule::Every:
        t->due = nextDue(*t, at.date());
        break;
    }
    return true;
}

bool Store::removeCompletion(int id) {
    return completions.removeIf([id](const Completion &c) { return c.id == id; }) > 0;
}
