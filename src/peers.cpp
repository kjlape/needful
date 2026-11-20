#include "peers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <QUuid>

#include "backend.h"
#include "link.h"
#include "store.h"
#include "universes.h"

namespace {
const QStringList Words = QString(
    "acid acorn actor adapt agent alarm album alert alien amber angle ankle apple apron arena armor arrow aspen "
    "atlas attic autumn award bacon badge bagel baker bamboo banjo barley basil basin beach beacon beaver bench "
    "berry bison blade blank blaze bloom board bonus boost brave bread brick bridge brook brush bubble bucket bugle "
    "cabin cable cactus camel candle canoe canyon carbon cargo carrot castle cedar cello chalk charm cherry chess "
    "chief cider cinder circus citrus clay cliff cloak clock cloud clover cobra comet coral cotton cougar crane "
    "crater cricket crown crumble crystal cupid daisy dance delta denim desert diesel dingo diver dolphin dragon "
    "dream drift drum eagle earth easel echo elbow ember emerald engine falcon fable feather fern fiddle field fig "
    "finch flame flint flute forest fossil fox frost garden garlic gecko geyser ginger glacier globe goose granite "
    "grape gravel guitar hammer harbor hazel heron honey hornet husky igloo indigo iris island ivory jacket "
    "jasmine jelly jigsaw jungle kayak kettle kiwi koala ladder lagoon lantern lemon lily lime linen lizard llama "
    "lobster lotus magnet mango maple marble meadow melon mint mirror mocha moose mosaic muffin nectar needle "
    "nickel noodle north nutmeg oak oasis ocean olive onion opal orbit orchid otter owl paddle panda paper parrot "
    "peach pebble pepper piano pickle pilot pine plum polar pony poppy prism pumpkin quail quartz quill rabbit "
    "radar raven reef ribbon river robin rocket rose ruby saddle salmon sandal satin scarf shell silver sketch sky "
    "sloth snow sonar spice spruce squid star stone storm sugar summit sunset swan tango thistle thunder tiger "
    "timber toast tomato topaz torch tulip tundra turtle umbrella valley velvet violet violin walnut walrus willow "
    "wizard yarrow zebra").split(' ');

constexpr int PairMinutes = 5;

QJsonObject parse(const QByteArray &text) { return QJsonDocument::fromJson(text).object(); }
QByteArray compact(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }

QByteArray contents(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool saveBytes(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    return QFileInfo(path).absoluteDir().mkpath(".") && file.open(QIODevice::WriteOnly) && file.write(bytes) >= 0 &&
           file.commit();
}

QString hostName() {
    const QString host = QSysInfo::machineHostName().section('.', 0, 0).trimmed().toLower();
    return host.isEmpty() || host == "localhost" ? QString("computer") : host;
}

QString newFolderName() { return QUuid::createUuid().toString(QUuid::Id128).left(12); }

bool readPointerAt(const QString &dataPath, SyncFolder::Pointer *pointer) {
    return SyncFolder::readPointer(contents(dataPath), pointer);
}

const QRegularExpression MachineId("^[0-9a-f]{12}$");
}

QString PeerLink::appDirFor(const QString &usualDataPath) {
    return QFileInfo(usualDataPath.isEmpty() ? Backend::defaultDataPath() : usualDataPath).absolutePath();
}

QString PeerLink::socketName(const QString &appDir) {
    return "needful-link-" +
           QCryptographicHash::hash(QDir::cleanPath(appDir).toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
}

QString PeerLink::newCode() {
    auto *random = QRandomGenerator::system();
    QStringList parts{QString::number(random->bounded(1, 100))};
    for (int i = 0; i < 3; ++i)
        parts.append(Words.at(random->bounded(int(Words.size()))));
    return parts.join('-');
}

PeerLink::PeerLink(const QString &usualDataPath, QObject *parent) : PeerLink(usualDataPath, Options(), parent) {}

PeerLink::PeerLink(const QString &usualDataPath, const Options &options, QObject *parent)
    : QObject(parent),
      m_usual(QFileInfo(usualDataPath.isEmpty() ? Backend::defaultDataPath() : usualDataPath).absoluteFilePath()),
      m_dir(appDirFor(m_usual)), m_options(options), m_devices(m_dir) {
    m_pairExpiry.setSingleShot(true);
    m_pairExpiry.setInterval(PairMinutes * 60 * 1000);
    connect(&m_pairExpiry, &QTimer::timeout, this, [this] {
        cancelPair();
        emit failed(failure("expired"));
    });
    m_offerDelay.setSingleShot(true);
    m_offerDelay.setInterval(300);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_offerDelay.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_offerDelay.start(); });
    connect(&m_offerDelay, &QTimer::timeout, this, [this] {
        reloadDevices();
        loadLists();
        offerAll();
    });
    m_stateDelay.setSingleShot(true);
    m_stateDelay.setInterval(200);
    connect(&m_stateDelay, &QTimer::timeout, this, &PeerLink::saveState);
    // Away from home there's no mDNS to say a machine is back, so look for
    // it now and then at the addresses it was last reached at.
    m_redial.setInterval(30 * 1000);
    connect(&m_redial, &QTimer::timeout, this, &PeerLink::dialAll);
}

PeerLink::~PeerLink() {
    if (!m_link)
        return;
    m_up.clear();
    stopLink();
    saveState();
}

QString PeerLink::name() const {
    const QString own = m_devices.name();
    return own.isEmpty() ? hostName() : own;
}

bool PeerLink::start(QString *error) {
    if (m_lock)
        return true;
    if (!QDir().mkpath(m_dir)) {
        if (error) *error = "Could not create " + m_dir;
        return false;
    }
    m_lock = std::make_unique<QLockFile>(m_dir + "/link.lock");
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) {
        const bool held = m_lock->error() == QLockFile::LockFailedError;
        m_lock.reset();
        if (!held && error)
            *error = "Could not lock " + m_dir + "/link.lock";
        return false;
    }
    // An identity is made the first time this computer pairs.
    if (!m_devices.load(error, QFile::exists(m_devices.path()))) {
        m_lock.reset();
        return false;
    }
    if (QFile::exists(m_devices.path()))
        nameThisDevice();
    m_devicesWritten = contents(m_devices.path());
    loadState();
    ensureSynced();
    loadLists();
    // A computer that has never paired stays off the network until asked to.
    if (!m_devices.paired().isEmpty() && !startLink(error)) {
        m_lock.reset();
        return false;
    }
    if (m_options.ipc) {
        m_server = std::make_unique<QLocalServer>();
        m_server->setSocketOptions(QLocalServer::UserAccessOption);
        const QString socket = socketName(m_dir);
        if (!m_server->listen(socket)) {
            QLocalServer::removeServer(socket); // Left by a Needful that crashed.
            m_server->listen(socket);
        }
        connect(m_server.get(), &QLocalServer::newConnection, this, [this] {
            while (QLocalSocket *socket = m_server->nextPendingConnection())
                serveClient(socket);
        });
    }
    m_redial.start();
    saveState();
    return true;
}

// Named as its own list already calls it, when it syncs; done once this
// computer pairs, so one that never does gets nothing new on disk.
void PeerLink::nameThisDevice() {
    if (m_devices.id().isEmpty())
        m_devices.load(nullptr, true);
    if (!m_devices.name().isEmpty())
        return;
    QString own = hostName();
    SyncFolder::Pointer pointer;
    if (readPointerAt(m_usual, &pointer)) {
        SyncFolder folder(pointer.folder, pointer.machine);
        Store ignored;
        if (folder.read(&ignored, nullptr))
            own = folder.name();
    }
    m_devices.setName(m_devices.id(), own, QDateTime::currentDateTime());
    saveDevices();
}

bool PeerLink::startLink(QString *error) {
    m_relay = m_devices.relay();
    const QJsonObject config{{"secret", m_devices.secret()},
                             {"port", m_options.port},
                             {"relay", m_relay.isEmpty() ? QJsonValue() : QJsonValue(m_relay)},
                             {"mdns", m_options.mdns},
                             {"loopback", m_options.loopback}};
    char *problem = nullptr;
    m_link = needful_link_start(compact(config).constData(), &problem);
    if (!m_link) {
        if (error) *error = "Could not start the link: " + QString::fromUtf8(problem ? problem : "unknown error");
        needful_link_free_string(problem);
        return false;
    }
    m_notifier = std::make_unique<QSocketNotifier>(needful_link_event_fd(m_link), QSocketNotifier::Read);
    connect(m_notifier.get(), &QSocketNotifier::activated, this, &PeerLink::onEvents);
    // Events that came before the notifier did.
    QTimer::singleShot(0, this, &PeerLink::onEvents);
    return true;
}

void PeerLink::stopLink() {
    m_notifier.reset();
    if (m_link)
        needful_link_stop(m_link);
    m_link = nullptr;
}

void PeerLink::command(const QJsonObject &command) {
    if (m_link)
        needful_link_command(m_link, compact(command).constData());
}

void PeerLink::onEvents() {
    while (m_link) {
        char *text = needful_link_poll(m_link);
        if (!text)
            break;
        const QJsonObject event = parse(QByteArray(text));
        needful_link_free_string(text);
        handle(event);
    }
}

void PeerLink::applyTrust() { command({{"op", "trust"}, {"ids", QJsonArray::fromStringList(m_devices.trustedIds())}}); }

void PeerLink::dial(const QString &id, const QStringList &addresses) {
    command({{"op", "dial"}, {"id", id}, {"addrs", QJsonArray::fromStringList(addresses)}});
}

void PeerLink::dialAll() {
    for (const Devices::Device &d : m_devices.paired())
        if (!m_up.contains(d.id) && d.kind == "computer") {
            QStringList addresses = d.reach;
            for (const QString &a : m_addrs.value(d.id))
                if (!addresses.contains(a))
                    addresses.append(a);
            dial(d.id, addresses);
        }
}

void PeerLink::send(const QString &peer, const QJsonObject &message) {
    command({{"op", "send"}, {"id", peer}, {"data", QString::fromUtf8(compact(message))}});
}

QJsonObject PeerLink::info() {
    List *own = mine();
    return {{"needful", 1}, {"name", name()}, {"kind", "computer"}, {"mine", own ? own->id : QString()}};
}

QString PeerLink::failure(const QString &reason) const {
    const bool host = !m_pairCode.isEmpty();
    if (reason == "wrong_code")
        return host ? "Someone tried a wrong code, so this one is spent. Nothing was paired; run needful sync pair "
                      "again for a new code."
                    : "That code didn't match. Nothing was paired, and the code is spent; run needful sync pair "
                      "again there for a new one.";
    if (reason == "not_found")
        return "No computer on this network is waiting for that code. Check the code, or, for a computer on "
               "another network, add --at <host>.";
    if (reason == "not_waiting")
        return "That computer isn't waiting to pair. Run needful sync pair there first; each code works once.";
    if (reason == "unreachable")
        return "Could not reach that computer.";
    if (reason == "unknown_host")
        return "Could not find that host.";
    if (reason == "declined")
        return "Pairing was declined. Nothing was paired.";
    if (reason == "self")
        return "That's this computer's own code. Type it on the other computer.";
    if (reason == "expired")
        return QString("The code expired after %1 minutes without a computer joining. Run needful sync pair again "
                       "for a new one.").arg(PairMinutes);
    return "The connection dropped while pairing. Nothing was paired; try again with a new code.";
}

void PeerLink::pair() {
    QString error;
    if (m_lock)
        nameThisDevice();
    if (!m_lock || (!m_link && !startLink(&error))) {
        emit failed(error.isEmpty() ? QString("The link isn't running here.") : error);
        return;
    }
    // Your own list must be a sync folder for another computer to join it.
    if (!ensureSynced(true)) {
        emit failed("Could not get this computer's list ready to share.");
        return;
    }
    m_pairCode = newCode();
    command({{"op", "pair_open"}, {"code", m_pairCode}, {"info", info()}});
    m_pairExpiry.start();
    emit pairCode(m_pairCode, name(), m_devices.fingerprint());
}

void PeerLink::cancelPair() {
    if (m_pairCode.isEmpty())
        return;
    command({{"op", "pair_close"}});
    m_pairCode.clear();
    m_pairExpiry.stop();
}

void PeerLink::join(const QString &code, const QString &at) {
    QString error;
    if (m_lock)
        nameThisDevice();
    if (!m_lock || (!m_link && !startLink(&error))) {
        emit failed(error.isEmpty() ? QString("The link isn't running here.") : error);
        return;
    }
    if (code.trimmed().isEmpty()) {
        emit failed("Type the code the other computer shows: needful sync join 7-violet-crumble-tango");
        return;
    }
    m_joinCode = code;
    command({{"op", "pair_join"}, {"code", code}, {"at", at}, {"info", info()}});
}

void PeerLink::answer(bool accept) { command({{"op", "pair_answer"}, {"accept", accept}}); }

void PeerLink::handle(const QJsonObject &event) {
    const QString ev = event.value("ev").toString(), id = event.value("id").toString();
    const QDateTime now = QDateTime::currentDateTime();
    if (ev == "ready") {
        m_port = event.value("port").toInt();
        applyTrust();
        dialAll();
        m_stateDelay.start();
    } else if (ev == "up" || ev == "path") {
        const bool fresh = ev == "up";
        m_up[id] = event.value("path").toString();
        m_seen[id] = now;
        QStringList addresses;
        for (const auto &a : event.value("addrs").toArray())
            addresses.append(a.toString());
        if (!addresses.isEmpty())
            m_addrs[id] = addresses;
        m_stateDelay.start();
        if (fresh) {
            sendHello(id);
            sendOffer(id);
        }
        emit changed();
    } else if (ev == "down") {
        m_up.remove(id);
        m_seen[id] = now;
        m_stateDelay.start();
        emit changed();
    } else if (ev == "seen") {
        if (m_devices.trustedIds().contains(id) && !m_up.contains(id))
            dial(id);
    } else if (ev == "msg") {
        handleMessage(id, parse(event.value("data").toString().toUtf8()));
    } else if (ev == "pair_confirm") {
        const QJsonObject theirs = event.value("info").toObject();
        m_joinInfo = theirs;
        emit confirm(theirs.value("name").toString(), Devices::fingerprintOf(id));
    } else if (ev == "paired") {
        const QJsonObject theirs = event.value("info").toObject();
        m_devices.pair(id, theirs.value("name").toString(), theirs.value("kind").toString(), now);
        saveDevices();
        QStringList addresses;
        for (const auto &a : event.value("addrs").toArray())
            addresses.append(a.toString());
        if (!addresses.isEmpty())
            m_addrs[id] = addresses;
        applyTrust();
        if (!m_joinCode.isEmpty()) {
            m_joinCode.clear();
            m_joinFrom = id;
            m_joinInfo = theirs;
        } else {
            m_pairCode.clear();
            m_pairExpiry.stop();
            ensureSynced();
            emit paired(theirs.value("name").toString(), Devices::fingerprintOf(id));
        }
        dial(id, addresses);
        m_stateDelay.start();
        emit changed();
    } else if (ev == "pair_failed") {
        const QString message = failure(event.value("reason").toString());
        m_pairCode.clear();
        m_pairExpiry.stop();
        m_joinCode.clear();
        emit failed(message);
    }
}

void PeerLink::sendHello(const QString &peer) { send(peer, {{"t", "hello"}, {"devices", m_devices.toJson()}}); }

void PeerLink::handleMessage(const QString &peer, const QJsonObject &message) {
    const QString type = message.value("t").toString();
    if (type == "hello") {
        if (!m_devices.merge(message.value("devices").toObject()))
            return;
        saveDevices();
        applyTrust();
        if (m_devices.relay() != m_relay) {
            // A relay was set or cleared on another computer.
            stopLink();
            m_up.clear();
            startLink(nullptr);
            return;
        }
        ensureSynced();
        for (auto it = m_up.cbegin(); it != m_up.cend(); ++it)
            if (it.key() != peer)
                sendHello(it.key());
        dialAll();
        emit changed();
    } else if (type == "offer") {
        handleOffer(peer, message.value("lists").toArray());
    } else if (type == "get") {
        List *list = listById(message.value("list").toString());
        if (!list)
            return;
        for (const auto &value : message.value("machines").toArray()) {
            const QString machine = value.toString();
            if (!MachineId.match(machine).hasMatch())
                continue;
            const QByteArray bytes = contents(list->folder + "/machines/" + machine + ".json");
            if (bytes.isEmpty())
                continue;
            send(peer, {{"t", "file"}, {"list", list->id}, {"machine", machine}, {"data", QString::fromUtf8(bytes)}});
            emit sent(list->id, machine, peer);
        }
    } else if (type == "file") {
        storeFile(peer, message);
    }
}

void PeerLink::sendOffer(const QString &peer) {
    QJsonArray lists;
    for (const auto &list : m_lists) {
        if (list->id.isEmpty())
            continue;
        Store ignored;
        list->sync->read(&ignored, nullptr);
        QJsonObject files;
        const auto known = list->sync->files();
        for (auto it = known.cbegin(); it != known.cend(); ++it)
            files[it.key()] = QJsonObject{{"saved", it.value().saved}, {"clock", it.value().clock}};
        m_offered[list->id] = known;
        lists.append(QJsonObject{{"id", list->id},
                                 {"name", list->universe},
                                 {"mine", list->mine},
                                 {"machine", list->machine},
                                 {"files", files}});
    }
    send(peer, {{"t", "offer"}, {"lists", lists}});
}

void PeerLink::offerAll() {
    bool different = false;
    for (const auto &list : m_lists) {
        if (list->id.isEmpty())
            continue;
        const auto now = list->sync->files();
        const auto before = m_offered.value(list->id);
        if (now.size() != before.size()) {
            different = true;
            break;
        }
        for (auto it = now.cbegin(); it != now.cend(); ++it)
            if (!before.contains(it.key()) || it.value().newerThan(before[it.key()]) ||
                before[it.key()].newerThan(it.value()))
                different = true;
    }
    if (m_offered.size() != qsizetype(std::count_if(m_lists.begin(), m_lists.end(),
                                                    [](const auto &l) { return !l->id.isEmpty(); })))
        different = true;
    if (!different)
        return;
    for (auto it = m_up.cbegin(); it != m_up.cend(); ++it)
        sendOffer(it.key());
}

PeerLink::List *PeerLink::listById(const QString &id) {
    if (id.isEmpty())
        return nullptr;
    for (auto &list : m_lists)
        if (list->id == id)
            return list.get();
    return nullptr;
}

PeerLink::List *PeerLink::mine() {
    for (auto &list : m_lists)
        if (list->mine)
            return list.get();
    return nullptr;
}

// The same list as one offered: by its id, or, for a folder whose machines
// made up ids of their own before hearing of each other, by a machine they
// have in common.
PeerLink::List *PeerLink::match(const QJsonObject &offered) {
    if (List *found = listById(offered.value("id").toString()))
        return found;
    const QJsonObject files = offered.value("files").toObject();
    const bool isMine = offered.value("mine").toBool();
    for (auto &list : m_lists) {
        if (list->mine != isMine)
            continue;
        const auto known = list->sync->files();
        for (auto it = files.begin(); it != files.end(); ++it)
            if (known.contains(it.key()))
                return list.get();
    }
    return nullptr;
}

void PeerLink::handleOffer(const QString &peer, const QJsonArray &lists) {
    const bool joining = peer == m_joinFrom;
    for (const auto &value : lists) {
        const QJsonObject offered = value.toObject();
        const QString id = offered.value("id").toString();
        const QJsonObject files = offered.value("files").toObject();
        if (id.isEmpty())
            continue;

        if (joining && offered.value("mine").toBool() && !m_staging) {
            List *own = mine();
            if (own && (own->id == id || match(offered) == own)) {
                finishJoin();
                continue;
            }
            // Take in their list first, then merge in what only this one had.
            m_staging = std::make_unique<Staging>();
            m_staging->peer = peer;
            m_staging->list = id;
            m_staging->folder = m_dir + "/lists/" + newFolderName();
            QDir().mkpath(m_staging->folder + "/machines");
            QJsonArray wanted;
            for (auto it = files.begin(); it != files.end(); ++it) {
                m_staging->waiting.insert(it.key());
                wanted.append(it.key());
            }
            if (wanted.isEmpty())
                finishJoin();
            else
                send(peer, {{"t", "get"}, {"list", id}, {"machines", wanted}});
            continue;
        }
        if (m_staging && m_staging->list == id)
            continue;

        List *local = match(offered);
        if (!local) {
            if (offered.value("mine").toBool() || Universes(m_usual).hidden().contains(id))
                continue;
            if (createUniverse(offered).isEmpty())
                continue;
            loadLists();
            local = listById(id);
            if (!local)
                continue;
        }
        Store ignored;
        local->sync->read(&ignored, nullptr);
        const auto known = local->sync->files();
        QJsonArray wanted;
        for (auto it = files.begin(); it != files.end(); ++it) {
            if (it.key() == local->machine || !MachineId.match(it.key()).hasMatch())
                continue;
            const QJsonObject f = it.value().toObject();
            const SyncFolder::File theirs{qint64(f.value("saved").toInteger()), f.value("clock").toString(), {}};
            if (!known.contains(it.key()) || theirs.newerThan(known[it.key()]))
                wanted.append(it.key());
        }
        // Until their files reach each other, two computers can each know
        // the list by its own id; they ask for files by the other's.
        if (id != local->id)
            m_aliases[id] = local->id;
        if (!wanted.isEmpty())
            send(peer, {{"t", "get"}, {"list", id}, {"machines", wanted}});
    }
}

QString PeerLink::createUniverse(const QJsonObject &offered) {
    Universes universes(m_usual);
    QString base = offered.value("name").toString();
    if (!Universes::validName(base) || base == Universes::Mine)
        base = "shared";
    QString chosen = base;
    for (int n = 2; !universes.dataPath(chosen).isEmpty(); ++n)
        chosen = QString("%1-%2").arg(base).arg(n);
    QString error;
    const QString path = universes.add(chosen, &error);
    if (path.isEmpty())
        return {};
    SyncFolder::Setup result;
    if (!SyncFolder::setup(path, m_dir + "/lists/" + newFolderName(), name(), QDateTime::currentDateTime(), &result,
                           &error, path, offered.value("id").toString())) {
        universes.remove(chosen, nullptr);
        return {};
    }
    return chosen;
}

void PeerLink::storeFile(const QString &peer, const QJsonObject &message) {
    const QString listId = message.value("list").toString(), machine = message.value("machine").toString();
    if (!MachineId.match(machine).hasMatch())
        return;
    const bool staged = m_staging && m_staging->list == listId;
    QString folder, own;
    if (staged) {
        folder = m_staging->folder;
    } else if (List *list = listById(listId) ? listById(listId) : listById(m_aliases.value(listId))) {
        folder = list->folder;
        own = list->machine;
    } else {
        return;
    }
    // Only this machine writes its own file.
    if (machine == own)
        return;
    const QByteArray bytes = message.value("data").toString().toUtf8();
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    if (!document.isObject() || document.object().value("machine").toString(machine) != machine)
        return;
    const QString path = folder + "/machines/" + machine + ".json";
    const QJsonObject existing = parse(contents(path));
    if (!existing.isEmpty() && !SyncFolder::describe(document.object()).newerThan(SyncFolder::describe(existing))) {
        if (staged && m_staging->waiting.remove(machine) && m_staging->waiting.isEmpty())
            finishJoin();
        return;
    }
    if (!saveBytes(path, bytes))
        return;
    emit received(listId, machine, peer);
    if (staged && m_staging->waiting.remove(machine) && m_staging->waiting.isEmpty())
        finishJoin();
}

void PeerLink::finishJoin() {
    QVariantMap result{{"name", m_joinInfo.value("name").toString()},
                       {"fingerprint", Devices::fingerprintOf(m_joinFrom)},
                       {"already", !m_staging}};
    const QString from = m_joinFrom;
    m_joinFrom.clear();
    if (m_staging) {
        // A list already in a folder takes theirs in where it is: each
        // computer keeps its files wherever it likes, and machines sharing
        // that folder through a sync tool come along. One in a single file
        // moves into theirs, merging as setup does.
        SyncFolder::Pointer pointer;
        SyncFolder::Setup setup;
        QString error;
        bool ok;
        if (readPointerAt(m_usual, &pointer) && !pointer.elsewhere()) {
            SyncFolder folder(pointer.folder, pointer.machine);
            ok = folder.mergeFrom(m_staging->folder, QDateTime::currentDateTime(), &setup, &error);
            QDir(m_staging->folder).removeRecursively();
        } else {
            ok = SyncFolder::setup(m_usual, m_staging->folder, name(), QDateTime::currentDateTime(), &setup, &error,
                                   m_usual);
        }
        m_staging.reset();
        if (!ok) {
            emit failed("Could not take in the other computer's list: " + error);
            return;
        }
        if (setup.name != name()) {
            m_devices.setName(m_devices.id(), setup.name, QDateTime::currentDateTime());
            saveDevices();
        }
        result["tasks"] = setup.tasks;
        result["contexts"] = setup.contexts;
        result["history"] = setup.history;
    }
    result["this"] = name();
    loadLists();
    ensureSynced();
    for (auto it = m_up.cbegin(); it != m_up.cend(); ++it) {
        sendHello(it.key());
        sendOffer(it.key());
    }
    send(from, {{"t", "joined"}});
    emit joined(result);
}

// Every list is a sync folder once this computer has someone to share it
// with; one that was a single file moves into a folder of its own. Until
// then, nothing changes on disk.
bool PeerLink::ensureSynced(bool pairing) {
    const bool sharing = !m_devices.paired().isEmpty();
    // Nothing to share with yet, except your own list while pairing.
    if (!sharing && !pairing)
        return true;
    bool ok = true;
    bool changed = false;
    for (const Universe &u : Universes(m_usual).list()) {
        if (!sharing && u.name != Universes::Mine)
            continue;
        SyncFolder::Pointer pointer;
        if (readPointerAt(u.dataPath, &pointer))
            continue;
        SyncFolder::Setup result;
        QString error;
        if (!SyncFolder::setup(u.dataPath, m_dir + "/lists/" + newFolderName(), name(), QDateTime::currentDateTime(),
                               &result, &error, u.dataPath)) {
            qWarning("needful: can't share %s: %s", qPrintable(u.name), qPrintable(error));
            if (u.name == Universes::Mine)
                ok = false;
            continue;
        }
        changed = true;
    }
    if (changed)
        loadLists();
    return ok;
}

void PeerLink::loadLists() {
    std::vector<std::unique_ptr<List>> lists;
    QStringList watch{m_dir};
    for (const Universe &u : Universes(m_usual).list()) {
        SyncFolder::Pointer pointer;
        if (!readPointerAt(u.dataPath, &pointer) || pointer.elsewhere())
            continue;
        auto list = std::make_unique<List>();
        list->universe = u.name;
        list->dataPath = u.dataPath;
        list->folder = pointer.folder;
        list->machine = pointer.machine;
        list->mine = u.name == Universes::Mine;
        list->sync = std::make_unique<SyncFolder>(pointer.folder, pointer.machine);
        Store ignored;
        if (!list->sync->read(&ignored, nullptr))
            continue;
        // Written once, and only for a computer that shares its lists.
        if ((!m_devices.paired().isEmpty() || !m_pairCode.isEmpty() || !m_joinCode.isEmpty()) &&
            (list->sync->listId().isEmpty() || !list->sync->files().contains(list->machine) ||
             list->sync->files().value(list->machine).device != m_devices.id())) {
            list->sync->ensureListId(nullptr);
            list->sync->setDevice(m_devices.id(), nullptr);
        }
        list->id = list->sync->listId();
        watch.append(list->sync->machinesPath());
        lists.push_back(std::move(list));
    }
    m_lists = std::move(lists);
    const QStringList current = m_watcher.directories();
    for (const QString &path : current)
        if (!watch.contains(path))
            m_watcher.removePath(path);
    for (const QString &path : watch)
        if (!current.contains(path) && QFileInfo(path).isDir())
            m_watcher.addPath(path);
}

// Merges in what's on disk first, so a change the command line made there
// isn't saved over.
void PeerLink::saveDevices() {
    Devices disk(m_dir);
    if (disk.load(nullptr))
        m_devices.merge(disk.toJson());
    m_devices.save(nullptr);
    m_devicesWritten = contents(m_devices.path());
}

void PeerLink::reloadDevices() {
    const QByteArray now = contents(m_devices.path());
    if (now == m_devicesWritten)
        return;
    // Changed from the command line: unpair, reach, relay, rename.
    m_devicesWritten = now;
    const QString relay = m_devices.relay();
    saveDevices();
    applyTrust();
    if (m_devices.relay() != relay) {
        stopLink();
        m_up.clear();
        startLink(nullptr);
    }
    ensureSynced();
    for (auto it = m_up.cbegin(); it != m_up.cend(); ++it)
        sendHello(it.key());
    dialAll();
    emit changed();
}

void PeerLink::saveState() {
    if (!m_lock || (!m_link && !QFile::exists(m_devices.path())))
        return;
    QJsonObject devices;
    QSet<QString> ids;
    for (auto it = m_seen.cbegin(); it != m_seen.cend(); ++it)
        ids.insert(it.key());
    for (auto it = m_addrs.cbegin(); it != m_addrs.cend(); ++it)
        ids.insert(it.key());
    for (const QString &id : ids)
        devices[id] = QJsonObject{{"link", m_up.contains(id) ? QJsonValue(m_up[id]) : QJsonValue()},
                                  {"seen", m_seen.value(id).toUTC().toString(Qt::ISODateWithMs)},
                                  {"addrs", QJsonArray::fromStringList(m_addrs.value(id))}};
    const QJsonObject state{{"running", m_link != nullptr},
                            {"pid", QCoreApplication::applicationPid()},
                            {"port", m_port},
                            {"devices", devices}};
    saveBytes(m_dir + "/link-state.json", QJsonDocument(state).toJson(QJsonDocument::Indented));
}

void PeerLink::loadState() {
    const QJsonObject devices = parse(contents(m_dir + "/link-state.json")).value("devices").toObject();
    for (auto it = devices.begin(); it != devices.end(); ++it) {
        const QJsonObject d = it.value().toObject();
        const QDateTime seen = QDateTime::fromString(d.value("seen").toString(), Qt::ISODateWithMs);
        if (seen.isValid())
            m_seen[it.key()] = seen;
        QStringList addresses;
        for (const auto &a : d.value("addrs").toArray())
            addresses.append(a.toString());
        if (!addresses.isEmpty())
            m_addrs[it.key()] = addresses;
    }
}

// A line of JSON each way: {"op": "pair"}, {"op": "join", "code", "at"},
// {"op": "answer", "accept"}, {"op": "cancel"}; events come back as
// {"ev": "code" | "paired" | "confirm" | "joined" | "failed", ...}.
void PeerLink::serveClient(QLocalSocket *socket) {
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    auto reply = [socket](const QJsonObject &message) {
        socket->write(compact(message) + "\n");
        socket->flush();
    };
    auto hook = [this, socket, reply] {
        connect(this, &PeerLink::pairCode, socket, [reply](const QString &code, const QString &name, const QString &fp) {
            reply({{"ev", "code"}, {"code", code}, {"name", name}, {"fingerprint", fp}});
        });
        connect(this, &PeerLink::paired, socket, [reply](const QString &name, const QString &fp) {
            reply({{"ev", "paired"}, {"name", name}, {"fingerprint", fp}});
        });
        connect(this, &PeerLink::confirm, socket, [reply](const QString &name, const QString &fp) {
            reply({{"ev", "confirm"}, {"name", name}, {"fingerprint", fp}});
        });
        connect(this, &PeerLink::joined, socket, [reply](const QVariantMap &result) {
            QJsonObject message = QJsonObject::fromVariantMap(result);
            message["ev"] = "joined";
            reply(message);
        });
        connect(this, &PeerLink::failed, socket, [reply](const QString &message) {
            reply({{"ev", "failed"}, {"message", message}});
        });
    };
    auto pairing = std::make_shared<bool>(false);
    connect(socket, &QLocalSocket::disconnected, this, [this, pairing] {
        if (*pairing)
            cancelPair();
    });
    connect(socket, &QLocalSocket::readyRead, this, [this, socket, hook, pairing] {
        while (socket->canReadLine()) {
            const QJsonObject request = parse(socket->readLine());
            const QString op = request.value("op").toString();
            if (op == "pair") {
                hook();
                *pairing = true;
                pair();
            } else if (op == "join") {
                hook();
                join(request.value("code").toString(), request.value("at").toString());
            } else if (op == "answer") {
                answer(request.value("accept").toBool());
            } else if (op == "cancel") {
                cancelPair();
            }
        }
    });
}

PeerStatus PeerStatus::read(const QString &appDir, const SyncFolder *list) {
    PeerStatus status;
    Devices devices(appDir);
    devices.load(nullptr, false);
    status.name = devices.name();
    status.fingerprint = devices.fingerprint();
    status.relay = devices.relay();
    {
        QLockFile lock(appDir + "/link.lock");
        lock.setStaleLockTime(0);
        status.running = !lock.tryLock(0);
        if (!status.running)
            lock.unlock();
    }
    const QJsonObject state = parse(contents(appDir + "/link-state.json")).value("devices").toObject();
    QHash<QString, QDateTime> changed;
    if (list) {
        const auto files = list->files();
        for (const auto &m : list->machines()) {
            const QString device = files.value(m.id).device;
            if (!device.isEmpty() && (!changed.contains(device) || changed[device] < m.changed))
                changed[device] = m.changed;
        }
    }
    for (const Devices::Device &d : devices.paired()) {
        const QJsonObject s = state.value(d.id).toObject();
        status.devices.append({d, status.running ? s.value("link").toString() : QString(),
                               QDateTime::fromString(s.value("seen").toString(), Qt::ISODateWithMs),
                               changed.value(d.id)});
    }
    return status;
}

LinkClient::LinkClient(const QString &appDir, QObject *parent) : QObject(parent), m_dir(appDir) {
    connect(&m_socket, &QLocalSocket::readyRead, this, [this] {
        while (m_socket.canReadLine()) {
            const QJsonObject event = parse(m_socket.readLine());
            const QString ev = event.value("ev").toString();
            m_done = ev == "paired" || ev == "joined" || ev == "failed";
            if (ev == "code")
                emit pairCode(event.value("code").toString(), event.value("name").toString(),
                              event.value("fingerprint").toString());
            else if (ev == "paired")
                emit paired(event.value("name").toString(), event.value("fingerprint").toString());
            else if (ev == "confirm")
                emit confirm(event.value("name").toString(), event.value("fingerprint").toString());
            else if (ev == "joined")
                emit joined(event.toVariantMap());
            else if (ev == "failed")
                emit failed(event.value("message").toString());
        }
    });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        if (!m_done)
            emit failed("The Needful holding the link here stopped.");
    });
}

bool LinkClient::connectToOwner() {
    m_socket.connectToServer(PeerLink::socketName(m_dir));
    return m_socket.waitForConnected(2000);
}

void LinkClient::write(const QJsonObject &message) {
    m_socket.write(compact(message) + "\n");
    m_socket.flush();
}

void LinkClient::pair() { write({{"op", "pair"}}); }
void LinkClient::join(const QString &code, const QString &at) { write({{"op", "join"}, {"code", code}, {"at", at}}); }
void LinkClient::answer(bool accept) { write({{"op", "answer"}, {"accept", accept}}); }
void LinkClient::cancel() { write({{"op", "cancel"}}); }

QString PeerLink::listIdOf(const QString &universe) const {
    for (const auto &list : m_lists)
        if (list->universe == universe)
            return list->id;
    return {};
}
