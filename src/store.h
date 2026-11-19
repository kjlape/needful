#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

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

struct Task {
    int id = 0;
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

class Store {
public:
    enum State { Due, Upcoming, Snoozed, OutOfSeason, Archived };

    QList<Context> contexts;
    QList<Task> tasks;
    QList<Completion> completions;

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
    bool removeTask(int id);    // Its history stays.
    // Records the completion and schedules the next time: for Ask tasks the
    // caller's nextDue, where an invalid date retires the task.
    bool complete(int id, const QDateTime &at, const QString &note, const QDate &askedNext = {});
    bool removeCompletion(int id);

    static QString unitName(const QString &unit);

private:
    int nextId() const;
};
