#include "webapi.h"

#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include "backend.h"
#include "theme.h"

namespace {
// The page loads nothing but its own files, and runs no inline script.
const QByteArray ContentSecurityPolicy = "default-src 'self'; img-src 'self' data:; style-src 'self'; "
                                         "script-src 'self'; connect-src 'self'; manifest-src 'self'; "
                                         "worker-src 'self'; base-uri 'none'; form-action 'none'; "
                                         "frame-ancestors 'none'";

QByteArray compact(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }

QByteArray contentType(const QString &path) {
    const QString suffix = QFileInfo(path).suffix();
    if (suffix == "html") return "text/html; charset=utf-8";
    if (suffix == "js") return "text/javascript; charset=utf-8";
    if (suffix == "css") return "text/css; charset=utf-8";
    if (suffix == "webmanifest") return "application/manifest+json";
    if (suffix == "svg") return "image/svg+xml";
    if (suffix == "png") return "image/png";
    return "application/octet-stream";
}
}

WebApi::WebApi(Backend *backend, Theme *theme, const QString &files)
    : m_backend(backend), m_theme(theme), m_files(files) {}

bool WebApi::allowedHost(const QByteArray &hostHeader) {
    if (hostHeader.isEmpty())
        return true; // HTTP/1.0 clients may leave it out; browsers never do.
    QString host = QString::fromLatin1(hostHeader).toLower();
    if (host.startsWith('[')) // [::1]:8787
        host = host.mid(1, host.indexOf(']') - 1);
    else if (host.count(':') == 1)
        host = host.section(':', 0, 0);
    if (host == "localhost" || QHostAddress(host).protocol() != QAbstractSocket::UnknownNetworkLayerProtocol)
        return true;
    return host.endsWith(".ts.net");
}

HttpResponse WebApi::handle(const HttpRequest &request) {
    HttpResponse response = allowedHost(request.header("host"))
                                ? route(request)
                                : HttpResponse::error(403, "This server only answers to localhost and its tailnet name.");
    response.headers.append(qMakePair(QByteArray("Content-Security-Policy"), ContentSecurityPolicy));
    response.headers.append(qMakePair(QByteArray("X-Content-Type-Options"), QByteArray("nosniff")));
    response.headers.append(qMakePair(QByteArray("Referrer-Policy"), QByteArray("no-referrer")));
    response.headers.append(qMakePair(QByteArray("Cache-Control"), QByteArray("no-cache")));
    return response;
}

HttpResponse WebApi::route(const HttpRequest &request) {
    const QString &path = request.path;
    if (!path.startsWith("/api/")) {
        if (request.method != "GET" && request.method != "HEAD")
            return HttpResponse::error(405, "Method not allowed");
        HttpResponse response = file(path == "/" ? "/index.html" : path);
        if (request.method == "HEAD")
            response.body.clear();
        return response;
    }

    if (request.method == "GET") {
        if (path == "/api/state")
            return state(request);
        static const QRegularExpression taskPath("^/api/task/(\\d+)$");
        const auto match = taskPath.match(path);
        if (match.hasMatch()) {
            const QJsonObject task = taskJson(match.captured(1).toInt());
            return task.isEmpty() ? HttpResponse::error(404, "No such task") : HttpResponse::json(compact(task));
        }
        return HttpResponse::error(404, "Not found");
    }
    if (request.method != "POST")
        return HttpResponse::error(405, "Method not allowed");

    // Changes only come as JSON, which a page on another site can't send
    // here without the browser asking first, and this server never says yes.
    if (!request.header("content-type").startsWith("application/json"))
        return HttpResponse::error(415, "Send JSON");
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(request.body.isEmpty() ? "{}" : request.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return HttpResponse::error(400, "The body isn't a JSON object");
    const QVariantMap body = document.object().toVariantMap();

    if (path == "/api/tasks")
        return addTask(body);
    if (path == "/api/log")
        return addLog(body);
    static const QRegularExpression actionPath("^/api/task/(\\d+)/(done|skip|today|snooze|unsnooze)$");
    const auto match = actionPath.match(path);
    if (match.hasMatch())
        return taskAction(match.captured(1).toInt(), match.captured(2), body);
    return HttpResponse::error(404, "Not found");
}

HttpResponse WebApi::file(const QString &path) const {
    // Only plain names directly in the folder; nothing climbs out of it.
    static const QRegularExpression name("^/[A-Za-z0-9_-]+(\\.[A-Za-z0-9]+)+$");
    if (!name.match(path).hasMatch())
        return HttpResponse::error(404, "Not found");
    QFile file(m_files + path);
    if (!file.open(QIODevice::ReadOnly))
        return HttpResponse::error(404, "Not found");
    HttpResponse response;
    response.contentType = contentType(path);
    response.body = file.readAll();
    return response;
}

HttpResponse WebApi::state(const HttpRequest &request) {
    const QString view = request.query.queryItemValue("view");
    m_backend->setView(QStringList{"today", "needful", "upcoming", "history"}.contains(view) ? view : "needful");
    m_backend->setContextFilter(request.query.queryItemValue("context").toInt());
    m_backend->setSearch(request.query.queryItemValue("search", QUrl::FullyDecoded));
    QJsonObject state{
        {"view", m_backend->view()},
        {"context", m_backend->contextFilter()},
        {"items", QJsonArray::fromVariantList(m_backend->items())},
        {"doneToday", QJsonArray::fromVariantList(m_backend->doneToday())},
        {"contexts", QJsonArray::fromVariantList(m_backend->contexts())},
        {"counts", QJsonObject::fromVariantMap(m_backend->counts())},
        {"today", m_backend->today().toString(Qt::ISODate)},
        {"todayText", m_backend->todayText()},
        {"accent", m_theme ? m_theme->accent() : QString("#FFD60A")},
        {"accentForeground", m_theme ? m_theme->accentForeground() : QString("black")},
        {"error", m_backend->error()},
    };
    return HttpResponse::json(compact(state));
}

QJsonObject WebApi::taskJson(int id) const {
    return QJsonObject::fromVariantMap(m_backend->task(id));
}

HttpResponse WebApi::taskAction(int id, const QString &action, const QVariantMap &body) {
    if (!m_backend->store().task(id))
        return HttpResponse::error(404, "No such task");
    if (action == "done" || action == "skip") {
        const QString next = body.value("next").toString();
        if (!next.isEmpty() && !QDate::fromString(next, Qt::ISODate).isValid())
            return HttpResponse::error(400, "next takes a date like 2026-05-01");
        if (action == "done")
            m_backend->complete(id, body.value("note").toString(), next);
        else
            m_backend->skip(id, body.value("reason").toString(), next);
    } else if (action == "today") {
        m_backend->toggleToday(id);
    } else if (action == "snooze") {
        const int days = body.value("days").toInt();
        if (days < 1 || days > 3660)
            return HttpResponse::error(400, "days takes a number from 1 to 3660");
        m_backend->snooze(id, days);
    } else {
        m_backend->unsnooze(id);
    }
    if (!m_backend->error().isEmpty())
        return HttpResponse::error(500, m_backend->error());
    return HttpResponse::json(compact({{"ok", true}, {"task", taskJson(id)}}));
}

HttpResponse WebApi::addTask(const QVariantMap &body) {
    const QString title = body.value("title").toString().trimmed();
    if (title.isEmpty())
        return HttpResponse::error(400, "Say what needs doing");
    QVariantMap fields{{"title", title},
                       {"notes", body.value("notes").toString()},
                       {"contexts", body.value("contexts").toList()},
                       {"priority", std::clamp(body.value("priority", 2).toInt(), 1, 4)},
                       {"scheduleKind", "once"}};
    const QString due = body.value("due").toString();
    if (!due.isEmpty()) {
        if (!QDate::fromString(due, Qt::ISODate).isValid())
            return HttpResponse::error(400, "due takes a date like 2026-05-01");
        fields["due"] = due;
    }
    // A quick task is one-time unless it says how often.
    const int every = body.value("every").toInt();
    const QString unit = body.value("unit").toString();
    if (every > 0 && QStringList{"day", "week", "month", "year"}.contains(unit)) {
        fields["scheduleKind"] = "every";
        fields["every"] = every;
        fields["unit"] = unit;
    }
    const int id = m_backend->saveTask(fields);
    if (!id)
        return HttpResponse::error(500, m_backend->error().isEmpty() ? "Could not add the task" : m_backend->error());
    return HttpResponse::json(compact({{"ok", true}, {"id", id}}), 201);
}

// A note on a context, optionally followed up as a one-time task.
HttpResponse WebApi::addLog(const QVariantMap &body) {
    if (body.value("text").toString().trimmed().isEmpty())
        return HttpResponse::error(400, "Write the note first");
    const int id = m_backend->addLog(body);
    if (!id)
        return HttpResponse::error(500, m_backend->error().isEmpty() ? "Could not save the note" : m_backend->error());
    return HttpResponse::json(compact({{"ok", true}, {"id", id}}), 201);
}
