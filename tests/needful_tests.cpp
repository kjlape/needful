#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include "backend.h"
#include "httpserver.h"
#include "serve.h"
#include "store.h"
#include "theme.h"
#include "translator.h"
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
    }

    void serveInstallWritesIntoTheConfigFolder()
    {
        QTemporaryDir dir;
        const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME"), oldData = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_CONFIG_HOME", dir.filePath("config").toUtf8());
        qputenv("XDG_DATA_HOME", dir.filePath("data").toUtf8());
        const int result = runServe({"needful", "serve", "install", "--port", "8899"});
        const QString unit = dir.filePath("config/systemd/user/needful-serve.service");
        const bool written = QFile::exists(unit);
        QFile file(unit);
        file.open(QIODevice::ReadOnly);
        const QByteArray contents = file.readAll();
        auto restore = [](const char *name, const QByteArray &value) {
            if (value.isEmpty()) qunsetenv(name); else qputenv(name, value);
        };
        restore("XDG_CONFIG_HOME", oldConfig);
        restore("XDG_DATA_HOME", oldData);
        QCOMPARE(result, 0);
        QVERIFY(written);
        QVERIFY(contents.contains(" serve --port 8899\n"));
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
