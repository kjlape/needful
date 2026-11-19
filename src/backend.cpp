#include "backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>
#include <algorithm>
#include <functional>

namespace {
constexpr int UndoLimit = 200;

QString iso(const QDate &date) { return date.isValid() ? date.toString(Qt::ISODate) : QString(); }

Schedule::Kind kindFrom(const QString &text) {
    return text == "once" ? Schedule::Once : text == "ask" ? Schedule::Ask : Schedule::Every;
}

QString kindName(Schedule::Kind kind) {
    return kind == Schedule::Once ? "once" : kind == Schedule::Ask ? "ask" : "every";
}

QString stateName(Store::State state) {
    switch (state) {
    case Store::Due: return "due";
    case Store::Upcoming: return "upcoming";
    case Store::Snoozed: return "snoozed";
    case Store::OutOfSeason: return "season";
    case Store::Archived: return "archived";
    }
    return {};
}
}

Backend::Backend(const QString &dataPath, QObject *parent)
    : QObject(parent), m_path(dataPath.isEmpty() ? defaultDataPath() : dataPath) {
    QString message;
    if (!m_store.load(m_path, &message))
        setError(message);
    if (QFile file(m_path); file.open(QIODevice::ReadOnly))
        m_written = file.readAll();

    // The command line, or another machine syncing the folder, can change the
    // file while the window is open; pick that up rather than overwrite it.
    m_reloadDelay.setSingleShot(true);
    m_reloadDelay.setInterval(150);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_reloadDelay.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_reloadDelay.start(); });
    connect(&m_reloadDelay, &QTimer::timeout, this, &Backend::reloadFromDisk);
    watch();

    // Past midnight, what's due and what's on today's list both change.
    m_clock.setInterval(60 * 1000);
    connect(&m_clock, &QTimer::timeout, this, [this] {
        if (today() != m_shownDay)
            refresh();
    });
    m_clock.start();
    refresh();
}

QString Backend::defaultDataPath() {
    const QString override = qEnvironmentVariable("NEEDFUL_DATA");
    if (!override.isEmpty())
        return override;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/needful.json";
}

void Backend::watch() {
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);
    const QFileInfo info(m_path);
    if (info.absoluteDir().exists())
        m_watcher.addPath(info.absolutePath());
    if (info.exists())
        m_watcher.addPath(m_path);
}

void Backend::reloadFromDisk() {
    watch();
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray contents = file.readAll();
    if (contents == m_written)
        return;
    Store loaded;
    QString message;
    if (!loaded.fromJson(contents, &message)) {
        setError(message);
        return;
    }
    // Undo steps were taken against the old file, so they no longer apply.
    m_store = loaded;
    m_written = contents;
    m_undo.clear();
    m_redo.clear();
    refresh();
}

void Backend::setError(const QString &message) {
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}

void Backend::dismissError() { setError({}); }

// Every change goes through here: remember the state before it for undo,
// apply it, and save at once, so nothing is ever waiting to be saved.
template <typename Change> bool Backend::change(Change apply) {
    const QByteArray before = m_store.toJson();
    if (!apply())
        return false;
    const QByteArray after = m_store.toJson();
    if (after == before)
        return false;
    QString message;
    if (!m_store.save(m_path, &message)) {
        // Saving failed, so undo the change in memory too: the window must
        // never show something that isn't on disk.
        m_store.fromJson(before);
        setError(message);
        refresh();
        return false;
    }
    m_written = after;
    m_undo.append(before);
    if (m_undo.size() > UndoLimit)
        m_undo.removeFirst();
    m_redo.clear();
    watch();
    setError({});
    refresh();
    return true;
}

void Backend::restore(const QByteArray &json) {
    QString message;
    m_store.fromJson(json);
    if (!m_store.save(m_path, &message))
        setError(message);
    m_written = m_store.toJson();
    watch();
    refresh();
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_store.toJson());
    restore(m_undo.takeLast());
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_store.toJson());
    restore(m_redo.takeLast());
}

void Backend::setToday(const QDate &day) {
    m_fixedToday = day;
    refresh();
}

void Backend::setView(const QString &view) {
    if (view == m_view)
        return;
    m_view = view;
    emit viewChanged();
    refresh();
}

void Backend::setContextFilter(int id) {
    if (id == m_contextFilter)
        return;
    m_contextFilter = m_store.context(id) ? id : 0;
    emit viewChanged();
    refresh();
}

void Backend::setSearch(const QString &search) {
    if (search == m_search)
        return;
    m_search = search;
    emit viewChanged();
    refresh();
}

bool Backend::matches(const QString &text) const {
    return m_search.trimmed().isEmpty() || text.contains(m_search.trimmed(), Qt::CaseInsensitive);
}

QString Backend::todayText() const {
    return QLocale().toString(today(), QLocale::LongFormat);
}

void Backend::refresh() {
    const QDate day = today();
    m_shownDay = day;

    QList<const Task *> todays, needful, upcoming;
    for (const auto &t : m_store.tasks) {
        if (!m_store.inContext(t.context, m_contextFilter))
            continue;
        const Store::State state = m_store.state(t, day);
        if (state == Store::Archived)
            continue;
        if (t.today == day)
            todays.append(&t);
        else if (state == Store::Due)
            needful.append(&t);
        else
            upcoming.append(&t);
    }

    QList<const Completion *> history;
    for (const auto &c : m_store.completions)
        if (m_store.inContext(c.context, m_contextFilter))
            history.append(&c);
    std::sort(history.begin(), history.end(), [](auto a, auto b) { return a->at > b->at; });

    auto byNeed = [&](const Task *a, const Task *b) {
        const double na = m_store.needfulness(*a, day), nb = m_store.needfulness(*b, day);
        return na != nb ? na > nb : a->title.localeAwareCompare(b->title) < 0;
    };
    std::sort(todays.begin(), todays.end(), byNeed);
    std::sort(needful.begin(), needful.end(), byNeed);
    std::sort(upcoming.begin(), upcoming.end(), [&](const Task *a, const Task *b) {
        const QDate ra = returnsOn(*a, day), rb = returnsOn(*b, day);
        return ra != rb ? ra < rb : a->title.localeAwareCompare(b->title) < 0;
    });

    QList<const LogEntry *> notes;
    for (const auto &e : m_store.log)
        if (m_store.inContext(e.context, m_contextFilter))
            notes.append(&e);

    m_items.clear();
    if (m_view == "history") {
        // What was done and what was noticed, interleaved, newest first.
        QList<std::pair<QDateTime, QVariantMap>> entries;
        for (const Completion *c : history)
            if (matches(c->title + "\n" + c->note + "\n" + m_store.contextPath(c->context)))
                entries.append({c->at, describe(*c)});
        for (const LogEntry *e : notes)
            if (matches(e->text + "\n" + m_store.contextPath(e->context)))
                entries.append({e->at, describe(*e)});
        std::stable_sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (const auto &entry : entries)
            m_items.append(entry.second);
    } else {
        const auto &list = m_view == "today" ? todays : m_view == "upcoming" ? upcoming : needful;
        for (const Task *t : list)
            if (matches(t->title + "\n" + t->notes + "\n" + m_store.contextPath(t->context)))
                m_items.append(describe(*t, day));
    }

    m_doneToday.clear();
    for (const Completion *c : history)
        if (c->at.date() == day)
            m_doneToday.append(describe(*c));

    m_counts = {{"today", int(todays.size())},
                {"needful", int(needful.size())},
                {"upcoming", int(upcoming.size())},
                {"history", int(history.size() + notes.size())},
                {"doneToday", int(m_doneToday.size())}};

    // Contexts in tree order, each with how much is due in it and under it.
    m_contexts.clear();
    std::function<void(int, int)> walk = [&](int parent, int depth) {
        QList<const Context *> children;
        for (const auto &c : m_store.contexts)
            if (c.parent == parent)
                children.append(&c);
        std::sort(children.begin(), children.end(),
                  [](auto a, auto b) { return a->name.localeAwareCompare(b->name) < 0; });
        for (const Context *c : children) {
            const QList<int> within = m_store.withDescendants(c->id);
            int due = 0;
            for (const auto &t : m_store.tasks)
                if (within.contains(t.context) && m_store.state(t, day) == Store::Due)
                    ++due;
            m_contexts.append(QVariantMap{{"id", c->id},
                                          {"name", c->name},
                                          {"parent", c->parent},
                                          {"path", m_store.contextPath(c->id)},
                                          {"depth", depth},
                                          {"due", due}});
            if (depth < 16)
                walk(c->id, depth + 1);
        }
    };
    walk(0, 0);

    emit changed();
}

QDate Backend::returnsOn(const Task &task, const QDate &today) const {
    switch (m_store.state(task, today)) {
    case Store::Snoozed:
        return task.snoozedUntil;
    case Store::OutOfSeason: {
        QDate day = today;
        for (int i = 0; i < 366 && !Store::inSeason(task, day); ++i)
            day = day.addDays(1);
        return std::max(day, task.due);
    }
    default:
        return task.due.isValid() ? task.due : today;
    }
}

QString Backend::agoText(const QDate &day, const QDate &today) const {
    const qint64 days = day.daysTo(today);
    if (days <= 0) return tr("today");
    if (days == 1) return tr("yesterday");
    if (days < 60) return tr("%n days ago", "", int(days));
    if (days < 730) return tr("%n months ago", "", int(days / 30));
    return tr("%n years ago", "", int(days / 365));
}

QString Backend::dueText(const Task &task, const QDate &today) const {
    const QLocale locale;
    switch (m_store.state(task, today)) {
    case Store::Due: {
        const int late = m_store.overdueDays(task, today);
        return late == 0 ? tr("Due today") : tr("%n days late", "", late);
    }
    case Store::Snoozed:
        return tr("Snoozed until %1").arg(locale.toString(task.snoozedUntil, QLocale::ShortFormat));
    case Store::OutOfSeason:
        return tr("Back in season %1").arg(locale.toString(returnsOn(task, today), "MMMM"));
    case Store::Archived:
        return tr("Done");
    case Store::Upcoming: {
        const qint64 days = today.daysTo(task.due);
        if (days == 1) return tr("Due tomorrow");
        if (days < 14) return tr("Due in %n days", "", int(days));
        return tr("Due %1").arg(locale.toString(task.due, QLocale::ShortFormat));
    }
    }
    return {};
}

QString Backend::scheduleText(const Task &task) const {
    const Schedule &s = task.schedule;
    if (s.kind == Schedule::Once) return tr("One time");
    if (s.kind == Schedule::Ask) return tr("When you choose");
    if (s.unit == "day") return tr("Every %n days", "", s.every);
    if (s.unit == "month") return tr("Every %n months", "", s.every);
    if (s.unit == "year") return tr("Every %n years", "", s.every);
    return tr("Every %n weeks", "", s.every);
}

QString Backend::seasonText(const Task &task) const {
    if (task.months.isEmpty() || task.months.size() == 12)
        return {};
    // Runs of months read as ranges: May – September, or November – February.
    QList<int> months = task.months;
    std::sort(months.begin(), months.end());
    const QLocale locale;
    QStringList parts;
    QList<bool> in(13, false);
    for (int m : months)
        in[m] = true;
    // Start each run at a month whose previous month is out, wrapping December.
    for (int m = 1; m <= 12; ++m) {
        const int previous = m == 1 ? 12 : m - 1;
        if (!in[m] || in[previous])
            continue;
        int end = m;
        while (in[end % 12 + 1] && end % 12 + 1 != m)
            end = end % 12 + 1;
        parts.append(end == m ? locale.standaloneMonthName(m, QLocale::ShortFormat)
                              : locale.standaloneMonthName(m, QLocale::ShortFormat) + QString::fromUtf8(" – ") +
                                    locale.standaloneMonthName(end, QLocale::ShortFormat));
    }
    return parts.join(", ");
}

QVariantMap Backend::describe(const Task &t, const QDate &today) const {
    QVariantList months;
    for (int m : t.months)
        months.append(m);
    const Completion *last = m_store.lastDone(t.id);
    const Store::State state = m_store.state(t, today);
    // A routine's other copies, so the window can say where else it applies.
    const Routine *routine = m_store.routine(t.routine);
    QVariantList copies, contexts{t.context};
    for (int copyId : m_store.copies(t.routine)) {
        const Task *copy = m_store.task(copyId);
        if (copyId != t.id) {
            copies.append(QVariantMap{{"id", copyId}, {"contextPath", m_store.contextPath(copy->context)},
                                      {"dueText", dueText(*copy, today)}});
            contexts.append(copy->context);
        }
    }
    return {{"kind", "task"},
            {"id", t.id},
            {"title", t.title},
            {"notes", t.notes},
            {"sharedNotes", routine ? routine->notes : QString()},
            {"routine", t.routine},
            {"overrides", t.overrides},
            {"copies", copies},
            {"contexts", contexts},
            {"context", t.context},
            {"contextPath", m_store.contextPath(t.context)},
            {"priority", t.priority},
            {"scheduleKind", kindName(t.schedule.kind)},
            {"every", t.schedule.every},
            {"unit", t.schedule.unit},
            {"fromDone", t.schedule.fromDone},
            {"months", months},
            {"due", iso(t.due)},
            {"snoozedUntil", iso(t.snoozedUntil)},
            {"today", t.today == today},
            {"state", stateName(state)},
            {"score", m_store.needfulness(t, today)},
            {"overdue", m_store.overdueDays(t, today)},
            {"dueText", dueText(t, today)},
            {"scheduleText", scheduleText(t)},
            {"seasonText", seasonText(t)},
            {"lastDone", last ? tr("Last done %1").arg(agoText(last->at.date(), today)) : tr("Never done")}};
}

QVariantMap Backend::describe(const Completion &c) const {
    const QLocale locale;
    return {{"kind", "completion"},
            {"id", c.id},
            {"task", c.task},
            {"title", c.title},
            {"note", c.note},
            {"contextPath", m_store.contextPath(c.context)},
            {"date", locale.toString(c.at.date(), QLocale::ShortFormat)},
            {"time", locale.toString(c.at.time(), QLocale::ShortFormat)},
            {"ago", agoText(c.at.date(), today())},
            {"exists", m_store.task(c.task) != nullptr},
            {"fromLog", m_store.logForTask(c.task)}};
}

QVariantMap Backend::describe(const LogEntry &e) const {
    const QLocale locale;
    const Task *followUp = m_store.task(e.task);
    const bool done = followUp ? followUp->archived || m_store.lastDone(followUp->id) : e.task != 0;
    return {{"kind", "log"},
            {"id", e.id},
            {"text", e.text},
            {"contextPath", m_store.contextPath(e.context)},
            {"date", locale.toString(e.at.date(), QLocale::ShortFormat)},
            {"time", locale.toString(e.at.time(), QLocale::ShortFormat)},
            {"ago", agoText(e.at.date(), today())},
            {"task", e.task},
            {"followUpTitle", followUp ? followUp->title : QString()},
            {"followUpDone", e.task != 0 && done},
            {"followUpDueText", followUp && !done ? dueText(*followUp, today()) : QString()}};
}

QVariantMap Backend::task(int id) const {
    const Task *t = m_store.task(id);
    if (!t)
        return {};
    QVariantMap map = describe(*t, today());
    QVariantList history;
    for (const auto &c : m_store.history(id))
        history.append(describe(c));
    map["history"] = history;
    if (const Routine *r = m_store.routine(t->routine)) {
        QVariantList months;
        for (int m : r->months)
            months.append(m);
        map["shared"] = QVariantMap{{"title", r->title},
                                    {"notes", r->notes},
                                    {"priority", r->priority},
                                    {"scheduleKind", kindName(r->schedule.kind)},
                                    {"every", r->schedule.every},
                                    {"unit", r->schedule.unit},
                                    {"fromDone", r->schedule.fromDone},
                                    {"months", months}};
    }
    return map;
}

namespace {
// The editable fields of a task or routine, read from the editor's map.
struct Fields {
    int priority = 2;
    Schedule schedule;
    QList<int> months;
};

Fields readFields(const QVariantMap &fields) {
    Fields f;
    f.priority = std::clamp(fields.value("priority", 2).toInt(), 1, 4);
    f.schedule.kind = kindFrom(fields.value("scheduleKind").toString());
    f.schedule.every = std::max(1, fields.value("every", 1).toInt());
    const QString unit = fields.value("unit").toString();
    f.schedule.unit = QStringList{"day", "week", "month", "year"}.contains(unit) ? unit : "week";
    f.schedule.fromDone = fields.value("fromDone", true).toBool();
    for (const auto &m : fields.value("months").toList())
        if (m.toInt() >= 1 && m.toInt() <= 12 && !f.months.contains(m.toInt()))
            f.months.append(m.toInt());
    std::sort(f.months.begin(), f.months.end());
    if (f.months.size() == 12)
        f.months.clear();
    return f;
}

bool sameSchedule(const Schedule &a, const Schedule &b) {
    return a.kind == b.kind && a.every == b.every && a.unit == b.unit && a.fromDone == b.fromDone;
}

QList<int> sorted(QList<int> list) {
    std::sort(list.begin(), list.end());
    return list;
}
}

// One save for every way the editor is used. "contexts" lists the contexts
// ticked; more than one makes a shared routine with a copy in each. For a
// copy, "scope" says whether the edit is to the routine ("all") or to this
// copy alone ("only"), where any field that differs from the routine becomes
// an override.
int Backend::saveTask(const QVariantMap &fields) {
    const QString title = fields.value("title").toString().trimmed();
    if (title.isEmpty())
        return 0;
    int id = fields.value("id").toInt();
    const Fields f = readFields(fields);
    const QString notes = fields.value("notes").toString().trimmed();
    const QDate due = QDate::fromString(fields.value("due").toString(), Qt::ISODate);
    QList<int> contexts;
    for (const auto &value : fields.value("contexts").toList())
        if (m_store.context(value.toInt()) && !contexts.contains(value.toInt()))
            contexts.append(value.toInt());
    // Older callers name one context.
    if (contexts.isEmpty() && m_store.context(fields.value("context").toInt()))
        contexts.append(fields.value("context").toInt());

    change([&] {
        Task *existing = id ? m_store.task(id) : nullptr;
        if (id && !existing)
            return false;

        // A copy edited on its own: overrides are whatever differs.
        if (existing && existing->routine && fields.value("scope").toString() == "only") {
            const Routine *r = m_store.routine(existing->routine);
            existing->priority = f.priority;
            existing->schedule = f.schedule;
            existing->months = f.months;
            existing->notes = notes;
            existing->overrides.clear();
            if (!sameSchedule(f.schedule, r->schedule)) existing->overrides.append("schedule");
            if (f.priority != r->priority) existing->overrides.append("priority");
            if (sorted(f.months) != sorted(r->months)) existing->overrides.append("months");
            if (due.isValid())
                existing->due = due;
            return true;
        }

        // The whole routine: shared fields, and which contexts have a copy.
        if (existing && existing->routine) {
            const int routineId = existing->routine;
            Routine *r = m_store.routine(routineId);
            r->title = title;
            r->notes = notes;
            r->priority = f.priority;
            r->schedule = f.schedule;
            r->months = f.months;
            if (!contexts.isEmpty()) {
                // Add before removing, so the routine never runs out of copies.
                const QList<int> before = m_store.copies(routineId);
                QList<int> have;
                for (int copyId : before)
                    have.append(m_store.task(copyId)->context);
                for (int contextId : contexts)
                    if (!have.contains(contextId))
                        m_store.addCopy(routineId, contextId, due.isValid() ? due : today(), today());
                for (int copyId : before)
                    if (!contexts.contains(m_store.task(copyId)->context))
                        m_store.removeTask(copyId); // Its history stays.
            }
            m_store.syncRoutine(routineId);
            if (!m_store.task(id)) {
                const QList<int> left = m_store.copies(routineId);
                id = left.isEmpty() ? 0 : left.first();
            }
            return true;
        }

        // A task of its own, new or not; more than one context makes it a routine.
        Task draft = existing ? *existing : Task();
        draft.title = title;
        draft.notes = notes;
        draft.priority = f.priority;
        draft.schedule = f.schedule;
        draft.months = f.months;
        if (due.isValid())
            draft.due = due;
        if (contexts.isEmpty())
            draft.context = 0;
        else if (!contexts.contains(draft.context) || !existing)
            draft.context = contexts.first();
        if (existing)
            *existing = draft;
        else
            id = m_store.addTask(draft, today());

        if (contexts.size() > 1) {
            const int routineId = m_store.makeRoutine(id);
            const int own = m_store.task(id)->context;
            for (int contextId : contexts)
                if (contextId != own)
                    m_store.addCopy(routineId, contextId, due.isValid() ? due : today(), today());
        }
        return true;
    });
    return id;
}

void Backend::removeTask(int id) {
    change([&] { return m_store.removeTask(id); });
}

void Backend::complete(int id, const QString &note, const QString &nextDue) {
    change([&] {
        const QDateTime at = now();
        return m_store.complete(id, at, note, QDate::fromString(nextDue, Qt::ISODate));
    });
}

void Backend::snooze(int id, int days) {
    change([&] {
        Task *t = m_store.task(id);
        if (!t || days < 1)
            return false;
        t->snoozedUntil = today().addDays(days);
        t->today = {};
        return true;
    });
}

void Backend::unsnooze(int id) {
    change([&] {
        Task *t = m_store.task(id);
        if (!t)
            return false;
        t->snoozedUntil = {};
        return true;
    });
}

void Backend::toggleToday(int id) {
    change([&] {
        Task *t = m_store.task(id);
        if (!t)
            return false;
        t->today = t->today == today() ? QDate() : today();
        // Pulling a snoozed task in means it's wanted now after all.
        if (t->today.isValid())
            t->snoozedUntil = {};
        return true;
    });
}

int Backend::addContext(const QString &name, int parent) {
    if (name.trimmed().isEmpty())
        return 0;
    int id = 0;
    change([&] {
        id = m_store.addContext(name, parent);
        return true;
    });
    return id;
}

void Backend::renameContext(int id, const QString &name) {
    change([&] {
        for (auto &c : m_store.contexts)
            if (c.id == id && !name.trimmed().isEmpty()) {
                c.name = name.trimmed();
                return true;
            }
        return false;
    });
}

void Backend::removeContext(int id) {
    if (change([&] { return m_store.removeContext(id); }) && !m_store.context(m_contextFilter))
        setContextFilter(0);
}

int Backend::addLog(const QVariantMap &fields) {
    const QString text = fields.value("text").toString().trimmed();
    if (text.isEmpty())
        return 0;
    int id = 0;
    // The note and its follow-up are one change, so one undo takes back both.
    change([&] {
        const QDateTime at = now();
        id = m_store.addLog(fields.value("context").toInt(), at, text);
        if (!id)
            return false;
        if (fields.value("followUp").toBool()) {
            Task task;
            task.title = Store::followUpTitle(text);
            task.notes = text != task.title ? text : QString();
            task.context = m_store.log.last().context;
            task.priority = std::clamp(fields.value("priority", 2).toInt(), 1, 4);
            task.schedule.kind = Schedule::Once;
            task.due = QDate::fromString(fields.value("due").toString(), Qt::ISODate);
            const int taskId = m_store.addTask(task, today());
            for (auto &e : m_store.log)
                if (e.id == id)
                    e.task = taskId;
        }
        return true;
    });
    return id;
}

void Backend::removeLog(int id) {
    change([&] { return m_store.removeLog(id); });
}

void Backend::removeCompletion(int id) {
    change([&] { return m_store.removeCompletion(id); });
}

QString Backend::dateAfter(int amount, const QString &unit) const {
    const QDate day = today();
    if (unit == "day") return iso(day.addDays(amount));
    if (unit == "month") return iso(day.addMonths(amount));
    if (unit == "year") return iso(day.addYears(amount));
    return iso(day.addDays(7 * amount));
}

QString Backend::formatDate(const QString &text) const {
    const QDate date = QDate::fromString(text, Qt::ISODate);
    return date.isValid() ? QLocale().toString(date, QLocale::LongFormat) : QString();
}

// For a task that asks when to come back, start from the gap it had last
// time, so a rhythm emerges without being set.
QString Backend::suggestedNext(int id) const {
    const QList<Completion> history = m_store.history(id);
    if (history.size() >= 2) {
        const qint64 gap = history[1].at.date().daysTo(history[0].at.date());
        if (gap > 0)
            return iso(today().addDays(gap));
    }
    return dateAfter(1, "month");
}
