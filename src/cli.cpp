// The command shape follows Hype's (MIT, David Heinemeier Hansson).
#include "cli.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <memory>
#include <optional>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <cstdio>
#include <cstdlib>

#include "backend.h"
#include "devices.h"
#include "peers.h"
#include "serve.h"
#include "universes.h"

namespace {
void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void printJson(const QJsonValue &value) {
    const QJsonDocument document = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    fputs(document.toJson().constData(), stdout);
}
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

struct Command {
    QCommandLineParser parser;
    Command(const QString &name, const QString &description) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, description, name);
        parser.addOption({"data", "Use this data file instead of the usual one", "path"});
        parser.addOption({"universe", "Use another universe's list, like hers (see: needful universe)", "name"});
    }
    void json() { parser.addOption({"json", "Print the result as JSON"}); }
    void context() { parser.addOption({"context", "A context by name or path, like \"Automotive/White Honda CR-V\"", "context"}); }
    bool wantsJson() const { return parser.isSet("json"); }
    QString argument(int index) const { return parser.positionalArguments().value(index); }
    // --data, or the universe named, or empty for the usual file.
    QString dataPath() const {
        if (parser.isSet("data") || !parser.isSet("universe"))
            return parser.value("data");
        const QString path = Universes().dataPath(parser.value("universe"));
        if (path.isEmpty()) {
            print(stderr, QString("No universe named %1. See: needful universe").arg(parser.value("universe")));
            std::exit(1);
        }
        return path;
    }
};

// A context option, or 0 for none; -1 with a message when it names nothing.
int contextOption(const Command &command, const Backend &backend) {
    if (!command.parser.isSet("context"))
        return 0;
    const int id = backend.store().findContext(command.parser.value("context"));
    if (id < 0)
        print(stderr, QString("No context named %1. See: needful contexts").arg(command.parser.value("context")));
    return id;
}

int priorityFrom(const QString &text) {
    const QString t = text.toLower();
    if (t == "low" || t == "1") return 1;
    if (t == "high" || t == "3") return 3;
    if (t == "urgent" || t == "4") return 4;
    if (t.isEmpty() || t == "normal" || t == "2") return 2;
    return 0;
}

// 5-9, 11-2 (wrapping the new year), or 4,5,6.
QVariantList monthsFrom(const QString &text, bool *ok) {
    QVariantList months;
    *ok = true;
    for (const QString &part : text.split(',', Qt::SkipEmptyParts)) {
        const QStringList range = part.split('-');
        const int from = range.value(0).toInt(), to = range.value(range.size() - 1).toInt();
        if (range.size() > 2 || from < 1 || from > 12 || to < 1 || to > 12) {
            *ok = false;
            return {};
        }
        for (int m = from;; m = m % 12 + 1) {
            months.append(m);
            if (m == to)
                break;
        }
    }
    return months;
}

QJsonObject taskJson(const QVariantMap &task) {
    QJsonObject o = QJsonObject::fromVariantMap(task);
    o.remove("kind");
    return o;
}

QString taskLine(const QVariantMap &t) {
    QString line = QString("%1  %2").arg(t["id"].toString().rightJustified(4), t["title"].toString());
    if (!t["contextPath"].toString().isEmpty())
        line += "  [" + t["contextPath"].toString() + "]";
    return line + "  — " + t["dueText"].toString();
}

int list(const QStringList &arguments) {
    Command command("list", "List tasks, most needful first.");
    command.parser.addOption({"view", "needful (the default), today, upcoming, or all", "view", "needful"});
    command.context();
    command.json();
    command.parser.process(arguments);
    Backend backend(command.dataPath());
    if (!backend.error().isEmpty())
        return fail(backend.error());
    const int context = contextOption(command, backend);
    if (context < 0)
        return 1;
    backend.setContextFilter(context);

    const QString view = command.parser.value("view");
    if (!QStringList{"needful", "today", "upcoming", "all"}.contains(view))
        return fail("--view is needful, today, upcoming, or all.");
    QVariantList items;
    for (const QString &each : view == "all" ? QStringList{"today", "needful", "upcoming"} : QStringList{view}) {
        backend.setView(each);
        items += backend.items();
    }
    QJsonArray array;
    for (const auto &item : items)
        array.append(taskJson(item.toMap()));
    if (command.wantsJson())
        printJson(array);
    else if (items.isEmpty())
        print(stdout, "Nothing here.");
    else
        for (const auto &item : items)
            print(stdout, taskLine(item.toMap()));
    return 0;
}

int add(const QStringList &arguments) {
    Command command("add", "Add a task. Give --context more than once to share it as a routine, with a copy in each.");
    command.parser.addPositionalArgument("title", "What needs doing", "<title>");
    command.context();
    command.parser.addOption({"priority", "low, normal (the default), high, or urgent", "priority"});
    command.parser.addOption({"every", "Repeat: 3d, 1w, 2m, 1y", "interval"});
    command.parser.addOption({"fixed", "Repeat on a fixed rhythm rather than from when it's done"});
    command.parser.addOption({"ask", "Ask when it's done when it should come back"});
    command.parser.addOption({"due", "First due date, YYYY-MM-DD (today by default)", "date"});
    command.parser.addOption({"months", "In season only these months: 5-9, 11-2, or 4,5,6", "months"});
    command.parser.addOption({"notes", "Notes on the task", "text"});
    command.json();
    command.parser.process(arguments);
    const QString title = command.parser.positionalArguments().mid(1).join(' ').trimmed();
    if (title.isEmpty())
        return fail("Say what needs doing: needful add \"Mow the lawn\"");
    Backend backend(command.dataPath());
    if (!backend.error().isEmpty())
        return fail(backend.error());
    // Several --context options make a routine with a copy in each.
    QVariantList contexts;
    for (const QString &name : command.parser.values("context")) {
        const int id = backend.store().findContext(name);
        if (id < 0)
            return fail(QString("No context named %1. See: needful contexts").arg(name));
        contexts.append(id);
    }

    QVariantMap fields{{"title", title}, {"notes", command.parser.value("notes")}, {"contexts", contexts},
                       {"scheduleKind", "once"}, {"fromDone", !command.parser.isSet("fixed")}};
    const int priority = priorityFrom(command.parser.value("priority"));
    if (!priority)
        return fail("--priority is low, normal, high, or urgent.");
    fields["priority"] = priority;
    if (command.parser.isSet("ask"))
        fields["scheduleKind"] = "ask";
    if (command.parser.isSet("every")) {
        static const QRegularExpression interval("^(\\d+)\\s*([dwmy])$");
        const auto match = interval.match(command.parser.value("every").trimmed().toLower());
        if (!match.hasMatch() || match.captured(1).toInt() < 1)
            return fail("--every takes a count and a unit: 3d, 1w, 2m, or 1y.");
        static const QHash<QString, QString> units{{"d", "day"}, {"w", "week"}, {"m", "month"}, {"y", "year"}};
        fields["scheduleKind"] = "every";
        fields["every"] = match.captured(1).toInt();
        fields["unit"] = units.value(match.captured(2));
    }
    if (command.parser.isSet("due")) {
        if (!QDate::fromString(command.parser.value("due"), Qt::ISODate).isValid())
            return fail("--due takes a date like 2026-05-01.");
        fields["due"] = command.parser.value("due");
    }
    if (command.parser.isSet("months")) {
        bool ok = false;
        fields["months"] = monthsFrom(command.parser.value("months"), &ok);
        if (!ok)
            return fail("--months takes month numbers: 5-9, 11-2, or 4,5,6.");
    }
    const int id = backend.saveTask(fields);
    if (!id)
        return fail(backend.error().isEmpty() ? "Could not add the task." : backend.error());
    // A routine reports every copy it made.
    const int routine = backend.store().task(id)->routine;
    const QList<int> ids = routine ? backend.store().copies(routine) : QList<int>{id};
    QJsonArray array;
    for (int each : ids)
        array.append(taskJson(backend.task(each)));
    if (command.wantsJson())
        printJson(routine ? QJsonValue(array) : array.first());
    else
        for (int each : ids)
            print(stdout, "Added " + taskLine(backend.task(each)));
    return 0;
}

// Commands that act on one task by its number.
const Task *taskArgument(const Command &command, const Backend &backend) {
    bool ok = false;
    const int id = command.argument(1).toInt(&ok);
    const Task *task = ok ? backend.store().task(id) : nullptr;
    if (!task)
        print(stderr, QString("No task %1. See: needful list --view all").arg(command.argument(1)));
    return task;
}

int done(const QStringList &arguments) {
    Command command("done", "Mark a task done, with an optional note for its history.");
    command.parser.addPositionalArgument("task", "The task's number, from needful list", "<task>");
    command.parser.addOption({"note", "What was done, for next time", "text"});
    command.parser.addOption({"next", "For tasks that ask: when it's next due, YYYY-MM-DD", "date"});
    command.json();
    command.parser.process(arguments);
    Backend backend(command.dataPath());
    if (!backend.error().isEmpty())
        return fail(backend.error());
    const Task *task = taskArgument(command, backend);
    if (!task)
        return 1;
    const QString next = command.parser.value("next");
    if (!next.isEmpty() && !QDate::fromString(next, Qt::ISODate).isValid())
        return fail("--next takes a date like 2026-05-01.");
    const int id = task->id;
    backend.complete(id, command.parser.value("note"), next);
    if (!backend.error().isEmpty())
        return fail(backend.error());
    if (command.wantsJson())
        printJson(taskJson(backend.task(id)));
    else
        print(stdout, "Done " + taskLine(backend.task(id)));
    return 0;
}

int snooze(const QStringList &arguments) {
    Command command("snooze", "Hide a task for some days.");
    command.parser.addPositionalArgument("task", "The task's number", "<task>");
    command.parser.addPositionalArgument("days", "How many days", "<days>");
    command.parser.process(arguments);
    Backend backend(command.dataPath());
    const Task *task = taskArgument(command, backend);
    const int days = command.argument(2).toInt();
    if (!task)
        return 1;
    if (days < 1)
        return fail("Say how many days: needful snooze 12 7");
    backend.snooze(task->id, days);
    return backend.error().isEmpty() ? 0 : fail(backend.error());
}

int today(const QStringList &arguments) {
    Command command("today", "Pull a task into today's list, or put it back.");
    command.parser.addPositionalArgument("task", "The task's number", "<task>");
    command.parser.process(arguments);
    Backend backend(command.dataPath());
    const Task *task = taskArgument(command, backend);
    if (!task)
        return 1;
    backend.toggleToday(task->id);
    return backend.error().isEmpty() ? 0 : fail(backend.error());
}

int history(const QStringList &arguments) {
    Command command("history", "Show what was done and noted, newest first.");
    command.context();
    command.parser.addOption({"task", "Only this task's history", "task"});
    command.parser.addOption({"search", "Only entries mentioning this", "text"});
    command.json();
    command.parser.process(arguments);
    Backend backend(command.dataPath());
    if (!backend.error().isEmpty())
        return fail(backend.error());
    const int context = contextOption(command, backend);
    if (context < 0)
        return 1;
    backend.setContextFilter(context);
    backend.setSearch(command.parser.value("search"));
    backend.setView("history");
    const int only = command.parser.value("task").toInt();
    QJsonArray array;
    for (const auto &item : backend.items()) {
        const QVariantMap c = item.toMap();
        if (only && c["task"].toInt() != only)
            continue;
        // JSON keeps "kind": a completion, or a log note.
        array.append(QJsonObject::fromVariantMap(c));
        if (command.wantsJson())
            continue;
        const QString where = c["contextPath"].toString().isEmpty() ? "" : "  [" + c["contextPath"].toString() + "]";
        if (c["kind"] == "log") {
            QString followUp;
            if (c["task"].toInt())
                followUp = c["followUpDone"].toBool() ? "\n      followed up"
                                                      : "\n      follow-up " + c["task"].toString() + ": " +
                                                            c["followUpDueText"].toString();
            print(stdout, QString("%1  note: %2%3%4").arg(c["date"].toString(), c["text"].toString(), where, followUp));
        } else {
            print(stdout, QString("%1  %2%3%4").arg(c["date"].toString(), c["title"].toString(), where,
                                                   c["note"].toString().isEmpty() ? "" : "\n      " + c["note"].toString()));
        }
    }
    if (command.wantsJson())
        printJson(array);
    return 0;
}

int logNote(const QStringList &arguments) {
    Command command("log", "Note something on a context, optionally with a one-time task to follow it up.");
    command.parser.addPositionalArgument("text", "What you noticed", "<text>");
    command.context();
    command.parser.addOption({"follow-up", "Add a one-time task to follow it up"});
    command.parser.addOption({"priority", "For the follow-up: low, normal (the default), high, or urgent", "priority"});
    command.parser.addOption({"due", "For the follow-up: YYYY-MM-DD (today by default)", "date"});
    command.json();
    command.parser.process(arguments);
    const QString text = command.parser.positionalArguments().mid(1).join(' ').trimmed();
    if (text.isEmpty())
        return fail("Say what you noticed: needful log \"Squeak from the front left\" --context CR-V");
    Backend backend(command.dataPath());
    if (!backend.error().isEmpty())
        return fail(backend.error());
    const int context = contextOption(command, backend);
    if (context < 0)
        return 1;
    const int priority = priorityFrom(command.parser.value("priority"));
    if (!priority)
        return fail("--priority is low, normal, high, or urgent.");
    const QString due = command.parser.value("due");
    if (!due.isEmpty() && !QDate::fromString(due, Qt::ISODate).isValid())
        return fail("--due takes a date like 2026-05-01.");
    const int id = backend.addLog({{"context", context}, {"text", text}, {"followUp", command.parser.isSet("follow-up")},
                                   {"priority", priority}, {"due", due}});
    if (!id)
        return fail(backend.error().isEmpty() ? "Could not log the note." : backend.error());
    const LogEntry *entry = backend.store().logEntry(id);
    if (command.wantsJson()) {
        QJsonObject object{{"id", id}, {"context", entry->context}, {"text", entry->text},
                           {"at", entry->at.toString(Qt::ISODate)}, {"task", entry->task}};
        if (entry->task)
            object["followUp"] = taskJson(backend.task(entry->task));
        printJson(object);
    } else {
        print(stdout, "Logged note " + QString::number(id));
        if (entry->task)
            print(stdout, "Follow-up " + taskLine(backend.task(entry->task)));
    }
    return 0;
}

int contexts(const QStringList &arguments) {
    Command command("contexts", "List contexts, or add one with: contexts add \"Automotive/White Honda CR-V\"");
    command.parser.addPositionalArgument("add", "Add a context path, making any missing parents", "[add <path>]");
    command.json();
    command.parser.process(arguments);
    Backend backend(command.dataPath());
    if (!backend.error().isEmpty())
        return fail(backend.error());
    if (command.argument(1) == "add") {
        const QStringList names = command.parser.positionalArguments().mid(2).join(' ').split('/', Qt::SkipEmptyParts);
        if (names.isEmpty())
            return fail("Name the context: needful contexts add Home");
        int parent = 0;
        QString path;
        for (const QString &name : names) {
            path += (path.isEmpty() ? "" : "/") + name.trimmed();
            const int existing = backend.store().findContext(path);
            parent = existing > 0 ? existing : backend.addContext(name, parent);
            if (!parent)
                return fail(backend.error().isEmpty() ? "Could not add the context." : backend.error());
        }
    } else if (!command.argument(1).isEmpty()) {
        return fail("Use needful contexts, or needful contexts add <path>.");
    }
    QJsonArray array;
    for (const auto &item : backend.contexts()) {
        const QVariantMap c = item.toMap();
        array.append(QJsonObject::fromVariantMap(c));
        if (!command.wantsJson())
            print(stdout, QString(c["depth"].toInt() * 2, ' ') + c["name"].toString() +
                              (c["due"].toInt() ? QString("  (%1 due)").arg(c["due"].toInt()) : QString()));
    }
    if (command.wantsJson())
        printJson(array);
    return 0;
}

int skill(const QStringList &arguments) {
    Command command("skill", "Print the agent skill, or install it for your coding agents with: skill install.");
    command.parser.addPositionalArgument("install", "Copy to ~/.agents/skills/needful and link into ~/.claude/skills",
                                         "[install]");
    command.parser.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    if (command.argument(1).isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (command.argument(1) != "install")
        return fail("Use needful skill to print the skill, or needful skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/needful");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude"))
        return 0;
    const QString link = home.filePath(".claude/skills/needful");
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/needful", link))
        return fail("Could not link " + link);
    print(stdout, "Linked " + link);
    return 0;
}

// ~/Sync/needful rather than /home/someone/Sync/needful.
QString shortPath(const QString &path) {
    const QString home = QDir::homePath();
    return path == home || path.startsWith(home + "/") ? "~" + path.mid(home.size()) : path;
}

QString count(int n) { return QLocale().toString(n); }

// A timer that swaps every machine's file with another computer over rsync
// and SSH, for keeping in step with no sync tool or service at all.
int syncRsync(const Command &command, const SyncFolder &folder) {
    const QString step = command.argument(2), host = command.argument(3);
#ifdef Q_OS_MACOS
    constexpr bool OnMac = true;
#else
    constexpr bool OnMac = false;
#endif
    // A systemd timer on Linux, a launchd agent on macOS.
    QDir units(OnMac ? launchAgentsDirectory() : systemdUserDirectory());
    const QString systemctl = OnMac ? QString() : QStandardPaths::findExecutable("systemctl");
    if (step == "uninstall") {
        if (OnMac) {
            if (!units.exists(RsyncAgent))
                return fail("Nothing to remove; no rsync agent in " + units.path());
            unloadLaunchAgent(RsyncAgent);
            if (units.remove(RsyncAgent))
                print(stdout, "Removed " + units.filePath(RsyncAgent));
            return 0;
        }
        if (!units.exists(RsyncService) && !units.exists(RsyncTimer))
            return fail("Nothing to remove; no rsync timer in " + units.path());
        if (!systemctl.isEmpty())
            QProcess::execute(systemctl, {"--user", "disable", "--now", RsyncTimer});
        for (const QString &name : {RsyncTimer, RsyncService})
            if (units.remove(name))
                print(stdout, "Removed " + units.filePath(name));
        if (!systemctl.isEmpty())
            QProcess::execute(systemctl, {"--user", "daemon-reload"});
        return 0;
    }
    if (step != "install" || host.isEmpty())
        return fail("Use needful sync rsync install <host> (as ssh knows it), or needful sync rsync uninstall.");

    RsyncSetup setup;
    setup.rsync = findRsync();
    if (setup.rsync.isEmpty() || QStandardPaths::findExecutable("ssh").isEmpty())
        return fail("This needs rsync and ssh, here and on " + host + ". On Arch: pacman -S rsync openssh. "
                    "macOS has both; brew install rsync gives it a newer one.");
    setup.modern = rsyncIsModern(setup.rsync);
    if (!OnMac && systemctl.isEmpty())
        return fail("This needs systemd's user services to run the timer.");
    static const QRegularExpression interval("^[1-9]\\d*(s|m|h)$");
    setup.every = command.parser.value("every").trimmed();
    if (!interval.match(setup.every).hasMatch())
        return fail("--every takes a count and a unit: 30s, 5m, or 1h.");
    setup.local = folder.folder();
    setup.host = host;
    const QString home = QDir::homePath();
    setup.remote = command.parser.isSet("remote-folder") ? command.parser.value("remote-folder")
                   : setup.local.startsWith(home + "/")   ? setup.local.mid(home.size() + 1)
                                                          : setup.local;

    // Once now, so a missing key or folder shows up here and not in a log.
    print(stdout, "Swapping changes with " + host + "…");
    fflush(stdout);
    const QString remoteShown = setup.remote.startsWith('/') ? setup.remote : "~/" + setup.remote;
    for (const QStringList &run : rsyncCommands(setup)) {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(run.first(), run.mid(1));
        process.waitForFinished(-1);
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            return fail(QString::fromUtf8(process.readAll()).trimmed() +
                        "\n\nCouldn't swap changes with " + host + ". Check that:\n"
                        "  ssh -o BatchMode=yes " + host + " true    works with no password prompt\n"
                        "  needful sync setup " + remoteShown + "    has been run on " + host +
                        "\n(or give its folder with --remote-folder)");
    }

    if (!units.mkpath("."))
        return fail("Could not create " + units.path());
    const auto files = OnMac ? rsyncLaunchAgents(setup) : rsyncUnits(setup);
    for (auto it = files.begin(); it != files.end(); ++it) {
        QSaveFile file(units.filePath(it.key()));
        if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) < 0 || !file.commit())
            return fail("Could not write " + file.fileName());
        print(stdout, "Wrote " + file.fileName());
    }
    if (OnMac) {
        QDir().mkpath(QDir::homePath() + "/Library/Logs");
        if (!QStandardPaths::findExecutable("launchctl").isEmpty() && !loadLaunchAgent(units.path(), RsyncAgent))
            return fail("launchctl could not load " + RsyncAgent);
    } else {
        QProcess::execute(systemctl, {"--user", "daemon-reload"});
        if (QProcess::execute(systemctl, {"--user", "enable", "--now", RsyncTimer}) != 0)
            return fail("Could not start the timer: systemctl --user enable --now " + RsyncTimer);
    }
    print(stdout, QString("Swapping changes with %1 every %2, while this computer is on. "
                          "Only one of the two computers needs this timer.\n"
                          "Stop with: needful sync rsync uninstall").arg(host, setup.every));
    return 0;
}

// --- Peer sync: your computers, paired by code, with nothing in between.

QString linkName(const QString &link) {
    if (link == "local") return "connected on this network";
    if (link == "direct") return "connected directly";
    if (link == "relay") return "connected through the relay";
    return {};
}

// "not reachable since Tuesday", for a machine not connected now.
QString unreachableText(const QDateTime &seen, bool running) {
    if (!running)
        return "not connected (Needful isn't running here)";
    if (!seen.isValid())
        return "not reachable yet";
    const QDate day = seen.toLocalTime().date(), today = QDate::currentDate();
    if (day == today)
        return "not reachable since " + QLocale().toString(seen.toLocalTime().time(), "H:mm");
    if (day.daysTo(today) < 7)
        return "not reachable since " + QLocale().dayName(day.dayOfWeek());
    return "not reachable since " + day.toString(Qt::ISODate);
}

// Pairing and joining go through whichever Needful holds the link here, or,
// when none does, this command holds it until it's done.
struct Holder {
    std::unique_ptr<PeerLink> link;
    std::unique_ptr<LinkClient> client;
    QString error;
    bool open(const QString &usual) {
        link = std::make_unique<PeerLink>(usual);
        if (link->start(&error))
            return true;
        link.reset();
        if (!error.isEmpty())
            return false;
        client = std::make_unique<LinkClient>(PeerLink::appDirFor(usual));
        if (client->connectToOwner())
            return true;
        error = "Another Needful here holds the link but isn't answering. Close it and try again.";
        return false;
    }
};

// Prints what pairing says, and ends the loop on a failure, or when paired
// unless onPaired says to wait longer.
template <typename Source> void wire(Source *source, QEventLoop *loop, int *code, std::function<void()> onPaired = {}) {
    QObject::connect(source, &Source::failed, loop, [loop, code](const QString &message) {
        print(stderr, message);
        *code = 1;
        loop->quit();
    });
    QObject::connect(source, &Source::pairCode, loop, [](const QString &pairCode, const QString &name, const QString &fp) {
        print(stdout, "Pair code: " + pairCode);
        print(stdout, QString("This machine: \"%1\"  %2").arg(name, fp));
        print(stdout, QString::fromUtf8("Waiting for a computer… (the code works once, for a few minutes)"));
        print(stdout, "On the other computer: needful sync join " + pairCode);
        fflush(stdout);
    });
    QObject::connect(source, &Source::paired, loop, [onPaired, loop](const QString &name, const QString &fp) {
        print(stdout, QString("Paired with \"%1\"  %2").arg(name, fp));
        fflush(stdout);
        if (onPaired)
            onPaired();
        else
            loop->quit();
    });
}

int syncPair(const QString &usual) {
    Holder holder;
    if (!holder.open(usual))
        return fail(holder.error);
    QEventLoop loop;
    int code = 0;
    stopOnSignals();
    if (holder.link) {
        // Holding the link only for this, so stay until the new computer has
        // what it needs from here and has sent back its own changes.
        PeerLink *link = holder.link.get();
        wire(link, &loop, &code, [&loop] { QTimer::singleShot(120 * 1000, &loop, &QEventLoop::quit); });
        QObject::connect(link, &PeerLink::received, &loop, [&loop, link](const QString &list) {
            if (list == link->listIdOf(Universes::Mine))
                QTimer::singleShot(1500, &loop, &QEventLoop::quit);
        });
        link->pair();
    } else {
        wire(holder.client.get(), &loop, &code);
        holder.client->pair();
    }
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, &loop, &QEventLoop::quit);
    loop.exec();
    return code;
}

int syncJoin(const QString &usual, const QString &pairCode, const QString &at, bool yes) {
    Holder holder;
    if (!holder.open(usual))
        return fail(holder.error);
    QEventLoop loop;
    int code = 0;
    stopOnSignals();
    auto ask = [yes](const QString &name, const QString &fp) {
        if (yes)
            return true;
        fprintf(stdout, "Pair with \"%s\"  %s? [y/N] ", qPrintable(name), qPrintable(fp));
        fflush(stdout);
        char line[64] = {0};
        if (!fgets(line, sizeof line, stdin))
            return false;
        const QString answer = QString::fromUtf8(line).trimmed().toLower();
        return answer == "y" || answer == "yes";
    };
    auto onJoined = [](const QVariantMap &result) {
        print(stdout, QString("Paired with \"%1\".").arg(result["name"].toString()));
        if (result["already"].toBool()) {
            print(stdout, "This computer already had its list.");
        } else {
            const int tasks = result["tasks"].toInt();
            print(stdout, tasks ? QString("Joined its list. Merged %1 %2 that %3 only here.")
                                      .arg(count(tasks), tasks == 1 ? "task" : "tasks", tasks == 1 ? "was" : "were")
                                : QString("Joined its list. Nothing was only here."));
        }
        print(stdout, QString("This machine is \"%1\".").arg(result["this"].toString()));
        fflush(stdout);
    };
    auto connectAll = [&](auto *source) {
        using Source = std::remove_pointer_t<decltype(source)>;
        wire(source, &loop, &code);
        QObject::connect(source, &Source::confirm, &loop, [source, ask, &loop, &code](const QString &name, const QString &fp) {
            const bool accept = ask(name, fp);
            source->answer(accept);
            if (!accept) {
                print(stderr, "Not paired.");
                code = 1;
                QTimer::singleShot(300, &loop, &QEventLoop::quit);
            }
        });
    };
    if (holder.link) {
        PeerLink *link = holder.link.get();
        connectAll(link);
        QObject::connect(link, &PeerLink::joined, &loop, [&loop, onJoined, link](const QVariantMap &result) {
            onJoined(result);
            // Stay until the other computer has this one's changes.
            QTimer::singleShot(15 * 1000, &loop, &QEventLoop::quit);
            QObject::connect(link, &PeerLink::sent, &loop, [&loop, link](const QString &list) {
                if (list == link->listIdOf(Universes::Mine))
                    QTimer::singleShot(1500, &loop, &QEventLoop::quit);
            });
        });
        link->join(pairCode, at);
    } else {
        LinkClient *client = holder.client.get();
        connectAll(client);
        QObject::connect(client, &LinkClient::joined, &loop, [&loop, onJoined](const QVariantMap &result) {
            onJoined(result);
            loop.quit();
        });
        client->join(pairCode, at);
    }
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, &loop, &QEventLoop::quit);
    loop.exec();
    return code;
}

int syncLinkCommand(const QString &usual) {
    PeerLink link(usual);
    QString error;
    if (!link.start(&error))
        return fail(error.isEmpty() ? QString("Another Needful here already keeps the link.") : error);
    print(stdout, QString("Keeping \"%1\" in step with its paired computers until stopped.").arg(link.name()));
    fflush(stdout);
    stopOnSignals();
    return QCoreApplication::exec();
}

int syncInstall(const QString &dataOverride, bool remove) {
    QDir units(systemdUserDirectory());
    const QString systemctl = QStandardPaths::findExecutable("systemctl");
    if (remove) {
        if (!units.exists(LinkService))
            return fail("Nothing to remove; no " + LinkService + " in " + units.path());
        if (!systemctl.isEmpty())
            QProcess::execute(systemctl, {"--user", "disable", "--now", LinkService});
        units.remove(LinkService);
        print(stdout, "Removed " + units.filePath(LinkService));
        if (!systemctl.isEmpty())
            QProcess::execute(systemctl, {"--user", "daemon-reload"});
        return 0;
    }
    if (systemctl.isEmpty())
        return fail("This needs systemd's user services to keep the link running.");
    if (!units.mkpath("."))
        return fail("Could not create " + units.path());
    const auto files = linkUnits(QCoreApplication::applicationFilePath(), dataOverride);
    for (auto it = files.begin(); it != files.end(); ++it) {
        QSaveFile file(units.filePath(it.key()));
        if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) < 0 || !file.commit())
            return fail("Could not write " + file.fileName());
        print(stdout, "Wrote " + file.fileName());
    }
    QProcess::execute(systemctl, {"--user", "daemon-reload"});
    if (QProcess::execute(systemctl, {"--user", "enable", "--now", LinkService}) != 0)
        return fail("Could not start it: systemctl --user enable --now " + LinkService);
    print(stdout, "The link to your paired computers stays up while this computer is on.\n"
                  "Stop with: needful sync uninstall");
    return 0;
}

int syncPeers(const Command &command, const QString &action, const QStringList &rest, const QString &usual) {
    const QString appDir = PeerLink::appDirFor(usual);
    Devices devices(appDir);
    QString error;
    if (!devices.load(&error))
        return fail(error);
    const QDateTime now = QDateTime::currentDateTime();
    const QString wanted = rest.value(0).trimmed();
    auto findDevice = [&](std::optional<Devices::Device> *found) {
        if (wanted.isEmpty()) {
            print(stderr, "Name a paired computer. See: needful sync status");
            return false;
        }
        *found = devices.find(wanted);
        if (!*found || !(*found)->paired) {
            print(stderr, QString("No paired computer named %1. See: needful sync status").arg(wanted));
            return false;
        }
        return true;
    };

    if (action == "unpair") {
        std::optional<Devices::Device> found;
        if (!findDevice(&found))
            return 1;
        devices.unpair(found->id, now);
        if (!devices.save(&error))
            return fail(error);
        if (command.wantsJson())
            printJson(QJsonObject{{"unpaired", found->name}, {"fingerprint", found->fingerprint()}});
        else
            print(stdout, QString("Unpaired \"%1\". It can no longer connect. Its changes stay.").arg(found->name));
        return 0;
    }
    if (action == "reach") {
        const bool self = wanted.compare(devices.name(), Qt::CaseInsensitive) == 0;
        std::optional<Devices::Device> found;
        if (self)
            found = devices.device(devices.id());
        else if (!findDevice(&found))
            return 1;
        const QString host = rest.mid(1).join(' ').trimmed();
        if (host.isEmpty()) {
            print(stdout, found->reach.isEmpty() ? QString("Looking for \"%1\" only on this network.").arg(found->name)
                                                 : QString("Looking for \"%1\" at %2.").arg(found->name, found->reach.join(", ")));
            return 0;
        }
        QStringList hosts = found->reach;
        if (host == "off" || host == "none") {
            hosts.clear();
        } else if (!hosts.contains(host)) {
            hosts.append(host);
        }
        devices.setReach(found->id, hosts, now);
        if (!devices.save(&error))
            return fail(error);
        print(stdout, hosts.isEmpty() ? QString("Will look for \"%1\" only on this network.").arg(found->name)
                                      : QString("Will also look for \"%1\" at %2.").arg(found->name, host));
        return 0;
    }
    // action == "relay"
    const QString url = wanted;
    if (url.isEmpty()) {
        print(stdout, devices.relay().isEmpty() ? QString("No relay. Paired computers connect only when one can reach the other.")
                                                : "Relay: " + devices.relay());
        return 0;
    }
    if (url == "off" || url == "none") {
        devices.setRelay({}, now);
        if (!devices.save(&error))
            return fail(error);
        print(stdout, "No relay. Paired computers connect only when one can reach the other.");
        return 0;
    }
    const QUrl parsed(url);
    if (!parsed.isValid() || (parsed.scheme() != "https" && parsed.scheme() != "http") || parsed.host().isEmpty())
        return fail("Give the relay's address, like https://relay.example.net");
    devices.setRelay(url, now);
    if (!devices.save(&error))
        return fail(error);
    print(stdout, QString("Paired computers that can't reach each other will meet through\n"
                          "%1. It passes along encrypted traffic and can't read it.").arg(parsed.host()));
    return 0;
}

// sync status for a computer with paired computers.
int peerStatus(const Command &command, const Backend &backend, const SyncFolder *folder, const QString &usual) {
    const PeerStatus status = PeerStatus::read(PeerLink::appDirFor(usual), folder);
    QSet<QString> shown;
    if (command.wantsJson()) {
        QJsonArray machines;
        for (const auto &e : status.devices) {
            machines.append(QJsonObject{
                {"name", e.device.name},
                {"fingerprint", e.device.fingerprint()},
                {"link", e.link.isEmpty() ? QJsonValue() : QJsonValue(e.link)},
                {"lastSeen", e.seen.isValid() ? QJsonValue(e.seen.toString(Qt::ISODateWithMs)) : QJsonValue()},
                {"lastChange", e.changed.isValid() ? QJsonValue(e.changed.toString(Qt::ISODateWithMs)) : QJsonValue()},
                {"reach", QJsonArray::fromStringList(e.device.reach)}});
        }
        QJsonObject out{{"this", status.name},
                        {"fingerprint", status.fingerprint},
                        {"running", status.running},
                        {"relay", status.relay.isEmpty() ? QJsonValue() : QJsonValue(status.relay)},
                        {"machines", machines}};
        if (folder && !QDir::cleanPath(folder->folder()).startsWith(PeerLink::appDirFor(usual) + "/lists/"))
            out["folder"] = folder->folder();
        printJson(out);
        return 0;
    }
    int width = 6;
    for (const auto &e : status.devices)
        width = std::max(width, int(e.device.name.size()));
    print(stdout, QString("This").leftJustified(width + 2) + status.name + "  " + status.fingerprint);
    if (folder && !QDir::cleanPath(folder->folder()).startsWith(PeerLink::appDirFor(usual) + "/lists/"))
        print(stdout, QString("Folder").leftJustified(width + 2) + shortPath(folder->folder()));
    for (const auto &e : status.devices) {
        const QString link = e.link.isEmpty() ? unreachableText(e.seen, status.running) : linkName(e.link);
        const QString change = !e.changed.isValid() ? QString("no changes yet")
                               : e.changed.daysTo(QDateTime::currentDateTime()) < 7
                                   ? "last change " + backend.heardText(e.changed)
                                   : "last change " + e.changed.toLocalTime().date().toString(Qt::ISODate);
        print(stdout, e.device.name.leftJustified(width + 2) + link + QString::fromUtf8(" · ") + change);
        shown.insert(e.device.id);
    }
    // Machines that sync only through the folder, and ones unpaired since.
    if (folder) {
        const auto files = folder->files();
        for (const auto &m : folder->machines()) {
            const QString device = files.value(m.id).device;
            if (m.forgotten || m.id == folder->machine() || shown.contains(device))
                continue;
            print(stdout, m.name.leftJustified(width + 2) + (device.isEmpty() ? "through the folder" : "unpaired") +
                              QString::fromUtf8(" · ") +
                              (m.changed.isValid() ? "last change " + backend.heardText(m.changed) : QString("no changes yet")));
        }
    }
    if (status.devices.isEmpty())
        print(stdout, "\nNo paired computers. Pair one with: needful sync pair");
    if (!status.running && !status.devices.isEmpty())
        print(stdout, "\nNeedful isn't running here, so nothing is passed along until it is. Open the window, or "
                      "keep it running with: needful sync install");
    if (!status.relay.isEmpty())
        print(stdout, "Relay  " + status.relay);
    return 0;
}

int sync(const QStringList &arguments) {
    Command command("sync", "Keep the same Needful on your computers: paired with each other by code, or through "
                            "a folder a sync tool keeps in step.");
    command.parser.addPositionalArgument("action", "pair, join <code>, status, unpair <machine>, reach <machine> <host>, "
                                                   "relay <url>|off, install, uninstall, setup <folder>, rename <name>, "
                                                   "forget <machine>, or rsync install <host> / rsync uninstall",
                                         "<action>");
    command.parser.addOption({"at", "With join: the other computer's address, when it isn't on this network", "host"});
    command.parser.addOption({"yes", "With join: pair without asking"});
    command.parser.addOption({"name", "With setup: what to call this machine (its host name by default)", "name"});
    command.parser.addOption({"remote-folder", "With rsync install: the sync folder on the other computer "
                                               "(the same path as here, from its home folder)", "path"});
    command.parser.addOption({"every", "With rsync install: how often to swap changes: 30s, 5m, 1h (5m)", "interval", "5m"});
    command.json();
    command.parser.process(arguments);
    const QString action = command.argument(1), argument = command.parser.positionalArguments().mid(2).join(' ');
    const QString dataPath = command.dataPath().isEmpty() ? Backend::defaultDataPath() : command.dataPath();
    // Pairing is for the whole install, whichever universe is asked about.
    const QString usual = command.parser.isSet("data") ? command.parser.value("data") : Backend::defaultDataPath();
    if (action == "pair")
        return syncPair(usual);
    if (action == "join") {
        if (command.argument(2).isEmpty())
            return fail("Type the code the other computer shows: needful sync join 7-violet-crumble-tango");
        return syncJoin(usual, command.parser.positionalArguments().mid(2).join('-'), command.parser.value("at"),
                        command.parser.isSet("yes"));
    }
    if (action == "link")
        return syncLinkCommand(usual);
    if (action == "install" || action == "uninstall")
        return syncInstall(command.parser.isSet("data") ? QFileInfo(usual).absoluteFilePath()
                                                        : qEnvironmentVariable("NEEDFUL_DATA"),
                           action == "uninstall");
    if (action == "unpair" || action == "reach" || action == "relay")
        return syncPeers(command, action, command.parser.positionalArguments().mid(2), usual);
    // Once this computer has paired, its status is about its computers.
    const bool peers = QFile::exists(PeerLink::appDirFor(usual) + "/devices.json") && [&] {
        Devices devices(PeerLink::appDirFor(usual));
        return devices.load(nullptr, false) && !devices.id().isEmpty() && !devices.devices().isEmpty();
    }();

    if (action == "setup") {
        if (argument.trimmed().isEmpty())
            return fail("Name the folder your sync tool shares: needful sync setup ~/Sync/needful");
        SyncFolder::Setup result;
        QString error;
        if (!SyncFolder::setup(dataPath, argument, command.parser.value("name"), QDateTime::currentDateTime(), &result,
                               &error, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/needful.json"))
            return fail(error);
        const QString folder = shortPath(QDir::cleanPath(QFileInfo(argument).absoluteFilePath()));
        if (command.wantsJson()) {
            printJson(QJsonObject{{"folder", QDir::cleanPath(QFileInfo(argument).absoluteFilePath())},
                                  {"this", result.name},
                                  {"joined", result.joined},
                                  {"already", result.already},
                                  {"others", QJsonArray::fromStringList(result.others)},
                                  {"tasks", result.tasks},
                                  {"contexts", result.contexts},
                                  {"history", result.history}});
        } else if (result.already) {
            print(stdout, QString("Already syncing with %1. This machine is \"%2\".").arg(folder, result.name));
        } else if (result.joined) {
            QStringList quoted;
            for (const QString &name : result.others)
                quoted.append("\"" + name + "\"");
            const QString merged = result.tasks ? QString(" Merged %1 %2 that %3 only here.")
                                                      .arg(count(result.tasks), result.tasks == 1 ? "task" : "tasks",
                                                           result.tasks == 1 ? "was" : "were")
                                                : QString(" Nothing was only here.");
            print(stdout, QString("Joined %1 (from %2).%3").arg(folder, quoted.join(", "), merged));
            print(stdout, QString("This machine is \"%1\".").arg(result.name));
        } else {
            print(stdout, QString("Moved %1 %2, %3 %4, and %5 history %6 into %7.")
                              .arg(count(result.tasks), result.tasks == 1 ? "task" : "tasks", count(result.contexts),
                                   result.contexts == 1 ? "context" : "contexts", count(result.history),
                                   result.history == 1 ? "entry" : "entries", folder));
            print(stdout, QString("This machine is \"%1\". Run the same command on your other computers.").arg(result.name));
        }
        return 0;
    }

    Backend backend(dataPath);
    if (!backend.error().isEmpty())
        return fail(backend.error());
    SyncFolder *folder = backend.sync();
    if (!folder) {
        if (action == "status" && command.wantsJson()) {
            printJson(QJsonObject{{"folder", QJsonValue::Null}});
            return 0;
        }
        if (action == "status") {
            print(stdout, "This machine doesn't sync. Set it up with: needful sync setup <folder>");
            return 0;
        }
        return fail("This machine doesn't sync yet. Set it up with: needful sync setup <folder>");
    }
    QList<SyncFolder::Machine> machines;
    for (const auto &m : folder->machines())
        if (!m.forgotten)
            machines.append(m);

    if ((action == "status" || action.isEmpty()) && peers)
        return peerStatus(command, backend, folder, usual);
    if (action == "status" || action.isEmpty()) {
        if (command.wantsJson()) {
            QJsonArray array;
            for (const auto &m : machines)
                array.append(QJsonObject{{"id", m.id},
                                         {"name", m.name},
                                         {"this", m.id == folder->machine()},
                                         {"lastChange", m.changed.isValid() ? QJsonValue(m.changed.toString(Qt::ISODateWithMs))
                                                                            : QJsonValue::Null}});
            printJson(QJsonObject{{"folder", folder->folder()}, {"this", folder->name()}, {"machines", array}});
            return 0;
        }
        int width = 6;
        for (const auto &m : machines)
            width = std::max(width, int(m.name.size()));
        print(stdout, QString("Folder").leftJustified(width + 2) + shortPath(folder->folder()));
        print(stdout, QString("This").leftJustified(width + 2) + folder->name());
        for (const auto &m : machines)
            print(stdout, m.name.leftJustified(width + 2) +
                              (m.changed.isValid() ? "last change " + backend.heardText(m.changed) : QString("no changes yet")));
        return 0;
    }

    if (action == "rsync")
        return syncRsync(command, *folder);

    if (action == "rename") {
        QString error;
        if (!folder->rename(argument, &error))
            return fail(error);
        if (peers) {
            // Your paired computers show it by the same name.
            Devices devices(PeerLink::appDirFor(usual));
            if (devices.load(&error)) {
                devices.setName(devices.id(), folder->name(), QDateTime::currentDateTime());
                devices.save(&error);
            }
        }
        print(stdout, QString("This machine is now \"%1\".").arg(folder->name()));
        return 0;
    }

    if (action == "forget") {
        if (argument.trimmed().isEmpty())
            return fail("Name the machine to forget: needful sync forget old-laptop");
        const SyncFolder::Machine *found = nullptr;
        for (const auto &m : machines)
            if (m.name.compare(argument.trimmed(), Qt::CaseInsensitive) == 0 || m.id == argument.trimmed())
                found = &m;
        if (!found)
            return fail(QString("No machine named %1. See: needful sync status").arg(argument.trimmed()));
        if (found->id == folder->machine())
            return fail(QString("\"%1\" is this machine. Run it from another computer.").arg(found->name));
        QString error;
        if (!folder->forget(found->id, &error))
            return fail(error);
        print(stdout, QString("Forgot \"%1\" (last change %2). Its %3 %4 stay.")
                          .arg(found->name,
                               found->changed.isValid() ? found->changed.toLocalTime().date().toString(Qt::ISODate) : "never",
                               count(found->records), found->records == 1 ? "change" : "changes"));
        return 0;
    }
    return fail("Use needful sync pair, sync join <code>, sync status, sync unpair <machine>, sync setup <folder>, "
                "sync rename <name>, sync forget <machine>, or sync rsync install <host>.");
}

int universe(const QStringList &arguments) {
    Command command("universe", "List universes, or add or remove one. A universe is a separate list, like "
                                "someone else's, that you open with --universe or from the window's switcher.");
    command.parser.addPositionalArgument("action", "add <name> [<folder>], or remove <name>", "[action]");
    command.parser.addOption({"name", "With add and a folder: what to call this machine in that universe", "name"});
    command.json();
    command.parser.process(arguments);
    const QString action = command.argument(1), name = command.argument(2).trimmed().toLower();
    Universes universes;
    QString error;

    if (action == "add") {
        if (name.isEmpty())
            return fail("Name it: needful universe add hers ~/Sync/needful-hers");
        const bool known = !universes.dataPath(name).isEmpty();
        const QString path = universes.add(name, &error);
        if (path.isEmpty())
            return fail(error);
        const QString folder = command.argument(3);
        if (!folder.isEmpty()) {
            SyncFolder::Setup result;
            if (!SyncFolder::setup(path, folder, command.parser.value("name"), QDateTime::currentDateTime(), &result,
                                   &error, path)) {
                if (!known)
                    universes.remove(name, nullptr);
                return fail(error);
            }
            const QString where = shortPath(QDir::cleanPath(QFileInfo(folder).absoluteFilePath()));
            if (result.already)
                print(stdout, QString("%1 already syncs with %2.").arg(name, where));
            else if (result.joined)
                print(stdout, QString("Added %1, joined %2 (from %3). This machine is \"%4\" there.")
                                  .arg(name, where, result.others.join(", "), result.name));
            else
                print(stdout, QString("Added %1, syncing through %2. This machine is \"%3\" there.")
                                  .arg(name, where, result.name));
        } else {
            print(stdout, known ? QString("%1 is already here.").arg(name)
                                : QString("Added %1, a list kept only on this computer.").arg(name));
        }
        print(stdout, QString("Open it with: needful open --universe %1, or U in the window.").arg(name));
        return 0;
    }
    if (action == "remove") {
        const QString path = universes.dataPath(name);
        if (!universes.remove(name, &error))
            return fail(error);
        print(stdout, QString("Removed %1 from this computer. Its data stays in %2%3.")
                          .arg(name, shortPath(path),
                               SyncFolder::readPointer([&] {
                                   QFile file(path);
                                   return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
                               }(), nullptr) ? " and its sync folder" : ""));
        return 0;
    }
    if (!action.isEmpty())
        return fail("Use needful universe, universe add <name> [<folder>], or universe remove <name>.");

    QJsonArray array;
    int width = 4;
    for (const Universe &u : universes.list())
        width = std::max(width, int(u.name.size()));
    for (const Universe &u : universes.list()) {
        QString folder;
        if (QFile file(u.dataPath); file.open(QIODevice::ReadOnly)) {
            SyncFolder::Pointer pointer;
            if (SyncFolder::readPointer(file.readAll(), &pointer))
                folder = pointer.folder;
        }
        array.append(QJsonObject{{"name", u.name}, {"data", u.dataPath},
                                 {"folder", folder.isEmpty() ? QJsonValue::Null : QJsonValue(folder)}});
        if (!command.wantsJson())
            print(stdout, u.name.leftJustified(width + 2) +
                              (folder.isEmpty() ? shortPath(u.dataPath) : shortPath(folder) + "  (synced)"));
    }
    if (command.wantsJson())
        printJson(array);
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (isCliCommand(topic) && topic != "help")
        return runCli({arguments[0], topic, "--help"});
    print(stdout, "Usage: needful <command> [options]\n\n" + cliSummary());
    return 0;
}
}

QString cliSummary() {
    return "Window:\n"
           "  open                             The needful list, in a window\n\n"
           "Commands that need no display (needful help <command> for options):\n"
           "  list [--view V] [--context C]    Tasks, most needful first\n"
           "  add <title> [--every 1w] ...     Add a task\n"
           "  done <task> [--note N]           Mark a task done\n"
           "  snooze <task> <days>             Hide a task for some days\n"
           "  today <task>                     Pull a task into today, or put it back\n"
           "  log <text> [--follow-up]         Note something on a context\n"
           "  history [--context C]            What was done and noted, newest first\n"
           "  contexts [add <path>]            List or add contexts\n"
           "  skill [install]                  Print the skill for coding agents, or install it\n\n"
           "Other lists (needful help universe):\n"
           "  universe                         List universes: yours, and others you can open\n"
           "  universe add <name> [<folder>]   Add one, like someone else's, syncing through its folder\n"
           "  universe remove <name>           Stop listing it on this computer\n"
           "  --universe <name>                Any command, or open, on that universe\n\n"
           "Your other computers (needful help sync):\n"
           "  sync pair                        Show a code for another computer to join with\n"
           "  sync join <code> [--at host]     Pair with the computer showing the code, joining its list\n"
           "  sync unpair <machine>            Stop trusting a paired computer; its changes stay\n"
           "  sync install                     Keep the link up here as a user service\n"
           "  sync setup <folder>              Keep this machine in step through a synced folder\n"
           "  sync status                      When each machine's changes were last seen\n"
           "  sync rename <name>               Change how this machine is shown\n"
           "  sync forget <machine>            Stop showing a machine you no longer use\n"
           "  sync rsync install <host>        Swap changes with another computer over SSH, no sync tool\n\n"
           "Phone page (needful help serve):\n"
           "  serve [--port 8787]              Serve the page and its API until stopped\n"
           "  serve install [--tailscale]      Keep it running as a systemd user service\n"
           "  serve login                      Put it on your tailnet as needful.<tailnet>.ts.net\n"
           "  serve status                     Whether it's serving, and where\n"
           "  serve uninstall                  Stop it and remove the services\n\n"
           "Every command takes --data <path>; the usual file is\n  " + Backend::defaultDataPath();
}

bool isCliCommand(const QString &word) {
    return QStringList{"list", "add", "done", "snooze", "today", "log", "history", "contexts", "skill", "serve", "sync", "universe",
                       "help", "--help", "-h", "--version", "-v"}
        .contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    if (command == "--version" || command == "-v") {
        print(stdout, "needful " APP_VERSION);
        return 0;
    }
    return command == "list"       ? list(arguments)
           : command == "add"      ? add(arguments)
           : command == "done"     ? done(arguments)
           : command == "snooze"   ? snooze(arguments)
           : command == "today"    ? today(arguments)
           : command == "log"      ? logNote(arguments)
           : command == "history"  ? history(arguments)
           : command == "contexts" ? contexts(arguments)
           : command == "skill"    ? skill(arguments)
           : command == "serve"    ? runServe(arguments)
           : command == "sync"     ? sync(arguments)
           : command == "universe" ? universe(arguments)
                                   : help(arguments);
}
