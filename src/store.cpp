#include "store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
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

QDate stepDate(const QDate &date, int every, const QString &unit) {
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
    QJsonArray contextArray, routineArray, taskArray, completionArray, logArray, stepArray, checkArray;
    static const char *kinds[] = {"once", "every", "ask"};
    auto scheduleJson = [](const Schedule &s) {
        return QJsonObject{{"kind", kinds[s.kind]}, {"every", s.every}, {"unit", s.unit}, {"fromDone", s.fromDone}};
    };
    auto monthsJson = [](const QList<int> &list) {
        QJsonArray months;
        for (int m : list)
            months.append(m);
        return months;
    };
    for (const auto &r : routines)
        routineArray.append(QJsonObject{{"id", r.id}, {"title", r.title}, {"notes", r.notes},
                                        {"priority", r.priority}, {"schedule", scheduleJson(r.schedule)},
                                        {"months", monthsJson(r.months)}});
    for (const auto &c : contexts)
        contextArray.append(QJsonObject{{"id", c.id}, {"name", c.name}, {"parent", c.parent}});
    for (const auto &t : tasks) {
        taskArray.append(QJsonObject{
            {"id", t.id},
            {"routine", t.routine},
            {"overrides", QJsonArray::fromStringList(t.overrides)},
            {"title", t.title},
            {"notes", t.notes},
            {"context", t.context},
            {"priority", t.priority},
            {"schedule", scheduleJson(t.schedule)},
            {"months", monthsJson(t.months)},
            {"due", dateText(t.due)},
            {"snoozedUntil", dateText(t.snoozedUntil)},
            {"today", dateText(t.today)},
            {"archived", t.archived},
            {"created", dateText(t.created)},
            {"round", t.round},
        });
    }
    for (const auto &c : completions) {
        QJsonObject done{{"id", c.id},
                         {"task", c.task},
                         {"title", c.title},
                         {"context", c.context},
                         {"at", c.at.toString(Qt::ISODateWithMs)},
                         {"note", c.note},
                         {"round", c.round}};
        // Only on skips, so the many completions from before skipping stay as they were.
        if (c.skipped)
            done["skipped"] = true;
        if (!c.steps.isEmpty()) {
            QJsonArray steps;
            for (const DoneStep &s : c.steps) {
                QJsonObject step{{"text", s.text}, {"checked", s.checked}};
                if (s.once)
                    step["once"] = true;
                steps.append(step);
            }
            done["steps"] = steps;
        }
        completionArray.append(done);
    }
    for (const auto &e : log) {
        QJsonObject entry{{"id", e.id},
                          {"context", e.context},
                          {"at", e.at.toString(Qt::ISODateWithMs)},
                          {"text", e.text},
                          {"task", e.task}};
        // Only on notes about a time, so context notes stay as they were.
        if (e.about) {
            entry["about"] = e.about;
            entry["round"] = e.round;
            entry["aboutTitle"] = e.aboutTitle;
        }
        logArray.append(entry);
    }
    for (const auto &s : steps)
        stepArray.append(QJsonObject{{"id", s.id},
                                     {"routine", s.routine},
                                     {"task", s.task},
                                     {"text", s.text},
                                     {"position", s.position},
                                     {"once", s.once},
                                     {"at", s.at.toString(Qt::ISODateWithMs)},
                                     {"round", s.round}});
    for (const auto &c : checks)
        checkArray.append(QJsonObject{
            {"id", c.id}, {"task", c.task}, {"step", c.step}, {"at", c.at.toString(Qt::ISODateWithMs)}, {"round", c.round}});
    QJsonObject root{{"needful", FormatVersion},
                     {"contexts", contextArray},
                     {"routines", routineArray},
                     {"tasks", taskArray},
                     {"completions", completionArray},
                     {"log", logArray}};
    // Only once there are any, so files from before checklists stay as they were.
    if (!stepArray.isEmpty())
        root["steps"] = stepArray;
    if (!checkArray.isEmpty())
        root["checks"] = checkArray;
    if (!reserved.isEmpty()) {
        QJsonObject highest;
        for (auto it = reserved.begin(); it != reserved.end(); ++it)
            highest[it.key()] = it.value();
        root["reserved"] = highest;
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
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
    auto readSchedule = [](const QJsonObject &s) {
        Schedule schedule;
        const QString kind = s.value("kind").toString("every");
        schedule.kind = kind == "once" ? Schedule::Once : kind == "ask" ? Schedule::Ask : Schedule::Every;
        schedule.every = std::max(1, s.value("every").toInt(1));
        schedule.unit = s.value("unit").toString("week");
        schedule.fromDone = s.value("fromDone").toBool(true);
        return schedule;
    };
    auto readMonths = [](const QJsonValue &value) {
        QList<int> months;
        for (const auto &m : value.toArray())
            if (m.toInt() >= 1 && m.toInt() <= 12)
                months.append(m.toInt());
        return months;
    };
    for (const auto &value : root.value("routines").toArray()) {
        const QJsonObject o = value.toObject();
        Routine r;
        r.id = o.value("id").toInt();
        r.title = o.value("title").toString();
        r.notes = o.value("notes").toString();
        r.priority = std::clamp(o.value("priority").toInt(2), 1, 4);
        r.schedule = readSchedule(o.value("schedule").toObject());
        r.months = readMonths(o.value("months"));
        loaded.routines.append(r);
    }
    for (const auto &value : root.value("tasks").toArray()) {
        const QJsonObject o = value.toObject();
        Task t;
        t.id = o.value("id").toInt();
        t.routine = o.value("routine").toInt();
        for (const auto &field : o.value("overrides").toArray())
            t.overrides.append(field.toString());
        t.title = o.value("title").toString();
        t.notes = o.value("notes").toString();
        t.context = o.value("context").toInt();
        t.priority = std::clamp(o.value("priority").toInt(2), 1, 4);
        t.schedule = readSchedule(o.value("schedule").toObject());
        t.months = readMonths(o.value("months"));
        t.due = readDate(o.value("due"));
        t.snoozedUntil = readDate(o.value("snoozedUntil"));
        t.today = readDate(o.value("today"));
        t.archived = o.value("archived").toBool();
        t.created = readDate(o.value("created"));
        t.round = std::max(0, o.value("round").toInt());
        loaded.tasks.append(t);
    }
    QSet<int> unnumbered; // Completions from before they kept their round, by index.
    for (const auto &value : root.value("completions").toArray()) {
        const QJsonObject o = value.toObject();
        Completion c{o.value("id").toInt(), o.value("task").toInt(), o.value("title").toString(),
                     o.value("context").toInt(), QDateTime::fromString(o.value("at").toString(), Qt::ISODate),
                     o.value("note").toString(), o.value("skipped").toBool(), {}, 0};
        for (const auto &step : o.value("steps").toArray()) {
            const QJsonObject s = step.toObject();
            c.steps.append({s.value("text").toString(), s.value("checked").toBool(), s.value("once").toBool()});
        }
        if (o.contains("round"))
            c.round = o.value("round").toInt();
        else
            unnumbered.insert(loaded.completions.size());
        loaded.completions.append(c);
    }
    // Older completions count back from the task's round, newest first: the
    // one before the coming time, then the one before that. Removing one
    // shifts those older than it, which is accepted rather than repaired.
    if (!unnumbered.isEmpty()) {
        QHash<int, QList<int>> byTask;
        for (int i = 0; i < loaded.completions.size(); ++i)
            byTask[loaded.completions[i].task].append(i);
        for (auto it = byTask.begin(); it != byTask.end(); ++it) {
            QList<int> &order = it.value();
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                const Completion &ca = loaded.completions[a], &cb = loaded.completions[b];
                return ca.at != cb.at ? ca.at > cb.at : ca.id > cb.id;
            });
            const Task *t = loaded.task(it.key());
            int expected = t ? t->round - 1 : int(order.size()) - 1;
            for (int i : order) {
                Completion &c = loaded.completions[i];
                if (unnumbered.contains(i))
                    c.round = expected;
                expected = c.round - 1;
            }
        }
    }
    for (const auto &value : root.value("steps").toArray()) {
        const QJsonObject o = value.toObject();
        loaded.steps.append({o.value("id").toInt(), o.value("routine").toInt(), o.value("task").toInt(),
                             o.value("text").toString(), o.value("position").toInt(), o.value("once").toBool(),
                             QDateTime::fromString(o.value("at").toString(), Qt::ISODate), o.value("round").toInt()});
    }
    for (const auto &value : root.value("checks").toArray()) {
        const QJsonObject o = value.toObject();
        loaded.checks.append({o.value("id").toInt(), o.value("task").toInt(), o.value("step").toInt(),
                              QDateTime::fromString(o.value("at").toString(), Qt::ISODate), o.value("round").toInt()});
    }
    for (const auto &value : root.value("log").toArray()) {
        const QJsonObject o = value.toObject();
        loaded.log.append({o.value("id").toInt(), o.value("context").toInt(),
                           QDateTime::fromString(o.value("at").toString(), Qt::ISODate), o.value("text").toString(),
                           o.value("task").toInt(), o.value("about").toInt(), o.value("round").toInt(),
                           o.value("aboutTitle").toString()});
    }
    const QJsonObject highest = root.value("reserved").toObject();
    for (auto it = highest.begin(); it != highest.end(); ++it)
        loaded.reserved[it.key()] = it.value().toInt();
    *this = loaded;
    return true;
}

int Store::nextId(const QString &list) const {
    int highest = reserved.value(list);
    if (list == "contexts") highest = std::max(highest, highestId(contexts));
    else if (list == "routines") highest = std::max(highest, highestId(routines));
    else if (list == "tasks") highest = std::max(highest, highestId(tasks));
    else if (list == "completions") highest = std::max(highest, highestId(completions));
    else if (list == "log") highest = std::max(highest, highestId(log));
    else if (list == "steps") highest = std::max(highest, highestId(steps));
    else if (list == "checks") highest = std::max(highest, highestId(checks));
    return highest + 1;
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

Routine *Store::routine(int id) {
    for (auto &r : routines)
        if (id && r.id == id)
            return &r;
    return nullptr;
}

const Routine *Store::routine(int id) const { return const_cast<Store *>(this)->routine(id); }

QList<int> Store::copies(int routineId) const {
    QList<const Task *> found;
    for (const auto &t : tasks)
        if (routineId && t.routine == routineId)
            found.append(&t);
    std::sort(found.begin(), found.end(), [this](auto a, auto b) {
        return contextPath(a->context).localeAwareCompare(contextPath(b->context)) < 0;
    });
    QList<int> ids;
    for (const Task *t : found)
        ids.append(t->id);
    return ids;
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

const Completion *Store::lastDone(int taskId, bool skips) const {
    const Completion *latest = nullptr;
    for (const auto &c : completions)
        if (c.task == taskId && (skips || !c.skipped) && (!latest || c.at > latest->at))
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
        return stepDate(done, s.every, s.unit);
    // A fixed rhythm skips the dates already behind us, and always moves on
    // at least once, so doing it early still counts for this time.
    QDate next = stepDate(task.due, s.every, s.unit);
    for (int i = 0; next <= done && i < 100000; ++i)
        next = stepDate(next, s.every, s.unit);
    return next;
}

// The time skipped is the one due, or today's if it's already late: a weekly
// mow skipped early comes back a week after it was due, and one skipped late
// a week from now.
QDate Store::afterSkip(const Task &task, const QDate &today) const {
    if (task.schedule.kind != Schedule::Every)
        return {};
    const QDate from = task.due.isValid() ? std::max(task.due, today) : today;
    return task.schedule.fromDone ? stepDate(from, task.schedule.every, task.schedule.unit) : nextDue(task, from);
}

int Store::addTask(Task task, const QDate &today) {
    task.id = nextId("tasks");
    if (!task.created.isValid())
        task.created = today;
    if (!task.due.isValid())
        task.due = today;
    tasks.append(task);
    return task.id;
}

int Store::addContext(const QString &name, int parent) {
    const int id = nextId("contexts");
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
    for (auto &entry : log)
        if (entry.context == id)
            entry.context = parent;
    contexts.removeIf([id](const Context &other) { return other.id == id; });
    return true;
}

bool Store::removeTask(int id) {
    const Task *t = task(id);
    if (!t)
        return false;
    const int routineId = t->routine;
    tasks.removeIf([id](const Task &t) { return t.id == id; });
    steps.removeIf([id](const Step &s) { return s.task == id; });
    checks.removeIf([id](const Check &c) { return c.task == id; });
    if (routineId && copies(routineId).isEmpty()) {
        routines.removeIf([routineId](const Routine &r) { return r.id == routineId; });
        removeOrphanSteps();
    }
    return true;
}

int Store::makeRoutine(int taskId) {
    Task *t = task(taskId);
    if (!t)
        return 0;
    if (t->routine)
        return t->routine;
    // The task's notes become the shared notes; it starts with none of its own.
    Routine r{nextId("routines"), t->title, t->notes, t->priority, t->schedule, t->months};
    routines.append(r);
    t->routine = r.id;
    t->notes.clear();
    t->overrides.clear();
    // Its checklist becomes the shared one too; steps for this time stay its own.
    for (auto &s : steps)
        if (s.task == taskId && !s.once) {
            s.task = 0;
            s.routine = r.id;
        }
    return r.id;
}

int Store::addCopy(int routineId, int contextId, const QDate &due, const QDate &today) {
    const Routine *r = routine(routineId);
    if (!r)
        return 0;
    Task copy;
    copy.routine = routineId;
    copy.title = r->title;
    copy.context = context(contextId) ? contextId : 0;
    copy.priority = r->priority;
    copy.schedule = r->schedule;
    copy.months = r->months;
    copy.due = due;
    return addTask(copy, today);
}

void Store::syncRoutine(int routineId) {
    const Routine *r = routine(routineId);
    if (!r)
        return;
    for (auto &t : tasks) {
        if (t.routine != routineId)
            continue;
        t.title = r->title;
        if (!t.overrides.contains("schedule"))
            t.schedule = r->schedule;
        if (!t.overrides.contains("priority"))
            t.priority = r->priority;
        if (!t.overrides.contains("months"))
            t.months = r->months;
    }
}

bool Store::complete(int id, const QDateTime &at, const QString &note, const QDate &askedNext) {
    Task *t = task(id);
    if (!t)
        return false;
    completions.append({nextId("completions"), t->id, t->title, t->context, at, note.trimmed(), false, snapshot(id), t->round});
    resetChecklist(id);
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

bool Store::skip(int id, const QDateTime &at, const QString &reason, const QDate &askedNext) {
    Task *t = task(id);
    if (!t)
        return false;
    completions.append({nextId("completions"), t->id, t->title, t->context, at, reason.trimmed(), true, snapshot(id), t->round});
    resetChecklist(id);
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
        t->due = afterSkip(*t, at.date());
        break;
    }
    return true;
}

bool Store::removeCompletion(int id) {
    return completions.removeIf([id](const Completion &c) { return c.id == id; }) > 0;
}

int Store::addLog(int contextId, const QDateTime &at, const QString &text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return 0;
    const int id = nextId("log");
    log.append({id, context(contextId) ? contextId : 0, at, trimmed, 0, 0, 0, {}});
    return id;
}

int Store::addTaskNote(int taskId, const QDateTime &at, const QString &text) {
    const Task *t = task(taskId);
    const QString trimmed = text.trimmed();
    if (!t || trimmed.isEmpty())
        return 0;
    const int id = nextId("log");
    log.append({id, t->context, at, trimmed, 0, t->id, t->round, t->title});
    return id;
}

int Store::addCompletionNote(int completionId, const QDateTime &at, const QString &text) {
    const Completion *c = completion(completionId);
    const QString trimmed = text.trimmed();
    if (!c || trimmed.isEmpty())
        return 0;
    const int id = nextId("log");
    log.append({id, context(c->context) ? c->context : 0, at, trimmed, 0, c->task, c->round, c->title});
    return id;
}

QList<LogEntry> Store::notesOn(int taskId, int round) const {
    QList<LogEntry> found;
    for (const auto &e : log)
        if (taskId && e.about == taskId && e.round == round)
            found.append(e);
    std::stable_sort(found.begin(), found.end(), [](const LogEntry &a, const LogEntry &b) { return a.at < b.at; });
    return found;
}

const Completion *Store::completion(int id) const {
    for (const auto &c : completions)
        if (id && c.id == id)
            return &c;
    return nullptr;
}

// Done twice on two computers before either heard of the other leaves two
// completions of one time; the notes go with the later.
const Completion *Store::completionAt(int taskId, int round) const {
    const Completion *found = nullptr;
    for (const auto &c : completions)
        if (taskId && c.task == taskId && c.round == round && (!found || c.at > found->at))
            found = &c;
    return found;
}

bool Store::removeLog(int id) {
    return log.removeIf([id](const LogEntry &e) { return e.id == id; }) > 0;
}

const LogEntry *Store::logEntry(int id) const {
    for (const auto &e : log)
        if (e.id == id)
            return &e;
    return nullptr;
}

int Store::logForTask(int taskId) const {
    for (const auto &e : log)
        if (taskId && e.task == taskId)
            return e.id;
    return 0;
}

// The first line, cut at a word near 80 characters, so a long note still
// reads as a task in a list.
QString Store::followUpTitle(const QString &text) {
    const QString line = text.trimmed().section('\n', 0, 0).trimmed();
    constexpr int Limit = 80;
    if (line.size() <= Limit)
        return line;
    QString cut = line.left(Limit);
    const int space = cut.lastIndexOf(' ');
    if (space > Limit / 2)
        cut = cut.left(space);
    return cut.trimmed() + QString::fromUtf8("…");
}

QList<Step> Store::checklist(int taskId) const {
    const Task *t = task(taskId);
    if (!t)
        return {};
    QList<Step> shared, own, once;
    for (const Step &s : steps) {
        if (t->routine && s.routine == t->routine && !s.task)
            shared.append(s);
        else if (s.task == taskId && !s.routine && !s.once)
            own.append(s);
        else if (s.task == taskId && !s.routine && s.round == t->round)
            once.append(s);
    }
    auto byPosition = [](const Step &a, const Step &b) { return a.position != b.position ? a.position < b.position : a.id < b.id; };
    std::sort(shared.begin(), shared.end(), byPosition);
    std::sort(own.begin(), own.end(), byPosition);
    std::sort(once.begin(), once.end(), byPosition);
    return shared + own + once;
}

bool Store::checked(int taskId, int stepId) const {
    const Task *t = task(taskId);
    for (const Check &c : checks)
        if (t && c.task == taskId && c.step == stepId && c.round == t->round)
            return true;
    return false;
}

Step *Store::step(int id) {
    for (auto &s : steps)
        if (id && s.id == id)
            return &s;
    return nullptr;
}

const Step *Store::step(int id) const { return const_cast<Store *>(this)->step(id); }

int Store::addStep(int taskId, const QString &text, bool keep, const QDateTime &at) {
    const Task *t = task(taskId);
    const QString trimmed = text.trimmed();
    if (!t || trimmed.isEmpty())
        return 0;
    Step s;
    s.id = nextId("steps");
    s.text = trimmed;
    s.once = !keep;
    s.at = at;
    s.round = keep ? 0 : t->round;
    if (keep && t->routine)
        s.routine = t->routine;
    else
        s.task = taskId;
    for (const Step &other : steps)
        if (other.routine == s.routine && other.task == s.task)
            s.position = std::max(s.position, other.position + 1);
    steps.append(s);
    return s.id;
}

bool Store::setChecked(int taskId, int stepId, bool on, const QDateTime &at) {
    bool listed = false;
    for (const Step &s : checklist(taskId))
        listed = listed || s.id == stepId;
    if (!listed)
        return false;
    if (!on) {
        checks.removeIf([&](const Check &c) { return c.task == taskId && c.step == stepId; });
        return true;
    }
    if (!checked(taskId, stepId))
        checks.append({nextId("checks"), taskId, stepId, at, task(taskId)->round});
    return true;
}

bool Store::removeStep(int id) {
    if (steps.removeIf([id](const Step &s) { return s.id == id; }) == 0)
        return false;
    checks.removeIf([id](const Check &c) { return c.step == id; });
    return true;
}

void Store::setChecklist(int routineId, int taskId, const QList<std::pair<int, QString>> &entries, const QDateTime &at) {
    if (!routineId && !task(taskId))
        return;
    if (routineId)
        taskId = 0;
    auto owned = [&](const Step &s) { return s.routine == routineId && s.task == taskId && !s.once; };
    QSet<int> kept;
    int position = 0;
    for (const auto &[id, text] : entries) {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty())
            continue;
        Step *existing = step(id);
        if (existing && owned(*existing) && !kept.contains(id)) {
            existing->text = trimmed;
            existing->position = position++;
            kept.insert(id);
            continue;
        }
        Step s;
        s.id = nextId("steps");
        s.routine = routineId;
        s.task = taskId;
        s.text = trimmed;
        s.position = position++;
        s.at = at;
        steps.append(s);
        kept.insert(s.id);
    }
    QList<int> gone;
    for (const Step &s : steps)
        if (owned(s) && !kept.contains(s.id))
            gone.append(s.id);
    for (int id : gone)
        removeStep(id);
}

QList<DoneStep> Store::snapshot(int taskId) const {
    QList<DoneStep> done;
    for (const Step &s : checklist(taskId))
        done.append({s.text, checked(taskId, s.id), s.once});
    return done;
}

// The next time starts clean: no boxes ticked, and no steps for this time.
void Store::resetChecklist(int taskId) {
    if (Task *t = task(taskId))
        ++t->round;
    checks.removeIf([taskId](const Check &c) { return c.task == taskId; });
    steps.removeIf([taskId](const Step &s) { return s.task == taskId && s.once; });
}

// Steps of a routine that's gone, and checks of steps or tasks that are.
void Store::removeOrphanSteps() {
    steps.removeIf([this](const Step &s) { return s.routine ? !routine(s.routine) : !task(s.task); });
    checks.removeIf([this](const Check &c) { return !task(c.task) || !step(c.step); });
}
