#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "store.h"
#include "theme.h"
#include "translator.h"

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
