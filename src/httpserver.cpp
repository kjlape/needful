#include "httpserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace {
QByteArray reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 413: return "Content Too Large";
    case 415: return "Unsupported Media Type";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 505: return "HTTP Version Not Supported";
    }
    return "Status";
}
}

HttpResponse HttpResponse::json(const QByteArray &body, int status) {
    HttpResponse response;
    response.status = status;
    response.contentType = "application/json";
    response.body = body;
    return response;
}

HttpResponse HttpResponse::error(int status, const QString &message) {
    return json(QJsonDocument(QJsonObject{{"error", message}}).toJson(QJsonDocument::Compact), status);
}

QByteArray HttpResponse::serialize() const {
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " " + reason(status) + "\r\n";
    if (status != 204)
        out += "Content-Type: " + contentType + "\r\n";
    out += "Content-Length: " + QByteArray::number(status == 204 ? 0 : body.size()) + "\r\n";
    out += "Connection: close\r\n";
    for (const auto &header : headers)
        out += header.first + ": " + header.second + "\r\n";
    out += "\r\n";
    if (status != 204)
        out += body;
    return out;
}

HttpRequestParser::State HttpRequestParser::fail(int status) {
    m_errorStatus = status;
    m_state = Failed;
    m_buffer.clear();
    return m_state;
}

HttpRequestParser::State HttpRequestParser::feed(const QByteArray &bytes) {
    if (m_state != Incomplete)
        return m_state;
    m_buffer += bytes;
    if (!m_headDone) {
        const qsizetype end = m_buffer.indexOf("\r\n\r\n");
        if (end < 0)
            return m_buffer.size() > MaxHeaderBytes ? fail(431) : Incomplete;
        if (end > MaxHeaderBytes)
            return fail(431);
        if (!parseHead(m_buffer.left(end)))
            return m_state;
        m_buffer.remove(0, end + 4);
        m_headDone = true;
    }
    if (m_buffer.size() < m_bodyLength)
        return Incomplete;
    // Anything after the body is a pipelined request, which a server that
    // closes after one answer ignores.
    m_request.body = m_buffer.left(m_bodyLength);
    m_buffer.clear();
    m_state = Complete;
    return m_state;
}

bool HttpRequestParser::parseHead(const QByteArray &head) {
    const QList<QByteArray> lines = head.split('\n');
    const QList<QByteArray> parts = lines.first().trimmed().split(' ');
    static const QRegularExpression token("^[A-Z]{1,16}$");
    if (parts.size() != 3 || !token.match(QString::fromLatin1(parts[0])).hasMatch() || !parts[1].startsWith('/')) {
        fail(400);
        return false;
    }
    if (parts[2] != "HTTP/1.1" && parts[2] != "HTTP/1.0") {
        fail(parts[2].startsWith("HTTP/") ? 505 : 400);
        return false;
    }
    m_request.method = parts[0];
    const qsizetype question = parts[1].indexOf('?');
    const QByteArray rawPath = question < 0 ? parts[1] : parts[1].left(question);
    m_request.path = QUrl::fromPercentEncoding(rawPath);
    if (question >= 0)
        m_request.query = QUrlQuery(QString::fromUtf8(parts[1].mid(question + 1)));

    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].endsWith('\r') ? lines[i].chopped(1) : lines[i];
        const qsizetype colon = line.indexOf(':');
        // Folded continuation lines and names with spaces are refused, as
        // RFC 9112 asks, rather than guessed at.
        if (colon <= 0 || line.startsWith(' ') || line.startsWith('\t') || line.left(colon).contains(' ')) {
            fail(400);
            return false;
        }
        const QByteArray name = line.left(colon).toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name == "content-length" && m_request.headers.contains(name) && m_request.headers.value(name) != value) {
            fail(400);
            return false;
        }
        m_request.headers.insert(name, value);
    }

    if (m_request.headers.contains("transfer-encoding")) {
        fail(501); // No chunked bodies; nothing Needful sends needs them.
        return false;
    }
    if (m_request.headers.contains("content-length")) {
        const QByteArray value = m_request.headers.value("content-length");
        bool ok = false;
        const qlonglong length = value.toLongLong(&ok);
        static const QRegularExpression digits("^[0-9]{1,12}$");
        if (!ok || !digits.match(QString::fromLatin1(value)).hasMatch()) {
            fail(400);
            return false;
        }
        if (length > MaxBodyBytes) {
            fail(413);
            return false;
        }
        m_bodyLength = length;
    }
    return true;
}

HttpServer::HttpServer(Handler handler, QObject *parent) : QObject(parent), m_handler(std::move(handler)) {
    connect(&m_server, &QTcpServer::newConnection, this, &HttpServer::accept);
}

bool HttpServer::listen(const QHostAddress &address, quint16 port) { return m_server.listen(address, port); }

void HttpServer::accept() {
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        // The parser lives as long as the socket's handlers that hold it.
        auto parser = std::make_shared<HttpRequestParser>();
        auto *timeout = new QTimer(socket);
        timeout->setSingleShot(true);
        timeout->setInterval(ReadTimeoutMs);

        auto answer = [socket, timeout](const HttpResponse &response) {
            timeout->stop();
            // No more reading: the answer is final, and then the line closes.
            QObject::disconnect(socket, &QTcpSocket::readyRead, nullptr, nullptr);
            socket->write(response.serialize());
            socket->disconnectFromHost();
        };
        connect(timeout, &QTimer::timeout, socket, [answer] { answer(HttpResponse::error(408, "Request timed out")); });
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, parser, answer] {
            switch (parser->feed(socket->readAll())) {
            case HttpRequestParser::Incomplete:
                return;
            case HttpRequestParser::Failed:
                answer(HttpResponse::error(parser->errorStatus(), "Bad request"));
                return;
            case HttpRequestParser::Complete:
                answer(m_handler(parser->request()));
                return;
            }
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        timeout->start();
    }
}
