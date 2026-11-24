#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

// Who a shared universe is shared with, beyond your own computers.
//
// Your own computers are in devices.json and get every list. People you
// share one universe with are kept here instead, in members.json beside it,
// by list id: they get that list and nothing else, they never learn about
// your other computers' trust, and an older Needful, which doesn't read this
// file, never trusts them.
//
// Every computer that holds a shared list is in its entry here, yours and
// theirs, so any of them can sync it with any other. Each list's entry
// travels to everyone in that list, and only to them; computers keep, field
// by field, the latest value anyone gave, as devices.json does, so a removal
// reaches the rest of the universe the way changes do.
//
// via names the computer a member came along with: a member's own computers
// get the list through it, the way they get any of its lists. Removing a
// member removes the computers that came with it.
class Members {
public:
    struct Member {
        QString id;    // The device's public key, as hex.
        QString name;
        QString via;   // The member it came with, or empty for one shared with directly.
        bool in = false;
        QString fingerprint() const;
    };

    explicit Members(const QString &appDir);

    QString path() const;
    // self is this install's id, for stamps.
    bool load(const QString &self, QString *error = nullptr);
    bool save(QString *error = nullptr) const;

    // Lists shared with anyone at all.
    QStringList lists() const;
    bool shared(const QString &list) const { return !members(list).isEmpty(); }
    // Every computer ever in the list, by name; with in false for those removed.
    QList<Member> all(const QString &list) const;
    QList<Member> members(const QString &list) const; // Those still in.
    std::optional<Member> member(const QString &list, const QString &id) const;
    bool allowed(const QString &list, const QString &id) const;
    // Every computer in any list, but this one: they may connect.
    QStringList trustedIds() const;
    // By name, or by the start of its fingerprint.
    std::optional<Member> find(const QString &list, const QString &nameOrFingerprint) const;
    // The member a computer came with, following via to the one shared with directly.
    QString root(const QString &list, const QString &id) const;

    void add(const QString &list, const QString &id, const QString &name, const QString &via, const QDateTime &now);
    // Removes a computer and every one that came with it; returns their ids.
    QStringList remove(const QString &list, const QString &id, const QDateTime &now);

    // One list's entry, as sent to the others in it.
    QJsonObject entry(const QString &list) const;
    // What another computer sent for a list; true when anything here changed.
    bool merge(const QString &list, const QJsonObject &theirs);
    // Every list's entry from a copy on disk.
    bool mergeAll(const Members &other);

private:
    void put(const QString &list, const QString &id, const QString &field, const QJsonValue &value,
             const QDateTime &now);
    QJsonValue get(const QString &list, const QString &id, const QString &field) const;
    Member make(const QString &list, const QString &id) const;

    QString m_dir, m_self;
    QJsonObject m_doc; // {lists: {list: {id: {field: [value, stamp]}}}}
};
