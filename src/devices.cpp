#include "devices.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>
#include <algorithm>

#include "link.h"

namespace {
constexpr int Version = 1;

QJsonArray entry(const QJsonValue &value, const QString &stamp) { return QJsonArray{value, stamp}; }

QDateTime timeOf(const QString &stamp) {
    return QDateTime::fromString(stamp.section('|', 0, 0), Qt::ISODateWithMs);
}
}

QString Devices::Device::fingerprint() const { return Devices::fingerprintOf(id); }

QString Devices::fingerprintOf(const QString &id) {
    const QString hex = id.left(12).toUpper();
    return hex.size() < 12 ? hex : hex.left(4) + "-" + hex.mid(4, 4) + "-" + hex.mid(8, 4);
}

Devices::Devices(const QString &appDir) : m_dir(appDir) {}

QString Devices::path() const { return m_dir + "/devices.json"; }

bool Devices::load(QString *error, bool create) {
    const QString keyPath = m_dir + "/identity.key";
    QFile key(keyPath);
    if (key.open(QIODevice::ReadOnly))
        m_secret = QString::fromLatin1(key.readAll().trimmed());
    key.close();
    if (m_secret.size() != 64 && !create) {
        m_secret.clear();
        m_id.clear();
    } else if (m_secret.size() != 64) {
        QByteArray bytes(32, 0);
        QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), 8);
        m_secret = QString::fromLatin1(bytes.toHex());
        QSaveFile out(keyPath);
        if (!QDir().mkpath(m_dir) || !out.open(QIODevice::WriteOnly) || out.write(m_secret.toLatin1() + "\n") < 0 ||
            !out.commit()) {
            if (error) *error = QString("Could not save %1: %2").arg(keyPath, out.errorString());
            return false;
        }
        QFile::setPermissions(keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    if (!m_secret.isEmpty()) {
        char *pub = needful_link_public_key(m_secret.toLatin1().constData());
        m_id = pub ? QString::fromLatin1(pub) : QString();
        needful_link_free_string(pub);
    }

    QFile file(path());
    m_doc = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    if (m_doc.value("needfulDevices").toInt() > Version) {
        if (error) *error = "The paired devices were saved by a newer Needful. Update Needful to use them.";
        return false;
    }
    m_doc["needfulDevices"] = Version;
    return !m_id.isEmpty() || !create;
}

bool Devices::save(QString *error) const {
    QSaveFile file(path());
    if (!QDir().mkpath(m_dir) || !file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(m_doc).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error) *error = QString("Could not save %1: %2").arg(path(), file.errorString());
        return false;
    }
    return true;
}

QString Devices::stamp(const QDateTime &now) const {
    return now.toUTC().toString("yyyy-MM-ddTHH:mm:ss.zzzZ") + "|" + m_id;
}

QJsonValue Devices::get(const QString &id, const QString &field) const {
    return m_doc.value("devices").toObject().value(id).toObject().value(field).toArray().at(0);
}

void Devices::put(const QString &id, const QString &field, const QJsonValue &value, const QDateTime &now) {
    QJsonObject all = m_doc.value("devices").toObject();
    QJsonObject fields = all.value(id).toObject();
    // Always after what's there, even with a clock that went back, so the
    // newest change made here is the one that sticks.
    QDateTime at = now;
    const QString before = fields.value(field).toArray().at(1).toString();
    if (!before.isEmpty() && timeOf(before) >= at)
        at = timeOf(before).addMSecs(1);
    fields[field] = entry(value, stamp(at));
    all[id] = fields;
    m_doc["devices"] = all;
}

Devices::Device Devices::device(const QString &id) const {
    Device d;
    d.id = id;
    d.name = get(id, "name").toString();
    if (d.name.isEmpty())
        d.name = fingerprintOf(id);
    d.kind = get(id, "kind").toString("computer");
    d.paired = get(id, "paired").toBool();
    const QJsonObject fields = m_doc.value("devices").toObject().value(id).toObject();
    d.pairedAt = timeOf(fields.value("paired").toArray().at(1).toString());
    for (const auto &host : get(id, "reach").toArray())
        d.reach.append(host.toString());
    return d;
}

QList<Devices::Device> Devices::devices() const {
    QList<Device> list;
    const QJsonObject all = m_doc.value("devices").toObject();
    for (auto it = all.begin(); it != all.end(); ++it)
        if (it.key() != m_id)
            list.append(device(it.key()));
    std::sort(list.begin(), list.end(),
              [](const Device &a, const Device &b) { return a.name.localeAwareCompare(b.name) < 0; });
    return list;
}

QList<Devices::Device> Devices::paired() const {
    QList<Device> list = devices();
    list.removeIf([](const Device &d) { return !d.paired; });
    return list;
}

QStringList Devices::trustedIds() const {
    QStringList ids;
    for (const Device &d : paired())
        ids.append(d.id);
    return ids;
}

std::optional<Devices::Device> Devices::find(const QString &wanted) const {
    const QString w = wanted.trimmed();
    const QString compact = QString(w).remove('-').toLower();
    for (const Device &d : devices())
        if (d.name.compare(w, Qt::CaseInsensitive) == 0 ||
            (compact.size() >= 4 && d.id.startsWith(compact)))
            return d;
    return std::nullopt;
}

QString Devices::name() const { return get(m_id, "name").toString(); }

void Devices::pair(const QString &id, const QString &name, const QString &kind, const QDateTime &now) {
    if (!name.isEmpty() && get(id, "name").toString() != name)
        put(id, "name", name, now);
    put(id, "kind", kind.isEmpty() ? QString("computer") : kind, now);
    put(id, "paired", true, now);
}

void Devices::unpair(const QString &id, const QDateTime &now) { put(id, "paired", false, now); }

void Devices::setName(const QString &id, const QString &name, const QDateTime &now) {
    if (get(id, "name").toString() != name)
        put(id, "name", name, now);
}

void Devices::setReach(const QString &id, const QStringList &hosts, const QDateTime &now) {
    put(id, "reach", QJsonArray::fromStringList(hosts), now);
}

QString Devices::relay() const { return m_doc.value("relay").toArray().at(0).toString(); }

void Devices::setRelay(const QString &url, const QDateTime &now) {
    QDateTime at = now;
    const QString before = m_doc.value("relay").toArray().at(1).toString();
    if (!before.isEmpty() && timeOf(before) >= at)
        at = timeOf(before).addMSecs(1);
    m_doc["relay"] = entry(url.isEmpty() ? QJsonValue() : QJsonValue(url), stamp(at));
}

bool Devices::merge(const QJsonObject &theirs) {
    bool changed = false;
    auto newer = [](const QJsonValue &a, const QJsonValue &b) {
        return a.toArray().size() == 2 && a.toArray().at(1).toString() > b.toArray().at(1).toString();
    };
    static const QStringList Fields{"name", "kind", "paired", "reach"};
    QJsonObject all = m_doc.value("devices").toObject();
    const QJsonObject other = theirs.value("devices").toObject();
    for (auto it = other.begin(); it != other.end(); ++it) {
        if (it.key().size() != 64)
            continue;
        QJsonObject fields = all.value(it.key()).toObject();
        const QJsonObject incoming = it.value().toObject();
        for (const QString &field : Fields)
            if (newer(incoming.value(field), fields.value(field))) {
                fields[field] = incoming.value(field);
                changed = true;
            }
        all[it.key()] = fields;
    }
    m_doc["devices"] = all;
    if (newer(theirs.value("relay"), m_doc.value("relay"))) {
        m_doc["relay"] = theirs.value("relay");
        changed = true;
    }
    return changed;
}
