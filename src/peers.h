#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QObject>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>
#include <QVariantMap>
#include <memory>

#include "devices.h"
#include "sync.h"

struct NeedfulLink;

// Peer sync: your computers keep each other in step over the link, with
// nothing in between.
//
// It moves the same files folder sync does. Every list a paired computer has
// (your own and every universe) is a sync folder, machines/<id>.json, one
// file per machine with one writer each; computers that aren't in a folder a
// sync tool keeps get a folder of their own, beside the data file, that only
// Needful touches. When two paired computers are connected, each says which
// files it has and how new, and asks for the ones the other has newer. Each
// passes along everything it has, other machines' files included, so an
// always-on computer carries the laptop's changes to the desktop. Nothing
// about merging changes: the files merge as they always have.
//
// One Needful per computer holds the link (the window, the phone page's
// service, or `needful sync link`); the others, and the command line, ask it
// to pair through a local socket. What it sees is kept in link-state.json,
// beside the data file, for `sync status` and the window's footer to read.
class PeerLink : public QObject {
    Q_OBJECT
public:
    static constexpr int DefaultPort = 7787;
    struct Options {
        int port = DefaultPort;
        bool mdns = true;
        bool loopback = false; // Tests: listen on 127.0.0.1 only, with no public relay.
        bool ipc = true;
    };

    // usualDataPath is this computer's own data file; empty means the usual.
    explicit PeerLink(const QString &usualDataPath = {}, QObject *parent = nullptr);
    PeerLink(const QString &usualDataPath, const Options &options, QObject *parent = nullptr);
    ~PeerLink() override;

    // Holds the link for this computer. False with no error when another
    // Needful here already holds it.
    bool start(QString *error);
    // Holds the link for this computer (it may not be on the network yet).
    bool holding() const { return m_lock != nullptr; }
    bool running() const { return m_link != nullptr; }
    QString listIdOf(const QString &universe) const;
    QString appDir() const { return m_dir; }
    QString id() const { return m_devices.id(); }
    int port() const { return m_port; }
    QString name() const;

    // Pairing from this end: pairCode, then paired or failed.
    void pair();
    void cancelPair();
    // Joining from this end: confirm, then (after answer(true)) joined or failed.
    void join(const QString &code, const QString &at = {});
    void answer(bool accept);

    // Tests and `sync status`: whether a device is connected now, and how.
    QString linkTo(const QString &id) const { return m_up.value(id); }
    // Tests: dial a device at an address, as `sync reach` would.
    void dial(const QString &id, const QStringList &addresses = {});

    static QString socketName(const QString &appDir);
    static QString appDirFor(const QString &usualDataPath);
    // Codes like 7-violet-crumble-tango: a number, then three words.
    static QString newCode();

signals:
    void pairCode(const QString &code, const QString &name, const QString &fingerprint);
    void paired(const QString &name, const QString &fingerprint);
    void confirm(const QString &name, const QString &fingerprint);
    // name, fingerprint, already (the same list), tasks/contexts/history merged in, this (this machine's name).
    void joined(const QVariantMap &result);
    void failed(const QString &message);
    // A machine file went to a peer: list id, machine id, device id.
    void sent(const QString &list, const QString &machine, const QString &device);
    // A machine file came from a peer and was newer than this computer's copy.
    void received(const QString &list, const QString &machine, const QString &device);
    void changed();

private:
    struct List {
        QString universe, dataPath, folder, machine, id;
        bool mine = false;
        std::unique_ptr<SyncFolder> sync;
    };
    struct Staging {
        QString peer, list, folder;
        QSet<QString> waiting;
    };

    void onEvents();
    void handle(const QJsonObject &event);
    void handleMessage(const QString &peer, const QJsonObject &message);
    void send(const QString &peer, const QJsonObject &message);
    void command(const QJsonObject &command);
    void applyTrust();
    void dialAll();
    void sendHello(const QString &peer);
    void sendOffer(const QString &peer);
    void offerAll();
    void handleOffer(const QString &peer, const QJsonArray &lists);
    void storeFile(const QString &peer, const QJsonObject &message);
    void finishJoin();
    void loadLists();
    bool ensureSynced(bool pairing = false);
    List *listById(const QString &id);
    List *match(const QJsonObject &offered);
    List *mine();
    QString createUniverse(const QJsonObject &offered);
    QJsonObject info();
    void saveState();
    void loadState();
    void reloadDevices();
    void saveDevices();
    void nameThisDevice();
    void serveClient(QLocalSocket *socket);
    QString failure(const QString &reason) const;
    // relay, when given, is used instead of the saved one, as a joiner
    // with none does to reach a computer waiting at the relay in its code.
    bool startLink(QString *error, const QString &relay = {});
    bool returnRelay(); // True when the link restarted.
    QString savedRelay() const;
    QString fullCode(const QString &words) const;
    void stopLink();

    QString m_usual, m_dir;
    Options m_options;
    Devices m_devices;
    std::unique_ptr<QLockFile> m_lock;
    NeedfulLink *m_link = nullptr;
    std::unique_ptr<QSocketNotifier> m_notifier;
    int m_port = 0;
    QString m_relay;

    std::vector<std::unique_ptr<List>> m_lists;
    QHash<QString, QString> m_up;           // device → local, direct, relay
    QHash<QString, QDateTime> m_seen;       // device → last connected
    QHash<QString, QStringList> m_addrs;    // device → addresses it was last reached at
    QHash<QString, QHash<QString, SyncFolder::File>> m_offered; // list → what was last offered
    QHash<QString, QString> m_aliases; // another computer's id for a list → its id here

    QString m_pairCode;
    QTimer m_pairExpiry;
    QString m_joinCode, m_joinFrom, m_joinRelay;
    QJsonObject m_joinInfo;
    std::unique_ptr<Staging> m_staging;

    QFileSystemWatcher m_watcher;
    QTimer m_offerDelay, m_stateDelay, m_redial;
    QByteArray m_devicesWritten;
    std::unique_ptr<QLocalServer> m_server;
};

// What this computer knows of its paired devices, read from the files, so
// the command line and the window can show it whichever Needful holds the
// link.
struct PeerStatus {
    struct Entry {
        Devices::Device device;
        QString link;       // local, direct, relay, or empty for not connected
        QDateTime seen;     // Last connected; invalid for never.
        QDateTime changed;  // Its last change in the list asked about.
    };
    bool running = false;   // A Needful here holds the link.
    QString name, fingerprint, relay;
    QList<Entry> devices;
    // mine is the list to take last changes from; may be null.
    static PeerStatus read(const QString &appDir, const SyncFolder *list);
};

// Asks the Needful holding the link here to pair or join, for the command
// line and a window that doesn't hold it. Same signals as PeerLink.
class LinkClient : public QObject {
    Q_OBJECT
public:
    explicit LinkClient(const QString &appDir, QObject *parent = nullptr);
    bool connectToOwner();
    void pair();
    void join(const QString &code, const QString &at);
    void answer(bool accept);
    void cancel();

signals:
    void pairCode(const QString &code, const QString &name, const QString &fingerprint);
    void paired(const QString &name, const QString &fingerprint);
    void confirm(const QString &name, const QString &fingerprint);
    void joined(const QVariantMap &result);
    void failed(const QString &message);

private:
    void write(const QJsonObject &message);
    QString m_dir;
    QLocalSocket m_socket;
    bool m_done = false;
};
