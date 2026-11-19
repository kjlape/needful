#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// Everything Needful knows, and the rules that rank it. The store needs no
// display, so the window, the command line, and the tests all share it, and
// it touches nothing platform specific: the caller says where the file lives.

struct Context {
    int id = 0;
    QString name;
    int parent = 0; // 0 is the top level.
};

struct Schedule {
    // Once: done is done. Every: comes back after an interval. Ask: comes
    // back whenever the person says, picked when they finish it.
    enum Kind { Once, Every, Ask };
    Kind kind = Every;
    int every = 1;
    QString unit = "week"; // day, week, month, or year
    // From the day it was done (mow a week after the last mow), or on a fixed
    // rhythm (rent on the 1st, however late it was paid).
    bool fromDone = true;
};

// A routine shared by several contexts (one oil change for every vehicle).
// Each context gets its own Task, a copy, with its own due date, snooze, and
// history; the copies take the routine's title, and its schedule, priority,
// and season unless they override them.
struct Routine {
    int id = 0;
    QString title;
    QString notes; // Shared; each copy's own notes add to these.
    int priority = 2;
    Schedule schedule;
    QList<int> months;
};

struct Task {
    int id = 0;
    int routine = 0;         // 0 for a task of its own.
    QStringList overrides;   // For a copy: "schedule", "priority", "months".
    QString title;
    QString notes;
    int context = 0;
    int priority = 2; // 1 low, 2 normal, 3 high, 4 urgent
    Schedule schedule;
    QList<int> months; // In season only in these months (1-12); empty is all year.
    QDate due;
    QDate snoozedUntil; // Hidden until this day.
    QDate today;        // Pulled into this day's list.
    bool archived = false;
    QDate created;
};

// A record of doing a task. It keeps the task's title and context as they
// were, so history survives renaming, moving, or deleting the task.
struct Completion {
    int id = 0;
    int task = 0;
    QString title;
    int context = 0;
    QDateTime at;
    QString note;
};

// A note on a context: a squeak, a leak, an idea. It sits in the history
// beside what was done, and can raise a one-off task to follow it up.
struct LogEntry {
    int id = 0;
    int context = 0;
    QDateTime at;
    QString text;
    int task = 0; // The follow-up task, or 0.
};

class Store {
public:
    enum State { Due, Upcoming, Snoozed, OutOfSeason, Archived };

    QList<Context> contexts;
    QList<Routine> routines;
    QList<Task> tasks;
    QList<Completion> completions;
    QList<LogEntry> log;

    // Reading and writing. Saving writes beside the file and renames, so a
    // crash never leaves half a file, and keeps the previous one as .bak.
    bool load(const QString &path, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr) const;
    QByteArray toJson() const;
    bool fromJson(const QByteArray &json, QString *error = nullptr);

    // Lookups.
    Task *task(int id);
    const Task *task(int id) const;
    const Context *context(int id) const;
    Routine *routine(int id);
    const Routine *routine(int id) const;
    QList<int> copies(int routineId) const; // Task ids, in context order.
    QString contextPath(int id) const;          // "Automotive › White Honda CR-V"
    QList<int> withDescendants(int id) const;   // The context and everything under it.
    bool inContext(int contextId, int filter) const;
    int findContext(const QString &pathOrName) const; // Case-insensitive; "A/B" or "B".
    QList<Completion> history(int taskId) const;      // Newest first.
    const Completion *lastDone(int taskId) const;

    // The ranking.
    static bool inSeason(const Task &task, const QDate &day);
    static int periodDays(const Schedule &schedule);
    State state(const Task &task, const QDate &today) const;
    int overdueDays(const Task &task, const QDate &today) const;
    double needfulness(const Task &task, const QDate &today) const;
    QDate nextDue(const Task &task, const QDate &done) const; // Invalid for Once and Ask.

    // Changes. Each returns false when the id is unknown.
    int addTask(Task task, const QDate &today);
    int addContext(const QString &name, int parent);
    bool removeContext(int id); // Its tasks and children move up to its parent.
    bool removeTask(int id);    // Its history stays; a routine goes with its last copy.

    // Routines. Making one turns the task into its first copy, keeping its
    // history and due date, and adds a copy for each other context.
    int makeRoutine(int taskId);
    int addCopy(int routineId, int context, const QDate &due, const QDate &today);
    // Pushes the routine's title and shared fields to its copies, except
    // where a copy overrides them.
    void syncRoutine(int routineId);
    // Records the completion and schedules the next time: for Ask tasks the
    // caller's nextDue, where an invalid date retires the task.
    bool complete(int id, const QDateTime &at, const QString &note, const QDate &askedNext = {});
    bool removeCompletion(int id);
    // Notes. A follow-up becomes a one-time task in the same context.
    int addLog(int context, const QDateTime &at, const QString &text);
    bool removeLog(int id);
    const LogEntry *logEntry(int id) const;
    int logForTask(int taskId) const; // The entry a follow-up task came from, or 0.
    static QString followUpTitle(const QString &text);

    static QString unitName(const QString &unit);

private:
    int nextId() const;
};
