#include "members.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

#include "devices.h"

namespace {
constexpr int Version = 1;
const QStringList Fields{"name", "in", "via"};

QDateTime timeOf(const QString &stamp) { return QDateTime::fromString(stamp.section('|', 0, 0), Qt::ISODateWithMs); }

bool validId(const QString &id) {
    if (id.size() != 64)
        return false;
    for (const QChar c : id)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}
}

QString Members::Member::fingerprint() const { return Devices::fingerprintOf(id); }

Members::Members(const QString &appDir) : m_dir(appDir) {}

QString Members::path() const { return m_dir + "/members.json"; }

bool Members::load(const QString &self, QString *error) {
    m_self = self;
    QFile file(path());
    m_doc = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    if (m_doc.value("needfulMembers").toInt() > Version) {
        if (error) *error = "Who your universes are shared with was saved by a newer Needful. Update Needful to use it.";
        m_doc = {};
        return false;
    }
    m_doc["needfulMembers"] = Version;
    return true;
}

bool Members::save(QString *error) const {
    QSaveFile file(path());
    if (!QDir().mkpath(m_dir) || !file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(m_doc).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error) *error = QString("Could not save %1: %2").arg(path(), file.errorString());
        return false;
    }
    return true;
}

QJsonValue Members::get(const QString &list, const QString &id, const QString &field) const {
    return m_doc.value("lists").toObject().value(list).toObject().value(id).toObject().value(field).toArray().at(0);
}

void Members::put(const QString &list, const QString &id, const QString &field, const QJsonValue &value,
                  const QDateTime &now) {
    QJsonObject lists = m_doc.value("lists").toObject();
    QJsonObject ids = lists.value(list).toObject();
    QJsonObject fields = ids.value(id).toObject();
    QDateTime at = now;
    const QString before = fields.value(field).toArray().at(1).toString();
    if (!before.isEmpty() && timeOf(before) >= at)
        at = timeOf(before).addMSecs(1);
    fields[field] = QJsonArray{value, at.toUTC().toString("yyyy-MM-ddTHH:mm:ss.zzzZ") + "|" + m_self};
    ids[id] = fields;
    lists[list] = ids;
    m_doc["lists"] = lists;
}

Members::Member Members::make(const QString &list, const QString &id) const {
    Member m;
    m.id = id;
    m.name = get(list, id, "name").toString();
    if (m.name.isEmpty())
        m.name = Devices::fingerprintOf(id);
    m.via = get(list, id, "via").toString();
    m.in = get(list, id, "in").toBool();
    return m;
}

QStringList Members::lists() const {
    QStringList found;
    const QJsonObject lists = m_doc.value("lists").toObject();
    for (auto it = lists.begin(); it != lists.end(); ++it)
        if (shared(it.key()))
            found.append(it.key());
    return found;
}

QList<Members::Member> Members::all(const QString &list) const {
    QList<Member> found;
    const QJsonObject ids = m_doc.value("lists").toObject().value(list).toObject();
    for (auto it = ids.begin(); it != ids.end(); ++it)
        found.append(make(list, it.key()));
    std::sort(found.begin(), found.end(),
              [](const Member &a, const Member &b) { return a.name.localeAwareCompare(b.name) < 0; });
    return found;
}

QList<Members::Member> Members::members(const QString &list) const {
    QList<Member> found = all(list);
    found.removeIf([](const Member &m) { return !m.in; });
    return found;
}

std::optional<Members::Member> Members::member(const QString &list, const QString &id) const {
    if (!m_doc.value("lists").toObject().value(list).toObject().contains(id))
        return std::nullopt;
    return make(list, id);
}

bool Members::allowed(const QString &list, const QString &id) const {
    return !list.isEmpty() && get(list, id, "in").toBool();
}

QStringList Members::trustedIds() const {
    QStringList ids;
    for (const QString &list : lists())
        for (const Member &m : members(list))
            if (m.id != m_self && !ids.contains(m.id))
                ids.append(m.id);
    return ids;
}

std::optional<Members::Member> Members::find(const QString &list, const QString &wanted) const {
    const QString w = wanted.trimmed();
    const QString compact = QString(w).remove('-').toLower();
    for (const Member &m : members(list))
        if (m.name.compare(w, Qt::CaseInsensitive) == 0 || (compact.size() >= 4 && m.id.startsWith(compact)))
            return m;
    return std::nullopt;
}

QString Members::root(const QString &list, const QString &id) const {
    QString at = id;
    for (int hops = 0; hops < 16; ++hops) {
        const QString via = get(list, at, "via").toString();
        if (via.isEmpty() || via == at)
            return at;
        at = via;
    }
    return at;
}

void Members::add(const QString &list, const QString &id, const QString &name, const QString &via,
                  const QDateTime &now) {
    if (!name.isEmpty() && get(list, id, "name").toString() != name)
        put(list, id, "name", name, now);
    if (get(list, id, "via").toString() != via || !member(list, id))
        put(list, id, "via", via, now);
    put(list, id, "in", true, now);
}

QStringList Members::remove(const QString &list, const QString &id, const QDateTime &now) {
    QStringList gone;
    for (const Member &m : members(list))
        if (m.id == id || root(list, m.id) == id)
            gone.append(m.id);
    for (const QString &g : gone)
        put(list, g, "in", false, now);
    return gone;
}

QJsonObject Members::entry(const QString &list) const { return m_doc.value("lists").toObject().value(list).toObject(); }

bool Members::merge(const QString &list, const QJsonObject &theirs) {
    if (list.isEmpty())
        return false;
    bool changed = false;
    auto newer = [](const QJsonValue &a, const QJsonValue &b) {
        return a.toArray().size() == 2 && a.toArray().at(1).toString() > b.toArray().at(1).toString();
    };
    QJsonObject lists = m_doc.value("lists").toObject();
    QJsonObject ids = lists.value(list).toObject();
    for (auto it = theirs.begin(); it != theirs.end(); ++it) {
        if (!validId(it.key()))
            continue;
        QJsonObject fields = ids.value(it.key()).toObject();
        const QJsonObject incoming = it.value().toObject();
        for (const QString &field : Fields)
            if (newer(incoming.value(field), fields.value(field))) {
                fields[field] = incoming.value(field);
                changed = true;
            }
        ids[it.key()] = fields;
    }
    if (!changed)
        return false;
    lists[list] = ids;
    m_doc["lists"] = lists;
    return true;
}

bool Members::mergeAll(const Members &other) {
    bool changed = false;
    const QJsonObject lists = other.m_doc.value("lists").toObject();
    for (auto it = lists.begin(); it != lists.end(); ++it)
        changed = merge(it.key(), it.value().toObject()) || changed;
    return changed;
}
