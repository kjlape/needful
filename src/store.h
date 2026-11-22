#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <utility>

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
    // How many times it's been done or skipped. Checks and steps for this
    // time only belong to one time, so a box ticked on another computer
    // before it heard the task was done falls to the reset.
    int round = 0;
};

// One step of a task's checklist. A routine's steps belong to the routine
// and every copy shares them; a task's own come after those. A step for this
// time only goes away when the task is next done or skipped.
struct Step {
    int id = 0;
    int routine = 0; // Shared by the routine's copies, or 0.
    int task = 0;    // This task's own, or 0.
    QString text;
    int position = 0;
    bool once = false; // For this time only.
    QDateTime at;      // When it was added.
    int round = 0;     // The task's time it's for, when once.
};

// A ticked box: this step, on this task, this time. Unticking removes it.
struct Check {
    int id = 0;
    int task = 0;
    int step = 0;
    QDateTime at;
    int round = 0; // The task's time it was ticked in.
};

// A step as it was when the task was done or skipped, so later changes to
// the checklist don't rewrite the past.
struct DoneStep {
    QString text;
    bool checked = false;
    bool once = false;
};

// A record of doing a task, or of skipping one time of it. It keeps the
// task's title and context as they were, so history survives renaming,
// moving, or deleting the task.
struct Completion {
    int id = 0;
    int task = 0;
    QString title;
    int context = 0;
    QDateTime at;
    QString note;          // For a skip, why.
    bool skipped = false;  // Passed over this time, not done.
    QList<DoneStep> steps; // The checklist as it stood.
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
    QList<Step> steps;
    QList<Check> checks;
    // The highest id ever used in each list ("tasks", "log", ...), counting
    // records since deleted, so a new one never takes a deleted one's id.
    // Only a synced store keeps them; on one file it stays empty.
    QMap<QString, int> reserved;

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
    // The last time it was done; with skips, the last time it was done or skipped.
    const Completion *lastDone(int taskId, bool skips = false) const;

    // The ranking.
    static bool inSeason(const Task &task, const QDate &day);
    static int periodDays(const Schedule &schedule);
    State state(const Task &task, const QDate &today) const;
    int overdueDays(const Task &task, const QDate &today) const;
    double needfulness(const Task &task, const QDate &today) const;
    QDate nextDue(const Task &task, const QDate &done) const; // Invalid for Once and Ask.
    QDate afterSkip(const Task &task, const QDate &today) const; // Likewise.

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
    // Passes over the time it's next due without doing it: the history says
    // it was skipped, and why, and it moves on to the time after. A one-time
    // task retires; for Ask tasks, as with complete.
    bool skip(int id, const QDateTime &at, const QString &reason, const QDate &askedNext = {});
    bool removeCompletion(int id);
    // Notes. A follow-up becomes a one-time task in the same context.
    int addLog(int context, const QDateTime &at, const QString &text);
    bool removeLog(int id);
    const LogEntry *logEntry(int id) const;
    int logForTask(int taskId) const; // The entry a follow-up task came from, or 0.
    static QString followUpTitle(const QString &text);

    // Checklists. A task's steps this time: its routine's, then its own,
    // then those for this time only.
    QList<Step> checklist(int taskId) const;
    bool checked(int taskId, int stepId) const;
    Step *step(int id);
    const Step *step(int id) const;
    // Adds a step for this time only, or with keep one that stays: on a
    // routine's copy, to the routine, which every copy shares. Returns its id.
    int addStep(int taskId, const QString &text, bool keep, const QDateTime &at);
    bool setChecked(int taskId, int stepId, bool checked, const QDateTime &at);
    bool removeStep(int id); // And its checks.
    // Replaces the steps that stay on a routine (routineId) or a task
    // (taskId) with `texts`, in order. An entry's id keeps that step, and its
    // checks; 0 makes a new one. Steps left out go, with their checks.
    void setChecklist(int routineId, int taskId, const QList<std::pair<int, QString>> &entries, const QDateTime &at);

    static QString unitName(const QString &unit);
    // The id the next record in a list gets: "contexts", "routines", "tasks",
    // "completions", "log", "steps", or "checks".
    int nextId(const QString &list) const;

private:
    QList<DoneStep> snapshot(int taskId) const;
    void resetChecklist(int taskId);
    void removeOrphanSteps();
};
