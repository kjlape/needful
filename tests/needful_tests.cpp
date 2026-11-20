#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QRegularExpression>

#include "backend.h"
#include "httpserver.h"
#include "serve.h"
#include "store.h"
#include "sync.h"
#include "theme.h"
#include "translator.h"
#include "universes.h"
#include "cli.h"
#include "devices.h"
#include "peers.h"
#include "webapi.h"

namespace {
Task weekly(const QString &title, int priority, const QDate &due) {
    Task t;
    t.title = title;
    t.priority = priority;
    t.schedule.kind = Schedule::Every;
    t.schedule.every = 1;
    t.schedule.unit = "week";
    t.due = due;
    return t;
}

void write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

QByteArray contentsOf(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// A sync tool between two folders that runs only when told to: each
// machine's file goes wherever an older copy of it is.
void copyNewer(const QString &from, const QString &to) {
    const QDir source(from + "/machines");
    QDir().mkpath(to + "/machines");
    for (const QString &file : source.entryList({"*.json"}, QDir::Files)) {
        const QString target = to + "/machines/" + file;
        const QString clock = QJsonDocument::fromJson(contentsOf(source.filePath(file))).object().value("clock").toString();
        const QString there = QJsonDocument::fromJson(contentsOf(target)).object().value("clock").toString();
        if (QFile::exists(target) && there >= clock)
            continue;
        QFile::remove(target);
        QFile::copy(source.filePath(file), target);
    }
}
void syncTool(const QString &a, const QString &b) {
    copyNewer(a, b);
    copyNewer(b, a);
}

QStringList titles(const Store &store) {
    QStringList found;
    for (const auto &t : store.tasks)
        found.append(t.title);
    found.sort();
    return found;
}

int taskNamed(const Store &store, const QString &title) {
    for (const auto &t : store.tasks)
        if (t.title == title)
            return t.id;
    return 0;
}
}

class NeedfulTests : public QObject {
    Q_OBJECT

    const QDate saturday{2026, 7, 4};

private slots:
    // The example that shaped the app: on Saturday the weekly mowing outranks
    // the monthly air filter even though the filter is weeks late, and the
    // filter climbs as it's left longer.
    void priorityAndLatenessRank()
    {
        Store store;
        const int mow = store.addTask(weekly("Mow", 3, saturday), saturday);
        Task filter = weekly("Air filter", 2, saturday.addDays(-21));
        filter.schedule.unit = "month";
        const int air = store.addTask(filter, saturday);
        QVERIFY(store.needfulness(*store.task(mow), saturday) > store.needfulness(*store.task(air), saturday));
        QVERIFY(store.needfulness(*store.task(air), saturday.addDays(60)) >
                store.needfulness(*store.task(mow), saturday));
        QCOMPARE(store.state(*store.task(mow), saturday.addDays(-1)), Store::Upcoming);
    }

    void outOfSeasonWaitsAndForgetsTheWinter()
    {
        Store store;
        Task mow = weekly("Mow", 3, QDate(2026, 10, 3));
        mow.months = {5, 6, 7, 8, 9};
        const int id = store.addTask(mow, QDate(2026, 10, 3));
        QCOMPARE(store.state(*store.task(id), QDate(2026, 10, 10)), Store::OutOfSeason);
        QCOMPARE(store.needfulness(*store.task(id), QDate(2027, 1, 1)), 0.0);
        // On May 3rd it is due, but only two days late (since May 1st), not seven months.
        QCOMPARE(store.state(*store.task(id), QDate(2027, 5, 3)), Store::Due);
        QCOMPARE(store.overdueDays(*store.task(id), QDate(2027, 5, 3)), 2);
    }

    void completingSchedulesTheNextTime()
    {
        Store store;
        const int id = store.addTask(weekly("Mow", 3, saturday), saturday);
        QVERIFY(store.complete(id, QDateTime(saturday.addDays(1), QTime(9, 0)), "Edged too"));
        QCOMPARE(store.task(id)->due, saturday.addDays(8));
        QCOMPARE(store.history(id).size(), 1);
        QCOMPARE(store.lastDone(id)->note, QString("Edged too"));

        Task rent = weekly("Rent", 4, QDate(2026, 7, 1));
        rent.schedule.unit = "month";
        rent.schedule.fromDone = false;
        const int rentId = store.addTask(rent, saturday);
        store.complete(rentId, QDateTime(QDate(2026, 7, 9), QTime(9, 0)), {});
        QCOMPARE(store.task(rentId)->due, QDate(2026, 8, 1));
    }

    void askingAndOnceTasks()
    {
        Store store;
        Task ask = weekly("Wax the car", 2, saturday);
        ask.schedule.kind = Schedule::Ask;
        const int askId = store.addTask(ask, saturday);
        store.complete(askId, QDateTime(saturday, QTime(10, 0)), {}, QDate(2026, 10, 1));
        QCOMPARE(store.task(askId)->due, QDate(2026, 10, 1));
        QVERIFY(!store.task(askId)->archived);
        store.complete(askId, QDateTime(QDate(2026, 10, 1), QTime(10, 0)), {});
        QVERIFY(store.task(askId)->archived);

        Task once = weekly("Fix the gate", 2, saturday);
        once.schedule.kind = Schedule::Once;
        const int onceId = store.addTask(once, saturday);
        store.complete(onceId, QDateTime(saturday, QTime(10, 0)), {});
        QCOMPARE(store.state(*store.task(onceId), saturday), Store::Archived);
    }

    void skippingMovesOnWithoutDoing()
    {
        Store store;
        // Skipped early: the time after the one due, a week after it was due.
        const int mow = store.addTask(weekly("Mow", 3, saturday), saturday);
        store.complete(mow, QDateTime(saturday.addDays(-7), QTime(9, 0)), "Edged");
        QVERIFY(store.skip(mow, QDateTime(saturday.addDays(-2), QTime(9, 0)), "  Rained all weekend "));
        QCOMPARE(store.task(mow)->due, saturday.addDays(7));
        QCOMPARE(store.history(mow).size(), 2);
        QVERIFY(store.history(mow).first().skipped);
        QCOMPARE(store.history(mow).first().note, QString("Rained all weekend"));
        // Skipping isn't doing: the last time done is still the mow.
        QCOMPARE(store.lastDone(mow)->note, QString("Edged"));
        QVERIFY(store.lastDone(mow, true)->skipped);
        // Skipped late: a week from the day it was skipped.
        QVERIFY(store.skip(mow, QDateTime(saturday.addDays(10), QTime(9, 0)), {}));
        QCOMPARE(store.task(mow)->due, saturday.addDays(17));

        // A fixed rhythm passes over the date due, and keeps its rhythm.
        Task rent = weekly("Rent", 4, QDate(2026, 7, 1));
        rent.schedule.unit = "month";
        rent.schedule.fromDone = false;
        const int rentId = store.addTask(rent, saturday);
        store.task(rentId)->snoozedUntil = QDate(2026, 7, 5);
        store.task(rentId)->today = QDate(2026, 6, 20);
        store.skip(rentId, QDateTime(QDate(2026, 6, 20), QTime(9, 0)), "Paid a year ahead");
        QCOMPARE(store.task(rentId)->due, QDate(2026, 8, 1));
        QVERIFY(!store.task(rentId)->snoozedUntil.isValid());
        QVERIFY(!store.task(rentId)->today.isValid());
        store.skip(rentId, QDateTime(QDate(2026, 9, 9), QTime(9, 0)), {});
        QCOMPARE(store.task(rentId)->due, QDate(2026, 10, 1));

        // Asking tasks take the next date, or retire; once tasks retire.
        Task ask = weekly("Wax the car", 2, saturday);
        ask.schedule.kind = Schedule::Ask;
        const int askId = store.addTask(ask, saturday);
        store.skip(askId, QDateTime(saturday, QTime(10, 0)), {}, QDate(2026, 10, 1));
        QCOMPARE(store.task(askId)->due, QDate(2026, 10, 1));
        QVERIFY(!store.task(askId)->archived);
        Task once = weekly("Fix the gate", 2, saturday);
        once.schedule.kind = Schedule::Once;
        const int onceId = store.addTask(once, saturday);
        store.skip(onceId, QDateTime(saturday, QTime(10, 0)), "Moved out");
        QVERIFY(store.task(onceId)->archived);
        QVERIFY(!store.skip(999, QDateTime(saturday, QTime(10, 0)), {}));

        // A skip survives saving, and plain completions stay as they were.
        Store loaded;
        QVERIFY(loaded.fromJson(store.toJson()));
        QCOMPARE(loaded.toJson(), store.toJson());
        QVERIFY(loaded.history(mow).first().skipped);
        QVERIFY(!loaded.history(mow).last().skipped);
        QVERIFY(!store.toJson().contains("\"skipped\": false"));
    }

    void backendSkipsWithUndo()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("needful.json"));
        backend.setToday(saturday);
        const int mow = backend.saveTask({{"title", "Mow"}, {"scheduleKind", "every"}, {"unit", "week"},
                                          {"due", saturday.toString(Qt::ISODate)}});
        QCOMPARE(backend.task(mow)["skipText"].toString(),
                 QString("It comes back %1.").arg(QLocale().toString(saturday.addDays(7), QLocale::LongFormat)));
        backend.skip(mow, "Rained");
        QCOMPARE(backend.store().task(mow)->due, saturday.addDays(7));
        QCOMPARE(backend.task(mow)["lastDone"].toString(), QString("Never done"));
        const QVariantList history = backend.task(mow)["history"].toList();
        QCOMPARE(history.size(), 1);
        QVERIFY(history.first().toMap()["skipped"].toBool());
        QCOMPARE(history.first().toMap()["note"].toString(), QString("Rained"));
        QCOMPARE(backend.doneToday().size(), 1);
        backend.undo();
        QCOMPARE(backend.store().task(mow)->due, saturday);
        QVERIFY(backend.store().completions.isEmpty());
    }

    void contextsNestAndFilter()
    {
        Store store;
        const int car = store.addContext("Automotive", 0);
        const int crv = store.addContext("White Honda CR-V", car);
        QCOMPARE(store.contextPath(crv), QString::fromUtf8("Automotive › White Honda CR-V"));
        QCOMPARE(store.findContext("automotive/white honda cr-v"), crv);
        QVERIFY(store.inContext(crv, car));
        QVERIFY(!store.inContext(car, crv));

        Task oil = weekly("Oil change", 3, saturday);
        oil.context = crv;
        const int id = store.addTask(oil, saturday);
        store.complete(id, QDateTime(saturday, QTime(10, 0)), "5W-30");
        store.removeContext(crv);
        QCOMPARE(store.task(id)->context, car);
        QCOMPARE(store.completions.first().context, car);
    }

    void savesAndLoadsEverything()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("nested/needful.json");
        Store store;
        const int home = store.addContext("Home", 0);
        Task t = weekly("Mow", 3, saturday);
        t.context = home;
        t.months = {5, 6, 7};
        t.snoozedUntil = saturday.addDays(2);
        const int id = store.addTask(t, saturday);
        store.complete(id, QDateTime(saturday, QTime(8, 30)), "Note");
        QVERIFY(store.save(path));

        Store loaded;
        QVERIFY(loaded.load(path));
        QCOMPARE(loaded.toJson(), store.toJson());
        QVERIFY(!loaded.fromJson("not json"));
    }

    void backendViewsUndoAndReload()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("needful.json");
        Backend backend(path);
        backend.setToday(saturday);
        const int mow = backend.saveTask({{"title", "Mow"}, {"priority", 3}, {"scheduleKind", "every"},
                                          {"unit", "week"}, {"due", saturday.toString(Qt::ISODate)}});
        backend.saveTask({{"title", "Clean gutters"}, {"due", saturday.addDays(5).toString(Qt::ISODate)}});
        QCOMPARE(backend.items().size(), 1);
        QCOMPARE(backend.counts().value("upcoming").toInt(), 1);

        backend.toggleToday(mow);
        QCOMPARE(backend.items().size(), 0);
        backend.setView("today");
        QCOMPARE(backend.items().size(), 1);

        backend.complete(mow, "Done early");
        QCOMPARE(backend.items().size(), 0);
        QCOMPARE(backend.doneToday().size(), 1);
        QVERIFY(backend.canUndo());
        backend.undo();
        QCOMPARE(backend.items().size(), 1);
        backend.redo();
        QCOMPARE(backend.items().size(), 0);

        backend.snooze(mow, 3);
        QCOMPARE(backend.store().task(mow)->snoozedUntil, saturday.addDays(3));

        // Another program (the command line) changes the file.
        Backend other(path);
        other.setToday(saturday);
        other.saveTask({{"title", "From the command line"}});
        QTRY_COMPARE(backend.store().tasks.size(), 3);
    }

    // One oil change routine for three vehicles: each copy keeps its own
    // due date and history, shares edits, and can override them.
    void routinesShareButKeepTheirOwnLogs()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("needful.json"));
        backend.setToday(saturday);
        const int car = backend.addContext("Automotive", 0);
        const int crv = backend.addContext("CR-V", car), tacoma = backend.addContext("Tacoma", car),
                  civic = backend.addContext("Civic", car);
        const QString due = saturday.toString(Qt::ISODate);
        const int first = backend.saveTask({{"title", "Oil change"}, {"contexts", QVariantList{crv, tacoma, civic}},
                                            {"scheduleKind", "every"}, {"every", 6}, {"unit", "month"},
                                            {"due", due}});
        const Store &store = backend.store();
        const int routine = store.task(first)->routine;
        QVERIFY(routine);
        const QList<int> copies = store.copies(routine);
        QCOMPARE(copies.size(), 3);
        for (int id : copies)
            QCOMPARE(store.task(id)->due, saturday);

        const int tacomaOil = copies[2]; // Sorted by path: Civic, CR-V, Tacoma.
        QCOMPARE(store.task(tacomaOil)->context, tacoma);
        backend.complete(tacomaOil, "5W-30");
        QCOMPARE(store.task(tacomaOil)->due, saturday.addMonths(6));
        QCOMPARE(store.task(copies[0])->due, saturday);
        QCOMPARE(store.history(tacomaOil).size(), 1);
        QCOMPARE(store.history(copies[0]).size(), 0);

        // The Tacoma alone goes every 5 months.
        backend.saveTask({{"id", tacomaOil}, {"scope", "only"}, {"title", "Oil change"}, {"scheduleKind", "every"},
                          {"every", 5}, {"unit", "month"}, {"notes", "Skid plate first"}});
        QCOMPARE(store.task(tacomaOil)->overrides, QStringList{"schedule"});

        // Renaming and changing the shared interval reaches the others only.
        backend.saveTask({{"id", copies[0]}, {"scope", "all"}, {"title", "Oil and filter"},
                          {"contexts", QVariantList{crv, tacoma, civic}}, {"scheduleKind", "every"},
                          {"every", 4}, {"unit", "month"}, {"priority", 3}});
        QCOMPARE(store.task(copies[1])->schedule.every, 4);
        QCOMPARE(store.task(tacomaOil)->schedule.every, 5);
        QCOMPARE(store.task(tacomaOil)->priority, 3);
        QCOMPARE(store.task(tacomaOil)->title, QString("Oil and filter"));
        QCOMPARE(store.task(tacomaOil)->notes, QString("Skid plate first"));

        // Matching the routine again clears the override.
        backend.saveTask({{"id", tacomaOil}, {"scope", "only"}, {"title", "x"}, {"scheduleKind", "every"},
                          {"every", 4}, {"unit", "month"}, {"priority", 3}});
        QVERIFY(store.task(tacomaOil)->overrides.isEmpty());

        // Unticking the Tacoma removes its copy but keeps its history.
        backend.saveTask({{"id", copies[0]}, {"scope", "all"}, {"title", "Oil and filter"},
                          {"contexts", QVariantList{crv, civic}}, {"scheduleKind", "every"},
                          {"every", 4}, {"unit", "month"}, {"priority", 3}});
        QCOMPARE(store.copies(routine).size(), 2);
        QVERIFY(!store.task(tacomaOil));
        QCOMPARE(store.history(tacomaOil).size(), 1);

        // Swapping every context at once keeps the routine alive.
        backend.saveTask({{"id", copies[0]}, {"scope", "all"}, {"title", "Oil and filter"},
                          {"contexts", QVariantList{tacoma}}, {"scheduleKind", "every"}});
        QCOMPARE(store.copies(routine).size(), 1);
    }

    void aTaskBecomesARoutineKeepingItsHistory()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("needful.json"));
        backend.setToday(saturday);
        const int crv = backend.addContext("CR-V", 0), tacoma = backend.addContext("Tacoma", 0);
        const int id = backend.saveTask({{"title", "Rotate tires"}, {"contexts", QVariantList{crv}},
                                         {"notes", "Torque 80 ft-lb"}});
        QVERIFY(!backend.store().task(id)->routine);
        backend.complete(id, "Done");
        const QDate kept = backend.store().task(id)->due;

        backend.saveTask({{"id", id}, {"title", "Rotate tires"}, {"contexts", QVariantList{crv, tacoma}},
                          {"notes", "Torque 80 ft-lb"}, {"due", kept.toString(Qt::ISODate)}});
        const Task *task = backend.store().task(id);
        QVERIFY(task->routine);
        QCOMPARE(task->context, crv);
        QCOMPARE(backend.store().history(id).size(), 1);
        QCOMPARE(backend.store().routine(task->routine)->notes, QString("Torque 80 ft-lb"));
        QCOMPARE(backend.store().copies(task->routine).size(), 2);

        // Deleting the last copies removes the routine too.
        for (int copy : backend.store().copies(task->routine))
            backend.removeTask(copy);
        QVERIFY(backend.store().routines.isEmpty());
    }

    // Noticing a squeak: a note in the car's history, optionally followed up.
    void logNotesInterleaveAndFollowUp()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("needful.json");
        Backend backend(path);
        backend.setToday(saturday);
        const int car = backend.addContext("Automotive", 0);
        const int crv = backend.addContext("CR-V", car), home = backend.addContext("Home", 0);
        const int oil = backend.saveTask({{"title", "Oil change"}, {"contexts", QVariantList{crv}}});
        backend.complete(oil, "5W-30");

        QVERIFY(!backend.addLog({{"context", crv}, {"text", "   "}}));
        const int plain = backend.addLog({{"context", crv}, {"text", "Tires look fine"}});
        QVERIFY(plain);
        QCOMPARE(backend.store().logEntry(plain)->task, 0);

        const QString longText = QString("Squeak from the front left at low speed, louder when cold and turning "
                                         "right into the driveway\nMaybe the sway bar link?");
        const int squeak = backend.addLog({{"context", crv}, {"text", longText}, {"followUp", true},
                                           {"priority", 3}, {"due", saturday.addDays(2).toString(Qt::ISODate)}});
        const int followUpId = backend.store().logEntry(squeak)->task;
        const Task *followUp = backend.store().task(followUpId);
        QVERIFY(followUp);
        QCOMPARE(followUp->schedule.kind, Schedule::Once);
        QCOMPARE(followUp->context, crv);
        QCOMPARE(followUp->priority, 3);
        QCOMPARE(followUp->due, saturday.addDays(2));
        QVERIFY(followUp->title.endsWith(QString::fromUtf8("…")));
        QVERIFY(followUp->title.size() <= 81);
        QVERIFY(!followUp->title.contains('\n'));
        QCOMPARE(followUp->notes, longText);
        QCOMPARE(Store::followUpTitle("Short one\nmore"), QString("Short one"));

        backend.addLog({{"context", home}, {"text", "Gutter drips"}});

        // History interleaves, newest first, filtered to the car.
        backend.setContextFilter(car);
        backend.setView("history");
        QVariantList items = backend.items();
        QCOMPARE(items.size(), 3);
        QCOMPARE(items[0].toMap()["kind"].toString(), QString("log"));
        QCOMPARE(items[0].toMap()["id"].toInt(), squeak);
        QCOMPARE(items[0].toMap()["followUpDone"].toBool(), false);
        QCOMPARE(items.last().toMap()["kind"].toString(), QString("completion"));
        QCOMPARE(backend.counts()["history"].toInt(), 3);

        // Following it up marks the note, and the completion points back.
        backend.complete(followUpId, "Replaced the link");
        items = backend.items();
        QCOMPARE(items[0].toMap()["kind"].toString(), QString("completion"));
        QCOMPARE(items[0].toMap()["fromLog"].toInt(), squeak);
        for (const auto &item : items)
            if (item.toMap()["id"].toInt() == squeak && item.toMap()["kind"] == "log")
                QVERIFY(item.toMap()["followUpDone"].toBool());

        backend.setSearch("driveway");
        QCOMPARE(backend.items().size(), 1);
        backend.setSearch({});

        // Round trip through the file.
        Store loaded;
        QVERIFY(loaded.load(path));
        QCOMPARE(loaded.log.size(), 3);
        QCOMPARE(loaded.toJson(), backend.store().toJson());

        // Deleting the car moves its notes up to Automotive.
        backend.removeContext(crv);
        QCOMPARE(backend.store().logEntry(squeak)->context, car);
    }

    void undoTakesBackANoteAndItsFollowUp()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("needful.json"));
        backend.setToday(saturday);
        const int tasks = backend.store().tasks.size();
        const int id = backend.addLog({{"text", "Check the leak"}, {"followUp", true}});
        QCOMPARE(backend.store().tasks.size(), tasks + 1);
        QCOMPARE(backend.store().tasks.last().due, saturday);
        backend.undo();
        QVERIFY(!backend.store().logEntry(id));
        QCOMPARE(backend.store().tasks.size(), tasks);

        // Removing a note leaves its follow-up.
        const int again = backend.addLog({{"text", "Check the leak"}, {"followUp", true}});
        backend.removeLog(again);
        QVERIFY(!backend.store().logEntry(again));
        QCOMPARE(backend.store().tasks.size(), tasks + 1);
    }

    // Two processes on one file: each change lands on top of the other's,
    // even before the file watcher has noticed.
    void concurrentWritersDontOverwrite()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("needful.json");
        Backend window(path), phone(path);
        window.setToday(saturday);
        phone.setToday(saturday);
        window.saveTask({{"title", "From the window"}});
        phone.saveTask({{"title", "From the phone"}});
        window.saveTask({{"title", "From the window again"}});
        Store disk;
        QVERIFY(disk.load(path));
        QStringList titles;
        for (const auto &t : disk.tasks)
            titles.append(t.title);
        titles.sort();
        QCOMPARE(titles, (QStringList{"From the phone", "From the window", "From the window again"}));
        // Ids stay unique across the two writers.
        QCOMPARE(disk.tasks[0].id + disk.tasks[1].id + disk.tasks[2].id, 6);
    }

    // The first machine moves its data into the folder; the second joins,
    // merging in only what it alone had, so a copy of the same file isn't
    // doubled.
    void syncSetupMovesInAndJoins()
    {
        QTemporaryDir dir;
        const QString desktopData = dir.filePath("desktop/needful.json"), laptopData = dir.filePath("laptop/needful.json");
        const QString desktopFolder = dir.filePath("desktop-sync"), laptopFolder = dir.filePath("laptop-sync");
        const QDateTime noon(saturday, QTime(12, 0));
        int mow = 0;
        {
            Backend desktop(desktopData);
            desktop.setToday(saturday);
            const int home = desktop.addContext("Home", 0);
            mow = desktop.saveTask({{"title", "Mow"}, {"contexts", QVariantList{home}}, {"scheduleKind", "every"}});
            desktop.complete(mow, "Edged too");
            desktop.addLog({{"context", home}, {"text", "Gutter drips"}});
        }
        // The laptop had a copy of the same file, then a task of its own.
        QDir().mkpath(dir.filePath("laptop"));
        QFile::copy(desktopData, laptopData);
        {
            Backend laptop(laptopData);
            laptop.setToday(saturday);
            laptop.saveTask({{"title", "Wash the car"}});
        }

        SyncFolder::Setup first;
        QString error;
        QVERIFY2(SyncFolder::setup(desktopData, desktopFolder, "desktop", noon, &first, &error), qPrintable(error));
        QVERIFY(!first.joined);
        QCOMPARE(first.tasks, 1);
        QCOMPARE(first.contexts, 1);
        QCOMPARE(first.history, 2);
        QVERIFY(SyncFolder::readPointer(contentsOf(desktopData), nullptr));
        QVERIFY(QFile::exists(desktopData + ".before-sync"));
        {
            Backend desktop(desktopData);
            QVERIFY(desktop.error().isEmpty());
            QVERIFY(desktop.sync());
            QCOMPARE(desktop.store().task(mow)->title, QString("Mow")); // Same numbers as before.
            QCOMPARE(desktop.store().history(mow).first().note, QString("Edged too"));
        }

        syncTool(desktopFolder, laptopFolder);
        SyncFolder::Setup joined;
        QVERIFY2(SyncFolder::setup(laptopData, laptopFolder, "laptop", noon, &joined, &error), qPrintable(error));
        QVERIFY(joined.joined);
        QCOMPARE(joined.others, QStringList{"desktop"});
        QCOMPARE(joined.tasks, 1);
        QCOMPARE(joined.history, 0);
        QCOMPARE(joined.name, QString("laptop"));

        // Setting up again is harmless; another folder is refused.
        SyncFolder::Setup again;
        QVERIFY(SyncFolder::setup(laptopData, laptopFolder, {}, noon, &again, &error));
        QVERIFY(again.already);
        QVERIFY(!SyncFolder::setup(laptopData, dir.filePath("elsewhere"), {}, noon, &again, &error));

        syncTool(desktopFolder, laptopFolder);
        Backend desktop(desktopData), laptop(laptopData);
        QCOMPARE(titles(desktop.store()), (QStringList{"Mow", "Wash the car"}));
        QCOMPARE(desktop.store().toJson(), laptop.store().toJson());
        QCOMPARE(desktop.store().log.size(), 1);
    }

    // Each row of the pitch's table, made on two machines that can't see
    // each other, then synced: both end up with the same list.
    void syncMergesChangesFromBothMachines()
    {
        QTemporaryDir dir;
        const QString aData = dir.filePath("a.json"), bData = dir.filePath("b.json");
        const QString aFolder = dir.filePath("a-sync"), bFolder = dir.filePath("b-sync");
        const QDate sunday = saturday.addDays(1), monday = saturday.addDays(2);
        int home = 0;
        {
            Backend a(aData);
            a.setToday(saturday);
            home = a.addContext("Home", 0);
            for (const QString title : {"Mow", "Gutters", "Filter", "Rake", "Sweep", "Dust", "Weed"})
                a.saveTask({{"title", title}, {"contexts", QVariantList{home}}, {"scheduleKind", "every"}, {"unit", "week"},
                            {"due", saturday.toString(Qt::ISODate)}});
            a.complete(taskNamed(a.store(), "Rake"), "Last fall");
        }
        SyncFolder::Setup setup;
        QString error;
        QVERIFY(SyncFolder::setup(aData, aFolder, "desktop", QDateTime(saturday, QTime(12, 0)), &setup, &error));
        syncTool(aFolder, bFolder);
        QVERIFY(SyncFolder::setup(bData, bFolder, "laptop", QDateTime(saturday, QTime(12, 0)), &setup, &error));

        Backend a(aData), b(bData);
        a.setToday(saturday);
        b.setToday(saturday);
        const Store &sa = a.store();
        const int mow = taskNamed(sa, "Mow"), gutters = taskNamed(sa, "Gutters"), filter = taskNamed(sa, "Filter"),
                  rake = taskNamed(sa, "Rake"), sweep = taskNamed(sa, "Sweep"), dust = taskNamed(sa, "Dust");
        QCOMPARE(taskNamed(b.store(), "Mow"), mow);

        auto edit = [](Backend &machine, int id, const QVariantMap &changes) {
            QVariantMap fields = machine.task(id);
            fields["scope"] = "only";
            for (auto it = changes.begin(); it != changes.end(); ++it)
                fields[it.key()] = it.value();
            machine.saveTask(fields);
        };
        // Different things: both kept.
        edit(a, mow, {{"priority", 4}});
        edit(b, mow, {{"notes", "Bag the clippings"}});
        // Done the same day: one completion, both notes.
        a.complete(gutters, "Left side");
        b.complete(gutters, "Right side");
        // Done on one and skipped on the other the same day: both stay.
        const int weed = taskNamed(sa, "Weed");
        a.complete(weed, "Front beds");
        b.skip(weed, "Back beds can wait");
        // Deleted on one, edited on the other: it stays deleted, history too.
        a.removeTask(rake);
        edit(b, rake, {{"notes", "Use the big rake"}});
        // Notes on a context, and pulling into today.
        a.addLog({{"context", home}, {"text", "Fence post loose"}});
        b.addLog({{"context", home}, {"text", "Hose leaks"}});
        a.toggleToday(sweep);
        b.toggleToday(dust);
        // New tasks on both: two tasks, though both picked the same number.
        const int fromA = a.saveTask({{"title", "From the desktop"}});
        const int fromB = b.saveTask({{"title", "From the laptop"}});
        QCOMPARE(fromA, fromB);
        // The same field: the later edit wins, though it arrives first.
        b.setToday(sunday);
        edit(b, sweep, {{"title", "Sweep the porch"}});
        a.setToday(saturday);
        edit(a, sweep, {{"title", "Sweep the deck"}});
        // Done on different days: both in history, due follows the later.
        a.complete(filter, "Saturday");
        b.setToday(monday);
        b.complete(filter, "Monday");

        syncTool(aFolder, bFolder);
        a.setToday(saturday);
        b.setToday(saturday);
        QTRY_COMPARE(a.store().toJson(), b.store().toJson());
        Backend fresh(aData);
        fresh.setToday(saturday);
        QCOMPARE(fresh.store().toJson(), b.store().toJson());
        const Store &s = fresh.store();

        QCOMPARE(s.task(mow)->priority, 4);
        QCOMPARE(s.task(mow)->notes, QString("Bag the clippings"));
        QCOMPARE(s.history(gutters).size(), 1);
        QCOMPARE(s.history(weed).size(), 2);
        QCOMPARE(s.lastDone(weed)->note, QString("Front beds"));
        QStringList both = s.history(gutters).first().note.split('\n'); // In the order they were done.
        both.sort();
        QCOMPARE(both, (QStringList{"Left side", "Right side"}));
        QVERIFY(!s.task(rake));
        QCOMPARE(s.history(rake).size(), 1);
        int notes = 0;
        for (const auto &e : s.log)
            notes += e.text == "Fence post loose" || e.text == "Hose leaks";
        QCOMPARE(notes, 2);
        QCOMPARE(s.task(sweep)->today, saturday);
        QCOMPARE(s.task(dust)->today, saturday);
        fresh.setView("today");
        QCOMPARE(fresh.items().size(), 2);
        QVERIFY(titles(s).contains("From the desktop") && titles(s).contains("From the laptop"));
        QVERIFY(taskNamed(s, "From the desktop") != taskNamed(s, "From the laptop"));
        QCOMPARE(s.task(sweep)->title, QString("Sweep the porch"));
        QCOMPARE(s.history(filter).size(), 2);
        QCOMPARE(s.task(filter)->due, monday.addDays(7));

        // Deleting the merged completion deletes it everywhere, both halves.
        const int merged = s.history(gutters).first().id;
        a.removeCompletion(merged);
        syncTool(aFolder, bFolder);
        QTRY_COMPARE(b.store().history(gutters).size(), 0);
    }

    // A clock that jumps back can't put this machine's own changes out of
    // order, and a change made after seeing another machine's beats it.
    void syncOrdersChangesDespiteClocks()
    {
        QTemporaryDir dir;
        const QString aData = dir.filePath("a.json"), bData = dir.filePath("b.json");
        const QString aFolder = dir.filePath("a-sync"), bFolder = dir.filePath("b-sync");
        int id = 0;
        {
            Backend a(aData);
            a.setToday(saturday);
            id = a.saveTask({{"title", "Mow"}});
        }
        SyncFolder::Setup setup;
        QString error;
        QVERIFY(SyncFolder::setup(aData, aFolder, "desktop", QDateTime(saturday, QTime(12, 0)), &setup, &error));
        Backend a(aData);
        a.setToday(saturday);
        a.saveTask({{"id", id}, {"title", "First"}});
        a.setToday(saturday.addDays(-30));
        a.saveTask({{"id", id}, {"title", "Second"}});
        QCOMPARE(Backend(aData).store().task(id)->title, QString("Second"));

        syncTool(aFolder, bFolder);
        QVERIFY(SyncFolder::setup(bData, bFolder, "laptop", QDateTime(saturday, QTime(12, 0)), &setup, &error));
        Backend b(bData);
        b.setToday(saturday.addDays(-400)); // A laptop whose clock is far behind.
        b.saveTask({{"id", id}, {"title", "Third"}});
        syncTool(aFolder, bFolder);
        QTRY_COMPARE(a.store().task(id)->title, QString("Third"));
    }

    // Undo takes back this machine's own last change, deletes included, and
    // the window picks up the other machine's changes as they arrive.
    void syncUndoesAndUpdatesInPlace()
    {
        QTemporaryDir dir;
        const QString aData = dir.filePath("a.json"), bData = dir.filePath("b.json");
        const QString aFolder = dir.filePath("a-sync"), bFolder = dir.filePath("b-sync");
        SyncFolder::Setup setup;
        QString error;
        QVERIFY(SyncFolder::setup(aData, aFolder, "desktop", QDateTime(saturday, QTime(12, 0)), &setup, &error));
        Backend a(aData);
        a.setToday(saturday);
        const int mow = a.saveTask({{"title", "Mow"}});
        a.complete(mow, "Edged");
        a.removeTask(mow);
        QVERIFY(!a.store().task(mow));
        a.undo();
        QVERIFY(a.store().task(mow));
        QVERIFY(Backend(aData).store().task(mow));
        a.undo();
        QCOMPARE(Backend(aData).store().history(mow).size(), 0);
        a.redo();
        QCOMPARE(Backend(aData).store().history(mow).size(), 1);

        syncTool(aFolder, bFolder);
        QVERIFY(SyncFolder::setup(bData, bFolder, "laptop", QDateTime(saturday, QTime(12, 0)), &setup, &error));
        Backend b(bData);
        b.setToday(saturday);
        b.saveTask({{"title", "From the laptop"}});
        syncTool(aFolder, bFolder);
        QTRY_COMPARE(a.store().tasks.size(), 2);
        QVERIFY(!a.canUndo()); // Its steps were for a list that has since changed.
        QCOMPARE(a.syncText(), QString::fromUtf8("⇄ Synced · laptop just now"));
    }

    void syncStatusRenamesAndForgets()
    {
        QTemporaryDir dir;
        const QString aData = dir.filePath("a.json"), bData = dir.filePath("b.json"), folder = dir.filePath("sync");
        SyncFolder::Setup setup;
        QString error;
        QVERIFY(SyncFolder::setup(aData, folder, "desktop", QDateTime(saturday.addDays(-10), QTime(12, 0)), &setup, &error));
        // Same name: the second gets its own.
        QVERIFY(SyncFolder::setup(bData, folder, "desktop", QDateTime(saturday, QTime(12, 0)), &setup, &error));
        QCOMPARE(setup.name, QString("desktop-2"));
        // A file a sync tool set aside is not Needful's, and is left alone.
        write(folder + "/machines/abc.sync-conflict-20261119-1.json", "not json");

        Backend b(bData);
        b.setToday(saturday);
        QVERIFY(b.error().isEmpty());
        QVERIFY(b.sync()->rename("laptop", &error));
        QVERIFY(!b.sync()->rename("Desktop", &error));
        QCOMPARE(b.sync()->machines().size(), 2);
        QCOMPARE(b.syncText(), QString("desktop: nothing since %1").arg(QLocale().toString(saturday.addDays(-10), QLocale::ShortFormat)));
        QVERIFY(b.syncLate());

        QString desktop;
        for (const auto &m : b.sync()->machines())
            if (m.name == "desktop")
                desktop = m.id;
        QVERIFY(b.sync()->forget(desktop, &error));
        Backend again(bData);
        again.setToday(saturday);
        QCOMPARE(again.syncText(), QString::fromUtf8("⇄ Synced · no other computer yet"));

        // It comes back when it changes something again.
        Backend a(aData);
        a.setToday(saturday);
        a.saveTask({{"title", "Back again"}});
        QTRY_COMPARE(again.syncText(), QString::fromUtf8("⇄ Synced · desktop just now"));
    }

    // The data file stays on this computer. Shared by a sync tool, the
    // pointer would reach the other computers, and they'd write as this one.
    void syncKeepsTheDataFileOnThisComputer()
    {
        QTemporaryDir dir;
        const QDateTime noon(saturday, QTime(12, 0));
        SyncFolder::Setup setup;
        QString error;
        const QString folder = dir.filePath("Sync/needful");
        QVERIFY(!SyncFolder::setup(folder + "/needful.json", folder, {}, noon, &setup, &error));
        QVERIFY(error.contains("is inside"));

        QDir().mkpath(dir.filePath("Dropbox/apps"));
        write(dir.filePath("Dropbox/.dropbox"), "{}");
        QVERIFY(!SyncFolder::setup(dir.filePath("Dropbox/apps/needful.json"), folder, {}, noon, &setup, &error));
        QVERIFY(error.contains("Dropbox keeps in step"));
        QVERIFY(!QFile::exists(dir.filePath("Dropbox/apps/needful.json")));
        QCOMPARE(SyncFolder::syncedBy(dir.filePath("plain/needful.json")), QString());

        // Set up here, the pointer names this computer and works.
        const QString data = dir.filePath("local/needful.json");
        QVERIFY2(SyncFolder::setup(data, folder, "desktop", noon, &setup, &error), qPrintable(error));
        SyncFolder::Pointer pointer;
        QVERIFY(SyncFolder::readPointer(contentsOf(data), &pointer));
        QCOMPARE(pointer.computer, SyncFolder::thisComputer());
        QVERIFY(!pointer.elsewhere());

        // Copied to another computer, it refuses to write as this machine.
        pointer.computer = "0123456789abcdef";
        const QString copied = dir.filePath("other/needful.json");
        QDir().mkpath(dir.filePath("other"));
        write(copied, SyncFolder::pointerJson(pointer));
        const QByteArray before = contentsOf(folder + "/machines/" + pointer.machine + ".json");
        Backend other(copied);
        other.setToday(saturday);
        QVERIFY(other.error().contains("another computer"));
        QVERIFY(!other.sync());
        QCOMPARE(other.saveTask({{"title", "Written as the desktop"}}), 0);
        QCOMPARE(contentsOf(folder + "/machines/" + pointer.machine + ".json"), before);
        QVERIFY(!SyncFolder::setup(copied, folder, {}, noon, &setup, &error));
        QVERIFY(error.contains("another computer"));
    }

    // Her list and yours on one computer: yours by default, hers when asked.
    void universesKeepListsApart()
    {
        QTemporaryDir dir;
        const QByteArray oldData = qgetenv("NEEDFUL_DATA");
        qputenv("NEEDFUL_DATA", dir.filePath("needful.json").toUtf8());
        auto restore = qScopeGuard([&] {
            if (oldData.isEmpty()) qunsetenv("NEEDFUL_DATA"); else qputenv("NEEDFUL_DATA", oldData);
        });

        Universes universes;
        QCOMPARE(universes.list().size(), 1);
        QCOMPARE(universes.list().first().name, QString("mine"));
        QCOMPARE(universes.list().first().label(), QString("Mine"));
        QString error;
        QVERIFY(universes.add("Hers", &error).isEmpty());
        QVERIFY(universes.add("mine", &error).isEmpty());
        const QString hers = universes.add("hers", &error);
        QCOMPARE(hers, dir.filePath("universes/hers.json"));
        QCOMPARE(universes.add("hers", &error), hers); // Adding again is harmless.
        QCOMPARE(universes.nameOf(dir.filePath("needful.json")), QString("mine"));
        QCOMPARE(universes.nameOf(hers), QString("hers"));

        {
            Backend her(hers);
            her.setToday(saturday);
            her.saveTask({{"title", "Water the ferns"}});
        }
        Backend window;
        window.setToday(saturday);
        window.saveTask({{"title", "Oil change"}});
        const int home = window.addContext("Garage", 0);
        window.setContextFilter(home);
        QCOMPARE(window.universe(), QString("mine"));
        QCOMPARE(window.universes().size(), 2);
        QVERIFY(window.canUndo());

        window.openUniverse("hers");
        QCOMPARE(window.universe(), QString("hers"));
        QCOMPARE(window.dataPath(), hers);
        QCOMPARE(titles(window.store()), QStringList{"Water the ferns"});
        QCOMPARE(window.contextFilter(), 0);
        QVERIFY(!window.canUndo());
        QVERIFY(window.universes()[1].toMap()["open"].toBool());
        window.saveTask({{"title", "Repot the fig"}});
        window.openUniverse("nobody");
        QCOMPARE(window.universe(), QString("hers"));
        window.openUniverse("mine");
        QCOMPARE(titles(window.store()), QStringList{"Oil change"});
        QCOMPARE(titles(Backend(hers).store()), (QStringList{"Repot the fig", "Water the ferns"}));

        // The command line takes --universe, and joins her synced folder.
        QCOMPARE(runCli({"needful", "add", "Prune the roses", "--universe", "hers"}), 0);
        QCOMPARE(titles(Backend(hers).store()).size(), 3);
        QCOMPARE(runCli({"needful", "universe", "add", "house", dir.filePath("house-sync"), "--name", "kitchen"}), 0);
        SyncFolder::Pointer pointer;
        QVERIFY(SyncFolder::readPointer(contentsOf(dir.filePath("universes/house.json")), &pointer));
        QCOMPARE(QDir::cleanPath(pointer.folder), QDir::cleanPath(dir.filePath("house-sync")));
        QTRY_COMPARE(window.universes().size(), 3); // The switcher hears of it.

        QVERIFY(universes.remove("hers", &error));
        QVERIFY(!universes.remove("mine", &error));
        QVERIFY(QFile::exists(hers)); // Her list stays; it's just not listed here.
        QCOMPARE(universes.list().size(), 2);
    }

    void httpParsesSplitRequests()
    {
        HttpRequestParser parser;
        QCOMPARE(parser.feed("POST /api/task/3/done?view=to%20day HTTP/1.1\r\nHost: localhost\r\nContent-Le"),
                 HttpRequestParser::Incomplete);
        QCOMPARE(parser.feed("ngth: 5\r\nContent-Type: application/json\r\n\r\n{\"a\""), HttpRequestParser::Incomplete);
        QCOMPARE(parser.feed(":1}"), HttpRequestParser::Complete);
        const HttpRequest &request = parser.request();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.path, QString("/api/task/3/done"));
        QCOMPARE(request.query.queryItemValue("view", QUrl::FullyDecoded), QString("to day"));
        QCOMPARE(request.header("Content-Type"), QByteArray("application/json"));
        QCOMPARE(request.body, QByteArray("{\"a\":"));
    }

    void httpRefusesBadRequests_data()
    {
        QTest::addColumn<QByteArray>("raw");
        QTest::addColumn<int>("status");
        QTest::newRow("garbage") << QByteArray("HELLO\r\n\r\n") << 400;
        QTest::newRow("relative target") << QByteArray("GET index.html HTTP/1.1\r\n\r\n") << 400;
        QTest::newRow("http2") << QByteArray("GET / HTTP/2.0\r\n\r\n") << 505;
        QTest::newRow("too big") << QByteArray("POST / HTTP/1.1\r\nContent-Length: 70000\r\n\r\n") << 413;
        QTest::newRow("chunked") << QByteArray("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n") << 501;
        QTest::newRow("folded") << QByteArray("GET / HTTP/1.1\r\nHost: a\r\n  b\r\n\r\n") << 400;
        QTest::newRow("two lengths") << QByteArray("POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n") << 400;
        QTest::newRow("bad length") << QByteArray("POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n") << 400;
        QTest::newRow("huge head") << ("GET / HTTP/1.1\r\nX: " + QByteArray(20000, 'a')) << 431;
    }

    void httpRefusesBadRequests()
    {
        QFETCH(QByteArray, raw);
        QFETCH(int, status);
        HttpRequestParser parser;
        QCOMPARE(parser.feed(raw), HttpRequestParser::Failed);
        QCOMPARE(parser.errorStatus(), status);
    }

    void httpServerAnswersAndCloses()
    {
        HttpServer server([](const HttpRequest &request) {
            return HttpResponse::json("{\"path\":\"" + request.path.toUtf8() + "\"}");
        });
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));
        socket.write("GET /hello HTTP/1.1\r\nHost: localhost\r\n");
        socket.flush();
        QTest::qWait(50);
        socket.write("\r\n");
        QByteArray reply;
        QTRY_VERIFY_WITH_TIMEOUT((reply += socket.readAll(), socket.state() == QAbstractSocket::UnconnectedState), 3000);
        reply += socket.readAll();
        QVERIFY(reply.startsWith("HTTP/1.1 200 OK\r\n"));
        QVERIFY(reply.contains("Connection: close\r\n"));
        QVERIFY(reply.endsWith("{\"path\":\"/hello\"}"));
    }

    void webApiServesTheList()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("needful.json"));
        backend.setToday(saturday);
        const int home = backend.addContext("Home", 0);
        const int mow = backend.saveTask({{"title", "Mow"}, {"contexts", QVariantList{home}}, {"priority", 3},
                                          {"scheduleKind", "every"}, {"unit", "week"}});
        WebApi api(&backend);

        auto call = [&api](const QByteArray &method, const QString &path, const QByteArray &body = {},
                           const QByteArray &host = "localhost:8787") {
            HttpRequest request;
            request.method = method;
            const int question = path.indexOf('?');
            request.path = question < 0 ? path : path.left(question);
            if (question >= 0)
                request.query = QUrlQuery(path.mid(question + 1));
            request.headers.insert("host", host);
            if (method == "POST")
                request.headers.insert("content-type", "application/json");
            request.body = body;
            return api.handle(request);
        };
        auto json = [](const HttpResponse &response) { return QJsonDocument::fromJson(response.body).object(); };

        HttpResponse state = call("GET", "/api/state?view=needful");
        QCOMPARE(state.status, 200);
        QCOMPARE(json(state).value("items").toArray().size(), 1);
        QCOMPARE(json(state).value("accent").toString(), QString("#FFD60A"));
        bool hasPolicy = false;
        for (const auto &header : state.headers)
            hasPolicy |= header.first == "Content-Security-Policy" && header.second.contains("script-src 'self'") &&
                         !header.second.contains("unsafe-inline");
        QVERIFY(hasPolicy);

        QCOMPARE(call("POST", QString("/api/task/%1/today").arg(mow)).status, 200);
        QCOMPARE(json(call("GET", "/api/state?view=today")).value("items").toArray().size(), 1);
        QCOMPARE(call("POST", QString("/api/task/%1/snooze").arg(mow), "{\"days\":3}").status, 200);
        QCOMPARE(backend.store().task(mow)->snoozedUntil, saturday.addDays(3));
        QCOMPARE(call("POST", QString("/api/task/%1/snooze").arg(mow), "{\"days\":0}").status, 400);
        QCOMPARE(call("POST", QString("/api/task/%1/unsnooze").arg(mow)).status, 200);
        QCOMPARE(call("POST", QString("/api/task/%1/done").arg(mow), "{\"note\":\"Edged too\"}").status, 200);
        QCOMPARE(backend.store().history(mow).first().note, QString("Edged too"));
        QCOMPARE(json(call("GET", QString("/api/task/%1").arg(mow))).value("history").toArray().size(), 1);

        const HttpResponse added = call("POST", "/api/tasks", "{\"title\":\"Wash the car\",\"contexts\":[" +
                                                                  QByteArray::number(home) + "],\"priority\":4}");
        QCOMPARE(added.status, 201);
        const int washed = json(added).value("id").toInt();
        QCOMPARE(backend.store().task(washed)->schedule.kind, Schedule::Once);
        QCOMPARE(backend.store().task(washed)->context, home);
        QCOMPARE(call("POST", "/api/tasks", "{\"title\":\"\"}").status, 400);

        QCOMPARE(call("POST", "/api/log", "{\"context\":1,\"text\":\"Squeak\",\"followUp\":true}").status, 201);
        QCOMPARE(call("POST", "/api/log", "{\"text\":\"  \"}").status, 400);
        QCOMPARE(call("POST", "/api/task/999/done", "{}").status, 404);
        QCOMPARE(call("GET", "/api/task/999").status, 404);
        QCOMPARE(call("POST", "/api/tasks", "not json").status, 400);
        QCOMPARE(call("DELETE", "/api/tasks").status, 405);

        HttpRequest form;
        form.method = "POST";
        form.path = "/api/tasks";
        form.headers.insert("content-type", "application/x-www-form-urlencoded");
        form.body = "title=x";
        QCOMPARE(api.handle(form).status, 415);

        QCOMPARE(call("GET", "/api/state", {}, "evil.example.com").status, 403);
        QCOMPARE(call("GET", "/api/state", {}, "needful.tail1234.ts.net").status, 200);
        QCOMPARE(call("GET", "/api/state", {}, "[::1]:8787").status, 200);
        QCOMPARE(call("GET", "/api/state", {}, "192.168.1.20:8787").status, 200);
    }

    void webApiServesThePage_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QByteArray>("type");
        QTest::newRow("root") << "/" << QByteArray("text/html; charset=utf-8");
        QTest::newRow("script") << "/app.js" << QByteArray("text/javascript; charset=utf-8");
        QTest::newRow("worker") << "/sw.js" << QByteArray("text/javascript; charset=utf-8");
        QTest::newRow("styles") << "/style.css" << QByteArray("text/css; charset=utf-8");
        QTest::newRow("manifest") << "/manifest.webmanifest" << QByteArray("application/manifest+json");
        QTest::newRow("icon") << "/icon-192.png" << QByteArray("image/png");
        QTest::newRow("svg") << "/icon.svg" << QByteArray("image/svg+xml");
    }

    void webApiServesThePage()
    {
        QFETCH(QString, path);
        QFETCH(QByteArray, type);
        QTemporaryDir dir;
        Backend backend(dir.filePath("needful.json"));
        WebApi api(&backend);
        HttpRequest request;
        request.method = "GET";
        request.path = path;
        const HttpResponse response = api.handle(request);
        QCOMPARE(response.status, 200);
        QCOMPARE(response.contentType, type);
        QVERIFY(!response.body.isEmpty());
        // No inline script anywhere in the page.
        if (path == "/") {
            QVERIFY(!response.body.contains("<script>"));
            QVERIFY(!response.body.contains(" on"));
        }
    }

    void webApiKeepsToItsFolder()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("needful.json"));
        WebApi api(&backend);
        for (const QString path : {"/../resources.qrc", "/missing.js", "/web/app.js", "/.hidden"}) {
            HttpRequest request;
            request.method = "GET";
            request.path = path;
            QCOMPARE(api.handle(request).status, 404);
        }
    }

    void serviceUnitsAreWritten()
    {
        ServeSetup setup;
        setup.needful = "/usr/bin/needful";
        setup.port = 9000;
        setup.data = "/home/me/My Data/50% done.json";
        setup.tailscale = true;
        setup.tailscaled = "/usr/bin/tailscaled";
        setup.stateDir = "/home/me/.local/share/needful/tailscale";
        const auto units = serviceUnits(setup);
        QCOMPARE(units.keys(), (QStringList{"needful-serve.service", "needful-tailscale.service"}));
        QVERIFY(units["needful-serve.service"].contains(
            "ExecStart=/usr/bin/needful serve --port 9000 --data \"/home/me/My Data/50%% done.json\"\n"));
        QVERIFY(units["needful-serve.service"].contains("WantedBy=default.target"));
        QVERIFY(units["needful-tailscale.service"].contains(
            "ExecStart=/usr/bin/tailscaled --tun=userspace-networking "
            "--statedir=/home/me/.local/share/needful/tailscale --socket=%t/needful-tailscaled.sock --port=0\n"));
        setup.tailscale = false;
        QCOMPARE(serviceUnits(setup).size(), 1);

        // Another universe's units sit beside yours instead of replacing them.
        setup.universe = "hers";
        setup.tailscale = true;
        const auto hers = serviceUnits(setup);
        QCOMPARE(hers.keys(), (QStringList{"needful-serve-hers.service", "needful-tailscale-hers.service"}));
        QVERIFY(hers["needful-serve-hers.service"].contains("Description=Needful on your phone: the page and its API, for hers\n"));
        QVERIFY(hers["needful-tailscale-hers.service"].contains(" --socket=%t/needful-tailscaled-hers.sock --port=0\n"));
    }

    void launchAgentsAreWritten()
    {
        ServeSetup setup;
        setup.needful = "/Applications/Needful.app/Contents/MacOS/needful";
        setup.port = 9000;
        setup.data = "/Users/me/A & B/needful.json";
        setup.tailscale = true;
        setup.tailscaled = "/opt/homebrew/bin/tailscaled";
        setup.stateDir = "/Users/me/Library/Application Support/needful/tailscale";
        const auto agents = launchAgents(setup);
        QCOMPARE(agents.keys(), (QStringList{"faithbibleinstitute.needful.serve.plist",
                                             "faithbibleinstitute.needful.tailscale.plist"}));
        const QByteArray serve = agents["faithbibleinstitute.needful.serve.plist"];
        QVERIFY(serve.contains("<string>faithbibleinstitute.needful.serve</string>"));
        QVERIFY(serve.contains("<string>/Applications/Needful.app/Contents/MacOS/needful</string>\n"
                               "        <string>serve</string>\n        <string>--port</string>\n"
                               "        <string>9000</string>\n        <string>--data</string>\n"
                               "        <string>/Users/me/A &amp; B/needful.json</string>"));
        QVERIFY(serve.contains("<key>KeepAlive</key>"));
        QVERIFY(agents["faithbibleinstitute.needful.tailscale.plist"].contains("<string>--tun=userspace-networking</string>"));
        setup.tailscale = false;
        QCOMPARE(launchAgents(setup).size(), 1);

        setup.universe = "hers";
        QCOMPARE(launchAgents(setup).keys(), (QStringList{"faithbibleinstitute.needful.serve-hers.plist"}));
    }

    void rsyncUnitsAreWritten()
    {
        RsyncSetup setup;
        setup.rsync = "/usr/bin/rsync";
        setup.local = "/home/me/Sync/needful";
        setup.host = "desktop";
        setup.remote = "Sync/needful";
        setup.every = "10m";
        const auto units = rsyncUnits(setup);
        QCOMPARE(units.keys(), (QStringList{"needful-rsync.service", "needful-rsync.timer"}));
        const QByteArray service = units["needful-rsync.service"];
        QVERIFY(service.contains("Type=oneshot\n"));
        QCOMPARE(service.count("ExecStart=/usr/bin/rsync -au "), 2);
        QVERIFY(service.contains("\"ssh -o BatchMode=yes -o ConnectTimeout=15\""));
        QVERIFY(service.contains(" desktop:Sync/needful/machines/ /home/me/Sync/needful/machines/\n"));
        QVERIFY(service.contains(" --mkpath /home/me/Sync/needful/machines/ desktop:Sync/needful/machines/\n"));
        QVERIFY(units["needful-rsync.timer"].contains("OnUnitInactiveSec=10m\n"));
        QVERIFY(units["needful-rsync.timer"].contains("WantedBy=timers.target"));
    }

    // The real commands, with a stand-in for ssh: each side ends up with the
    // newest copy of every file, and nobody's own file is overwritten by an
    // older copy of it.
    void rsyncLaunchAgentRunsBothCommands()
    {
        RsyncSetup setup;
        setup.rsync = "/opt/homebrew/bin/rsync";
        setup.local = "/Users/me/My Sync/needful";
        setup.host = "desktop";
        setup.remote = "Sync/needful";
        setup.every = "10m";
        const QByteArray agent = rsyncLaunchAgents(setup)["faithbibleinstitute.needful.rsync.plist"];
        QVERIFY(agent.contains("<key>StartInterval</key>\n    <integer>600</integer>"));
        QVERIFY(!agent.contains("KeepAlive"));
        QVERIFY(agent.contains("<string>/bin/sh</string>"));
        // Pull, then push, as one shell line with the spaced path quoted.
        QVERIFY(agent.contains("/opt/homebrew/bin/rsync -au --protect-args"));
        QVERIFY(agent.contains(" &amp;&amp; /opt/homebrew/bin/rsync -au --protect-args "));
        QVERIFY(agent.contains("'/Users/me/My Sync/needful/machines/'"));

        // Without --protect-args or --mkpath, the remote path is quoted instead.
        setup.modern = false;
        setup.remote = "My Sync/needful";
        const auto old = rsyncCommands(setup);
        QVERIFY(!old[0].contains("--protect-args"));
        QVERIFY(!old[1].contains("--mkpath"));
        QCOMPARE(old[0].constLast(), QString("/Users/me/My Sync/needful/machines/"));
        QCOMPARE(old[0].at(old[0].size() - 2), QString("desktop:'My Sync/needful/machines/'"));
    }

    void rsyncKeepsTheNewestCopies()
    {
        const QString rsync = QStandardPaths::findExecutable("rsync");
        if (rsync.isEmpty())
            QSKIP("rsync isn't installed");
        QTemporaryDir dir;
        const QString here = dir.filePath("here"), there = dir.filePath("there");
        QDir().mkpath(here + "/machines");
        QDir().mkpath(there + "/machines");
        const QDateTime old(saturday, QTime(9, 0)), recent(saturday, QTime(10, 0));
        auto put = [](const QString &path, const QByteArray &contents, const QDateTime &at) {
            write(path, contents);
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(at, QFileDevice::FileModificationTime));
        };
        put(here + "/machines/aaaaaaaaaaaa.json", "here, new", recent);
        put(there + "/machines/aaaaaaaaaaaa.json", "here, old copy", old);
        put(there + "/machines/bbbbbbbbbbbb.json", "there, new", recent);
        put(here + "/machines/bbbbbbbbbbbb.json", "there, old copy", old);
        put(there + "/machines/cccccccccccc.json", "only there", old);
        write(there + "/machines/notes.txt", "not Needful's");

        // "ssh <options> host command…" runs the command here instead.
        const QString fakeSsh = dir.filePath("fake-ssh");
        write(fakeSsh, "#!/bin/sh\nwhile [ \"$1\" = -o ]; do shift 2; done\nshift\nexec \"$@\"\n");
        QFile::setPermissions(fakeSsh, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

        RsyncSetup setup;
        setup.rsync = rsync;
        setup.modern = rsyncIsModern(rsync);
        setup.local = here;
        setup.host = "desktop";
        setup.remote = there;
        for (QStringList command : rsyncCommands(setup)) {
            const int shell = command.indexOf("-e");
            QVERIFY(command[shell + 1].startsWith("ssh "));
            command[shell + 1] = fakeSsh + " -o BatchMode=yes";
            QCOMPARE(QProcess::execute(command.first(), command.mid(1)), 0);
        }
        for (const QString &side : {here, there}) {
            QCOMPARE(contentsOf(side + "/machines/aaaaaaaaaaaa.json"), QByteArray("here, new"));
            QCOMPARE(contentsOf(side + "/machines/bbbbbbbbbbbb.json"), QByteArray("there, new"));
            QCOMPARE(contentsOf(side + "/machines/cccccccccccc.json"), QByteArray("only there"));
        }
        QVERIFY(!QFile::exists(here + "/machines/notes.txt"));
    }

    void serveInstallWritesIntoTheConfigFolder()
    {
        QTemporaryDir dir;
        const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME"), oldData = qgetenv("XDG_DATA_HOME");
        const QByteArray oldHome = qgetenv("HOME"), path = qgetenv("PATH");
        qputenv("XDG_CONFIG_HOME", dir.filePath("config").toUtf8());
        qputenv("XDG_DATA_HOME", dir.filePath("data").toUtf8());
#ifdef Q_OS_MACOS
        // Agents go under the home folder, and with no launchctl to find the
        // test never loads any into this session.
        qputenv("HOME", dir.path().toUtf8());
        qputenv("PATH", dir.filePath("empty").toUtf8());
        const QString folder = dir.filePath("Library/LaunchAgents/");
        const QString mine = "faithbibleinstitute.needful.serve.plist", hersFile = "faithbibleinstitute.needful.serve-hers.plist";
        const QByteArray port8899 = "<string>8899</string>";
        const QByteArray port8788 = "<string>8788</string>";
#else
        const QString folder = dir.filePath("config/systemd/user/");
        const QString mine = "needful-serve.service", hersFile = "needful-serve-hers.service";
        const QByteArray port8899 = " serve --port 8899\n";
        const QByteArray port8788 = " serve --port 8788 --data ";
#endif
        const int result = runServe({"needful", "serve", "install", "--port", "8899"});
        const QString unit = folder + mine;
        const bool written = QFile::exists(unit);
        const QByteArray contents = contentsOf(unit);
        // Hers goes beside it, on the first port nothing else uses.
        QString error;
        const QString hersData = Universes().add("hers", &error);
        const int hersResult = runServe({"needful", "serve", "install", "--universe", "hers"});
        const QByteArray hers = contentsOf(folder + hersFile);
        const QByteArray mineAfter = contentsOf(unit);
        const int unknown = runServe({"needful", "serve", "install", "--universe", "nobody"});
        // With no systemctl to find, so the test never touches this session's services.
        qputenv("PATH", dir.filePath("empty").toUtf8());
        const int removed = runServe({"needful", "serve", "uninstall", "--universe", "hers"});
        qputenv("PATH", path);
        const bool hersGone = !QFile::exists(folder + hersFile);
        auto restore = [](const char *name, const QByteArray &value) {
            if (value.isEmpty()) qunsetenv(name); else qputenv(name, value);
        };
        restore("XDG_CONFIG_HOME", oldConfig);
        restore("XDG_DATA_HOME", oldData);
        restore("HOME", oldHome);
        QCOMPARE(result, 0);
        QVERIFY(written);
        QVERIFY(contents.contains(port8899));
        QCOMPARE(hersResult, 0);
        QVERIFY(hers.contains(port8788));
        QVERIFY(hers.contains(hersData.toUtf8()));
        QCOMPARE(mineAfter, contents);
        QCOMPARE(unknown, 1);
        QCOMPARE(removed, 0);
        QVERIFY(hersGone);
        QVERIFY(QFile::exists(unit));
    }

    void devicesMergeLatestAndSpreadUnpairing()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath("a"));
        QDir().mkpath(dir.filePath("b"));
        Devices a(dir.filePath("a")), b(dir.filePath("b"));
        QVERIFY(a.load());
        QVERIFY(b.load());
        QCOMPARE(a.id().size(), 64);
        QVERIFY(a.id() != b.id());
        QVERIFY(QRegularExpression("^[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}$").match(a.fingerprint()).hasMatch());
        // The identity stays the same from one start to the next, and only
        // its owner can read the secret.
        Devices again(dir.filePath("a"));
        QVERIFY(again.load());
        QCOMPARE(again.id(), a.id());
        QCOMPARE(QFile::permissions(dir.filePath("a/identity.key")) & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());

        const QString nas(64, 'c');
        const QDateTime t(saturday, QTime(9, 0));
        a.pair(nas, "nas", "computer", t);
        QVERIFY(b.merge(a.toJson()));
        QVERIFY(b.trustedIds().contains(nas));
        QVERIFY(!b.merge(a.toJson())); // Nothing new the second time.
        b.unpair(nas, t.addSecs(60));
        b.setRelay("https://relay.example.net", t.addSecs(60));
        QVERIFY(a.merge(b.toJson()));
        QVERIFY(!a.trustedIds().contains(nas));
        QCOMPARE(a.relay(), QString("https://relay.example.net"));
        // An older pairing that arrives late doesn't undo the unpairing...
        Devices stale(dir.filePath("a"));
        QVERIFY(stale.load());
        stale.pair(nas, "nas", "computer", t.addSecs(30));
        a.merge(stale.toJson());
        QVERIFY(!a.trustedIds().contains(nas));
        // ...but pairing it again later does.
        a.pair(nas, "nas", "computer", t.addSecs(120));
        QVERIFY(a.trustedIds().contains(nas));
        QVERIFY(a.find("NAS"));
        QVERIFY(a.find(Devices::fingerprintOf(nas).left(9)));
        QVERIFY(!a.find("nope"));
    }

    void syncListsHaveIdsAndFilesHaveVersions()
    {
        QTemporaryDir dir;
        const QString desktopData = dir.filePath("desktop/needful.json"), laptopData = dir.filePath("laptop/needful.json");
        const QString folder = dir.filePath("sync");
        {
            Backend desktop(desktopData);
            desktop.saveTask({{"title", "Mow"}});
        }
        SyncFolder::Setup result;
        QString error;
        QVERIFY2(SyncFolder::setup(desktopData, folder, "desktop", QDateTime::currentDateTime(), &result, &error), qPrintable(error));
        SyncFolder::Pointer pointer;
        QVERIFY(SyncFolder::readPointer(contentsOf(desktopData), &pointer));
        SyncFolder desktop(folder, pointer.machine);
        Store store;
        QVERIFY(desktop.read(&store, &error));
        const QString list = desktop.listId();
        QVERIFY(!list.isEmpty());
        const qint64 saved = desktop.files().value(pointer.machine).saved;
        QVERIFY(saved > 0);
        QVERIFY(desktop.rename("big desktop", &error));
        QVERIFY(desktop.files().value(pointer.machine).newerThan(SyncFolder::File{saved, {}, {}}));
        QVERIFY(desktop.setDevice(QString(64, 'a'), &error));
        QCOMPARE(desktop.files().value(pointer.machine).device, QString(64, 'a'));

        // Joining takes on the list's id.
        QVERIFY(SyncFolder::setup(laptopData, folder, "laptop", QDateTime::currentDateTime(), &result, &error));
        QVERIFY(SyncFolder::readPointer(contentsOf(laptopData), &pointer));
        SyncFolder laptop(folder, pointer.machine);
        QVERIFY(laptop.read(&store, &error));
        QCOMPARE(laptop.listId(), list);

        // A list another computer has can be set up before its files arrive.
        const QString hers = dir.filePath("hers.json");
        QVERIFY(SyncFolder::setup(hers, dir.filePath("hers-sync"), "laptop", QDateTime::currentDateTime(), &result,
                                  &error, hers, "0123456789abcdef"));
        QVERIFY(SyncFolder::readPointer(contentsOf(hers), &pointer));
        SyncFolder adopted(dir.filePath("hers-sync"), pointer.machine);
        QVERIFY(adopted.read(&store, &error));
        QCOMPARE(adopted.listId(), QString("0123456789abcdef"));
    }

    // Two computers, each with its own data folder, on this one: pair by
    // code, join, swap changes, carry universes, and unpair.
    void peerSyncPairsJoinsAndKeepsInStep()
    {
        QTemporaryDir dir;
        const QString desktopData = dir.filePath("desktop/needful.json"), laptopData = dir.filePath("laptop/needful.json");
        {
            Backend desktop(desktopData);
            desktop.saveTask({{"title", "Mow"}});
            desktop.saveTask({{"title", "Oil change"}});
            Backend laptop(laptopData);
            laptop.saveTask({{"title", "Mow"}});
            laptop.saveTask({{"title", "Water the plants"}});
        }
        QString error;
        const QString house = Universes(laptopData).add("house", &error);
        {
            Backend b(house);
            b.saveTask({{"title", "Clean the gutters"}});
        }

        PeerLink::Options options;
        options.port = 0;
        options.mdns = false;
        options.loopback = true;
        options.ipc = false;
        PeerLink desktop(desktopData, options), laptop(laptopData, options);
        QVERIFY2(desktop.start(&error), qPrintable(error));
        QVERIFY2(laptop.start(&error), qPrintable(error));
        // A computer that has never paired keeps its single file.
        QVERIFY(!SyncFolder::readPointer(contentsOf(laptopData), nullptr));

        QSignalSpy code(&desktop, &PeerLink::pairCode), paired(&desktop, &PeerLink::paired);
        QSignalSpy confirm(&laptop, &PeerLink::confirm), joined(&laptop, &PeerLink::joined);
        QSignalSpy desktopFailed(&desktop, &PeerLink::failed), laptopFailed(&laptop, &PeerLink::failed);
        desktop.pair();
        QCOMPARE(code.size(), 1);
        const QString pairCode = code.first().at(0).toString();
        QVERIFY(QRegularExpression("^\\d{1,2}(-[a-z]+){3}$").match(pairCode).hasMatch());
        QCOMPARE(code.first().at(2).toString(), Devices::fingerprintOf(desktop.id()));
        QTRY_VERIFY(desktop.port() > 0);

        laptop.join(pairCode.toUpper().replace('-', ' '), QString("127.0.0.1:%1").arg(desktop.port()));
        QTRY_COMPARE_WITH_TIMEOUT(confirm.size(), 1, 15000);
        // Both show the same fingerprint for the desktop.
        QCOMPARE(confirm.first().at(1).toString(), Devices::fingerprintOf(desktop.id()));
        laptop.answer(true);
        QTRY_COMPARE_WITH_TIMEOUT(joined.size(), 1, 15000);
        QCOMPARE(paired.size(), 1);
        QCOMPARE(paired.first().at(1).toString(), Devices::fingerprintOf(laptop.id()));
        QVERIFY(desktopFailed.isEmpty());
        QVERIFY(laptopFailed.isEmpty());
        const QVariantMap result = joined.first().at(0).toMap();
        QCOMPARE(result["already"].toBool(), false);
        QCOMPARE(result["tasks"].toInt(), 1); // Mow was in both, so only the plants came over.

        auto titles = [](const QString &data) {
            QStringList list;
            const Backend backend(data);
            for (const Task &t : backend.store().tasks)
                list.append(t.title);
            list.sort();
            return list;
        };
        const QStringList all{"Mow", "Oil change", "Water the plants"};
        QCOMPARE(titles(laptopData), all);
        QTRY_COMPARE_WITH_TIMEOUT(titles(desktopData), all, 15000);
        QVERIFY(!laptop.linkTo(desktop.id()).isEmpty());
        QCOMPARE(desktop.linkTo(laptop.id()), QString("local"));

        // The laptop's universe shows up on the desktop, under its name.
        QTRY_VERIFY_WITH_TIMEOUT(!Universes(desktopData).dataPath("house").isEmpty(), 15000);
        const QString desktopHouse = Universes(desktopData).dataPath("house");
        QTRY_COMPARE_WITH_TIMEOUT(titles(desktopHouse), QStringList{"Clean the gutters"}, 15000);

        // A change on one shows up on the other, with no command run.
        {
            Backend b(laptopData);
            b.saveTask({{"title", "Replace the air filter"}});
        }
        QTRY_VERIFY_WITH_TIMEOUT(titles(desktopData).contains("Replace the air filter"), 15000);
        {
            Backend b(desktopHouse);
            b.saveTask({{"title", "Sweep the porch"}});
        }
        QTRY_VERIFY_WITH_TIMEOUT(titles(house).contains("Sweep the porch"), 15000);

        // Status, as the command line and the window read it.
        Backend laptopBackend(laptopData);
        const PeerStatus status = PeerStatus::read(PeerLink::appDirFor(laptopData), laptopBackend.sync());
        QCOMPARE(status.devices.size(), 1);
        QCOMPARE(status.devices.first().device.id, desktop.id());
        QVERIFY(status.devices.first().changed.isValid());

        // Removing a shared universe keeps it off this computer.
        QVERIFY(Universes(desktopData).remove("house", &error));
        QVERIFY(!Universes(desktopData).hidden().isEmpty());
        {
            Backend b(house);
            b.saveTask({{"title", "Fix the fence"}});
        }
        QTest::qWait(1500);
        QVERIFY(Universes(desktopData).dataPath("house").isEmpty());

        // Unpairing cuts the link at once, and nothing more comes over.
        {
            Devices devices(desktop.appDir());
            QVERIFY(devices.load());
            devices.unpair(laptop.id(), QDateTime::currentDateTime());
            QVERIFY(devices.save());
        }
        QTRY_VERIFY_WITH_TIMEOUT(desktop.linkTo(laptop.id()).isEmpty(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(laptop.linkTo(desktop.id()).isEmpty(), 15000);
        {
            Backend b(laptopData);
            b.saveTask({{"title", "Wash the car"}});
        }
        laptop.dial(desktop.id(), {QString("127.0.0.1:%1").arg(desktop.port())});
        QTest::qWait(1500);
        QVERIFY(!titles(desktopData).contains("Wash the car"));
        QVERIFY(desktop.linkTo(laptop.id()).isEmpty());
    }

    // Pairing a third computer with any one of yours trusts it on all of
    // them, and changes find their way through the one in the middle.
    void peerSyncTrustSpreadsAndChangesTravelOn()
    {
        QTemporaryDir dir;
        PeerLink::Options options;
        options.port = 0;
        options.mdns = false;
        options.loopback = true;
        options.ipc = false;
        const QString aData = dir.filePath("a/needful.json"), bData = dir.filePath("b/needful.json"),
                      cData = dir.filePath("c/needful.json");
        PeerLink a(aData, options), b(bData, options), c(cData, options);
        QString error;
        QVERIFY(a.start(&error) && b.start(&error) && c.start(&error));

        auto pairUp = [](PeerLink &host, PeerLink &guest) {
            QSignalSpy code(&host, &PeerLink::pairCode), confirm(&guest, &PeerLink::confirm),
                joined(&guest, &PeerLink::joined);
            host.pair();
            QTRY_VERIFY(host.port() > 0);
            guest.join(code.first().at(0).toString(), QString("127.0.0.1:%1").arg(host.port()));
            QTRY_COMPARE_WITH_TIMEOUT(confirm.size(), 1, 15000);
            guest.answer(true);
            QTRY_COMPARE_WITH_TIMEOUT(joined.size(), 1, 15000);
        };
        pairUp(a, b);
        if (QTest::currentTestFailed())
            return;
        pairUp(b, c);
        if (QTest::currentTestFailed())
            return;
        auto trusted = [](const PeerLink &link) {
            Devices devices(link.appDir());
            devices.load();
            return devices.trustedIds();
        };
        QTRY_VERIFY_WITH_TIMEOUT(trusted(a).contains(c.id()), 15000);
        QVERIFY(trusted(c).contains(a.id()));

        // a and c have never talked; b carries c's change to a.
        {
            Backend backend(cData);
            backend.saveTask({{"title", "From c"}});
        }
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                const Backend backend(aData);
                for (const Task &t : backend.store().tasks)
                    if (t.title == "From c")
                        return true;
                return false;
            }(),
            15000);
    }

    void peerSyncRefusesAWrongCode()
    {
        QTemporaryDir dir;
        PeerLink::Options options;
        options.port = 0;
        options.mdns = false;
        options.loopback = true;
        options.ipc = false;
        PeerLink desktop(dir.filePath("d/needful.json"), options), laptop(dir.filePath("l/needful.json"), options);
        QString error;
        QVERIFY(desktop.start(&error) && laptop.start(&error));
        QSignalSpy code(&desktop, &PeerLink::pairCode), desktopFailed(&desktop, &PeerLink::failed),
            laptopFailed(&laptop, &PeerLink::failed), confirm(&laptop, &PeerLink::confirm);
        desktop.pair();
        QTRY_VERIFY(desktop.port() > 0);
        laptop.join("99-not-the-code", QString("127.0.0.1:%1").arg(desktop.port()));
        QTRY_COMPARE_WITH_TIMEOUT(laptopFailed.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(desktopFailed.size(), 1, 15000);
        QVERIFY(confirm.isEmpty());
        QVERIFY(laptopFailed.first().at(0).toString().contains("didn't match"));
        // The code is spent: the right one no longer works either.
        laptop.join(code.first().at(0).toString(), QString("127.0.0.1:%1").arg(desktop.port()));
        QTRY_COMPARE_WITH_TIMEOUT(laptopFailed.size(), 2, 15000);
        QVERIFY(laptopFailed.last().at(0).toString().contains("isn't waiting"));
        Devices devices(desktop.appDir());
        devices.load();
        QVERIFY(devices.paired().isEmpty());
    }

    // A list already shared through a sync tool's folder can't be swapped
    // for another computer's by joining it.
    // A computer whose list syncs through a folder, with another machine on
    // it, joins where it is: nothing has to live at the same path, and the
    // other machine gets the joined list through the folder.
    void peerSyncJoinsWhereItsFolderIs()
    {
        QTemporaryDir dir;
        const QString desktopData = dir.filePath("d/needful.json"), laptopData = dir.filePath("l/needful.json"),
                      tabletData = dir.filePath("t/needful.json");
        const QString laptopFolder = dir.filePath("Sync/needful"), tabletFolder = dir.filePath("t-sync");
        {
            Backend desktop(desktopData);
            desktop.saveTask({{"title", "Mow"}});
            desktop.saveTask({{"title", "Oil change"}});
            Backend laptop(laptopData);
            const int mow = laptop.saveTask({{"title", "Mow"}});
            laptop.saveTask({{"title", "Water the plants"}});
            laptop.complete(mow, "front only");
        }
        SyncFolder::Setup result;
        QString error;
        QVERIFY2(SyncFolder::setup(laptopData, laptopFolder, "laptop", QDateTime::currentDateTime(), &result, &error),
                 qPrintable(error));
        copyNewer(laptopFolder, tabletFolder);
        QVERIFY2(SyncFolder::setup(tabletData, tabletFolder, "tablet", QDateTime::currentDateTime(), &result, &error),
                 qPrintable(error));
        copyNewer(tabletFolder, laptopFolder);

        PeerLink::Options options;
        options.port = 0;
        options.mdns = false;
        options.loopback = true;
        options.ipc = false;
        PeerLink desktop(desktopData, options), laptop(laptopData, options);
        QVERIFY(desktop.start(&error) && laptop.start(&error));
        QSignalSpy code(&desktop, &PeerLink::pairCode), confirm(&laptop, &PeerLink::confirm),
            joined(&laptop, &PeerLink::joined), failed(&laptop, &PeerLink::failed);
        desktop.pair();
        QTRY_VERIFY(desktop.port() > 0);
        laptop.join(code.first().at(0).toString(), QString("127.0.0.1:%1").arg(desktop.port()));
        QTRY_COMPARE_WITH_TIMEOUT(confirm.size(), 1, 15000);
        laptop.answer(true);
        QTRY_COMPARE_WITH_TIMEOUT(joined.size(), 1, 15000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().at(0).toString()));
        QCOMPARE(joined.first().at(0).toMap()["tasks"].toInt(), 1); // Only the plants were only here.

        // Still in its own folder, with nothing left behind in staging.
        SyncFolder::Pointer pointer;
        QVERIFY(SyncFolder::readPointer(contentsOf(laptopData), &pointer));
        QCOMPARE(QDir::cleanPath(pointer.folder), QDir::cleanPath(laptopFolder));
        QVERIFY(QDir(dir.filePath("l/lists")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());

        auto titles = [](const QString &data) {
            QStringList list;
            const Backend backend(data);
            for (const Task &t : backend.store().tasks)
                list.append(t.title);
            list.sort();
            return list;
        };
        const QStringList all{"Mow", "Oil change", "Water the plants"};
        QCOMPARE(titles(laptopData), all);
        {
            // Mow is the desktop's now, and keeps what was done to it here.
            const Backend b(laptopData);
            QCOMPARE(b.store().completions.size(), 1);
            const Task *mow = nullptr;
            for (const Task &t : b.store().tasks)
                if (t.title == "Mow")
                    mow = &t;
            QVERIFY(mow);
            QCOMPARE(b.store().completions.first().task, mow->id);
            QCOMPARE(b.store().completions.first().note, QString("front only"));
        }
        QTRY_COMPARE_WITH_TIMEOUT(titles(desktopData), all, 15000);
        // The tablet hears through the folder, as it always did.
        copyNewer(laptopFolder, tabletFolder);
        QCOMPARE(titles(tabletData), all);
        {
            Backend tablet(tabletData);
            tablet.saveTask({{"title", "Sweep"}});
        }
        copyNewer(tabletFolder, laptopFolder);
        QTRY_VERIFY_WITH_TIMEOUT(titles(desktopData).contains("Sweep"), 15000);
    }

    // Two computers already in step through a sync tool's folder pair, and
    // nothing is doubled: their lists are already one.
    void peerSyncTakesOverFromAFolder()
    {
        QTemporaryDir dir;
        const QString desktopData = dir.filePath("d/needful.json"), laptopData = dir.filePath("l/needful.json");
        const QString desktopFolder = dir.filePath("d-sync"), laptopFolder = dir.filePath("l-sync");
        {
            Backend desktop(desktopData);
            desktop.saveTask({{"title", "Mow"}});
        }
        SyncFolder::Setup result;
        QString error;
        QVERIFY(SyncFolder::setup(desktopData, desktopFolder, "desktop", QDateTime::currentDateTime(), &result, &error));
        copyNewer(desktopFolder, laptopFolder);
        QVERIFY(SyncFolder::setup(laptopData, laptopFolder, "laptop", QDateTime::currentDateTime(), &result, &error));
        copyNewer(laptopFolder, desktopFolder);

        PeerLink::Options options;
        options.port = 0;
        options.mdns = false;
        options.loopback = true;
        options.ipc = false;
        PeerLink desktop(desktopData, options), laptop(laptopData, options);
        QVERIFY(desktop.start(&error) && laptop.start(&error));
        QSignalSpy code(&desktop, &PeerLink::pairCode), confirm(&laptop, &PeerLink::confirm),
            joined(&laptop, &PeerLink::joined), failed(&laptop, &PeerLink::failed);
        desktop.pair();
        QTRY_VERIFY(desktop.port() > 0);
        laptop.join(code.first().at(0).toString(), QString("127.0.0.1:%1").arg(desktop.port()));
        QTRY_COMPARE_WITH_TIMEOUT(confirm.size(), 1, 15000);
        laptop.answer(true);
        QTRY_COMPARE_WITH_TIMEOUT(joined.size(), 1, 15000);
        QVERIFY(failed.isEmpty());
        QVERIFY(joined.first().at(0).toMap()["already"].toBool());

        // Now changes pass over the link, with no sync tool running.
        {
            Backend b(laptopData);
            b.saveTask({{"title", "Wash the car"}});
        }
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                const Backend backend(desktopData);
                return backend.store().tasks.size() == 2;
            }(),
            15000);
    }

    void linkUnitRunsSyncLink()
    {
        const auto units = linkUnits("/usr/bin/needful", {});
        QVERIFY(units.value(LinkService).contains("ExecStart=/usr/bin/needful sync link\n"));
        QVERIFY(!units.value(LinkService).contains("Environment="));
        QVERIFY(linkUnits("/usr/bin/needful", "/home/me/my data.json")
                    .value(LinkService)
                    .contains("Environment=\"NEEDFUL_DATA=/home/me/my data.json\"\n"));
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
    }

    void translatorUsesEnglishPlurals()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "en\n");
        Translator translator(dir.path() + "/language", QFINDTESTDATA("../src/i18n"));
        QCOMPARE(translator.translate("Backend", "Every %n weeks", nullptr, 1), QString("Every week"));
        QCOMPARE(translator.translate("Backend", "Every %n weeks", nullptr, 2), QString("Every %n weeks"));
    }
};

QTEST_MAIN(NeedfulTests)
#include "needful_tests.moc"
