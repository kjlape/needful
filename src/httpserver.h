#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QTcpServer>
#include <QUrlQuery>
#include <functional>

// A deliberately small HTTP/1.1 server: one request per connection, then
// close. It serves Needful's phone page to a handful of devices, so it reads
// a request, answers it, and hangs up, with no keep-alive, chunking, or TLS
// (Tailscale's HTTPS proxy sits in front when it's on the tailnet). Built on
// QTcpServer, so Flatpak and macOS builds need nothing beyond Qt's base.

struct HttpRequest {
    QByteArray method;
    QString path;  // Decoded, without the query.
    QUrlQuery query;
    QHash<QByteArray, QByteArray> headers; // Names in lower case.
    QByteArray body;

    QByteArray header(const QByteArray &name) const { return headers.value(name.toLower()); }
};

struct HttpResponse {
    int status = 200;
    QByteArray contentType = "text/plain; charset=utf-8";
    QByteArray body;
    QList<QPair<QByteArray, QByteArray>> headers;

    static HttpResponse json(const QByteArray &body, int status = 200);
    static HttpResponse error(int status, const QString &message);
    QByteArray serialize() const;
};

// Reads one request from bytes as they arrive, which may split anywhere.
class HttpRequestParser {
public:
    enum State { Incomplete, Complete, Failed };
    static constexpr int MaxHeaderBytes = 16 * 1024;
    static constexpr int MaxBodyBytes = 64 * 1024;

    State feed(const QByteArray &bytes);
    State state() const { return m_state; }
    const HttpRequest &request() const { return m_request; }
    int errorStatus() const { return m_errorStatus; } // 400, 413, 431, 501, or 505.

private:
    State fail(int status);
    bool parseHead(const QByteArray &head);

    QByteArray m_buffer;
    HttpRequest m_request;
    State m_state = Incomplete;
    bool m_headDone = false;
    qsizetype m_bodyLength = 0;
    int m_errorStatus = 0;
};

class HttpServer : public QObject {
    Q_OBJECT
public:
    using Handler = std::function<HttpResponse(const HttpRequest &)>;
    static constexpr int ReadTimeoutMs = 30 * 1000;

    explicit HttpServer(Handler handler, QObject *parent = nullptr);
    bool listen(const QHostAddress &address, quint16 port);
    quint16 port() const { return m_server.serverPort(); }
    QString errorString() const { return m_server.errorString(); }

private:
    void accept();

    QTcpServer m_server;
    Handler m_handler;
};
