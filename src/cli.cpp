// The command shape follows Hype's (MIT, David Heinemeier Hansson).
#include "cli.h"

#include <QCommandLineParser>
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
    QDir units(systemdUserDirectory());
    const QString systemctl = QStandardPaths::findExecutable("systemctl");
    if (step == "uninstall") {
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
    setup.rsync = QStandardPaths::findExecutable("rsync");
    if (setup.rsync.isEmpty() || QStandardPaths::findExecutable("ssh").isEmpty())
        return fail("This needs rsync and ssh, here and on " + host + ". On Arch: pacman -S rsync openssh");
    if (systemctl.isEmpty())
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
    const auto files = rsyncUnits(setup);
    for (auto it = files.begin(); it != files.end(); ++it) {
        QSaveFile file(units.filePath(it.key()));
        if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) < 0 || !file.commit())
            return fail("Could not write " + file.fileName());
        print(stdout, "Wrote " + file.fileName());
    }
    QProcess::execute(systemctl, {"--user", "daemon-reload"});
    if (QProcess::execute(systemctl, {"--user", "enable", "--now", RsyncTimer}) != 0)
        return fail("Could not start the timer: systemctl --user enable --now " + RsyncTimer);
    print(stdout, QString("Swapping changes with %1 every %2, while this computer is on. "
                          "Only one of the two computers needs this timer.\n"
                          "Stop with: needful sync rsync uninstall").arg(host, setup.every));
    return 0;
}

int sync(const QStringList &arguments) {
    Command command("sync", "Keep the same Needful on your computers, through a folder a sync tool keeps in step.");
    command.parser.addPositionalArgument("action", "setup <folder>, status, rename <name>, forget <machine>, or rsync install <host> / rsync uninstall",
                                         "<action>");
    command.parser.addOption({"name", "With setup: what to call this machine (its host name by default)", "name"});
    command.parser.addOption({"remote-folder", "With rsync install: the sync folder on the other computer "
                                               "(the same path as here, from its home folder)", "path"});
    command.parser.addOption({"every", "With rsync install: how often to swap changes: 30s, 5m, 1h (5m)", "interval", "5m"});
    command.json();
    command.parser.process(arguments);
    const QString action = command.argument(1), argument = command.parser.positionalArguments().mid(2).join(' ');
    const QString dataPath = command.dataPath().isEmpty() ? Backend::defaultDataPath() : command.dataPath();

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
    return fail("Use needful sync setup <folder>, sync status, sync rename <name>, sync forget <machine>, "
                "or sync rsync install <host>.");
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
