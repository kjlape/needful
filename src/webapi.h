#pragma once

#include <QJsonObject>
#include <QVariantMap>

#include "httpserver.h"

class Backend;
class Theme;

// The phone page's API and files. Requests are answered one at a time on the
// main thread, so the server keeps a Backend of its own and sets its view,
// context, and search for each request before reading the list; nothing else
// shares that Backend's view state. Changes go through the same Backend
// calls as the window, so they save, catch up with other writers, and undo
// the same way.
class WebApi {
public:
    // The theme is optional; without it the page uses Omarchy's default accent.
    WebApi(Backend *backend, Theme *theme = nullptr, const QString &files = ":/web");

    HttpResponse handle(const HttpRequest &request);

    // Only names a browser could have meant for this server: localhost, IP
    // addresses, and tailnet names. Other names are a DNS rebinding attempt.
    static bool allowedHost(const QByteArray &host);

private:
    HttpResponse route(const HttpRequest &request);
    HttpResponse file(const QString &path) const;
    HttpResponse state(const HttpRequest &request);
    HttpResponse taskAction(int id, const QString &action, const QVariantMap &body);
    HttpResponse addTask(const QVariantMap &body);
    // A note on a context, through Backend::addLog.
    HttpResponse addLog(const QVariantMap &body);
    QJsonObject taskJson(int id) const;

    Backend *m_backend;
    Theme *m_theme;
    QString m_files;
};
