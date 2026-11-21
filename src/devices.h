#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

// One identity for this install, and one list of the devices it trusts.
// Pairing is Needful-wide, not a sync feature: peer sync uses it for your
// computers, and phone link adds phones to the same list (kind "phone") and
// uses the same link to reach them, so there is one place to see what's
// trusted and one step to revoke it.
//
// The identity is a secret key kept in identity.key beside the data file;
// its public half is this install's id on the link, and the first 48 bits
// of it, as 4F2A-91C0-7B3E, are the fingerprint people compare.
//
// The list lives in devices.json beside it. Paired computers swap their
// lists and keep, field by field, the latest value any of them gave, so
// pairing a computer with any one of yours trusts it on all of them, and
// unpairing it on one unpairs it everywhere once the word gets around. The
// relay and each device's reach addresses travel the same way.
class Devices {
public:
    struct Device {
        QString id;          // The device's public key, as hex.
        QString name;
        QString kind;        // computer, or phone
        bool paired = false;
        QDateTime pairedAt;  // When it was last paired, or unpaired.
        QStringList reach;   // Addresses to look for it at: host or host:port.
        QString fingerprint() const;
    };

    // appDir is the folder the usual data file is in.
    explicit Devices(const QString &appDir);

    QString appDir() const { return m_dir; }
    QString path() const;

    // Makes the identity on first use, unless create is false. False only
    // when it can't be saved, or the list is from a newer Needful.
    bool load(QString *error = nullptr, bool create = true);
    bool save(QString *error = nullptr) const;

    QString secret() const { return m_secret; }
    QString id() const { return m_id; }
    QString fingerprint() const { return fingerprintOf(m_id); }
    static QString fingerprintOf(const QString &id);

    QList<Device> devices() const;           // Every device ever paired but this one, by name.
    QList<Device> paired() const;            // Those still trusted.
    QStringList trustedIds() const;
    // By name, or by the start of its fingerprint.
    std::optional<Device> find(const QString &nameOrFingerprint) const;
    Device device(const QString &id) const;
    QString name() const;                    // This install's name, from its own record.

    void pair(const QString &id, const QString &name, const QString &kind, const QDateTime &now);
    void unpair(const QString &id, const QDateTime &now);
    void setName(const QString &id, const QString &name, const QDateTime &now);
    void setReach(const QString &id, const QStringList &hosts, const QDateTime &now);
    // The relay paired computers meet at: the public one until another is
    // set, or empty once it's turned off.
    static const QString PublicRelay;
    QString relay() const;
    void setRelay(const QString &url, const QDateTime &now);

    // What another device sent; true when anything here changed.
    QJsonObject toJson() const { return m_doc; }
    bool merge(const QJsonObject &theirs);

private:
    void put(const QString &id, const QString &field, const QJsonValue &value, const QDateTime &now);
    QString stamp(const QDateTime &now) const;
    QJsonValue get(const QString &id, const QString &field) const;

    QString m_dir, m_secret, m_id;
    QJsonObject m_doc; // {devices: {id: {field: [value, stamp]}}, relay: [url, stamp]}
};
