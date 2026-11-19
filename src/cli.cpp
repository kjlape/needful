// The command shape follows Hype's (MIT, David Heinemeier Hansson).
#include "cli.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <cstdio>

#include "backend.h"
#include "serve.h"

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
    }
    void json() { parser.addOption({"json", "Print the result as JSON"}); }
    void context() { parser.addOption({"context", "A context by name or path, like \"Automotive/White Honda CR-V\"", "context"}); }
    bool wantsJson() const { return parser.isSet("json"); }
    QString argument(int index) const { return parser.positionalArguments().value(index); }
    QString dataPath() const { return parser.value("data"); }
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
    Command command("history", "Show what was done, newest first.");
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
        array.append(taskJson(c));
        if (!command.wantsJson())
            print(stdout, QString("%1  %2%3%4").arg(c["date"].toString(), c["title"].toString(),
                                                   c["contextPath"].toString().isEmpty() ? "" : "  [" + c["contextPath"].toString() + "]",
                                                   c["note"].toString().isEmpty() ? "" : "\n      " + c["note"].toString()));
    }
    if (command.wantsJson())
        printJson(array);
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
           "  history [--context C]            What was done, newest first\n"
           "  contexts [add <path>]            List or add contexts\n"
           "  skill [install]                  Print the skill for coding agents, or install it\n\n"
           "Phone page (needful help serve):\n"
           "  serve [--port 8787]              Serve the page and its API until stopped\n"
           "  serve install [--tailscale]      Keep it running as a systemd user service\n"
           "  serve login                      Put it on your tailnet as needful.<tailnet>.ts.net\n"
           "  serve status                     Whether it's serving, and where\n"
           "  serve uninstall                  Stop it and remove the services\n\n"
           "Every command takes --data <path>; the usual file is\n  " + Backend::defaultDataPath();
}

bool isCliCommand(const QString &word) {
    return QStringList{"list", "add", "done", "snooze", "today", "history", "contexts", "skill", "serve",
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
           : command == "history"  ? history(arguments)
           : command == "contexts" ? contexts(arguments)
           : command == "skill"    ? skill(arguments)
           : command == "serve"    ? runServe(arguments)
                                   : help(arguments);
}
