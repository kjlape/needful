#include "peerpanel.h"

#include <QDateTime>
#include <QFile>
#include <QLocale>

#include "devices.h"
#include "peers.h"

PeerPanel::PeerPanel(PeerLink *link, const QString &appDir, QObject *parent)
    : QObject(parent), m_link(link), m_dir(appDir) {
    if (m_link && m_link->holding())
        wire(m_link);
    loadDevices();
    // Paired from the command line or another computer: the menu follows.
    m_poll.setInterval(5000);
    connect(&m_poll, &QTimer::timeout, this, &PeerPanel::loadDevices);
    m_poll.start();
}

PeerPanel::~PeerPanel() = default;

bool PeerPanel::useLink() { return m_link && m_link->holding(); }

QString PeerPanel::usual() const { return m_link ? m_link->usualDataPath() : QString(); }

template <typename Source> void PeerPanel::wire(Source *source) {
    connect(source, &Source::pairCode, this, [this](const QString &code, const QString &name, const QString &fp) {
        m_code = code;
        m_thisName = name;
        m_thisFingerprint = fp;
        set("pairing");
    });
    connect(source, &Source::paired, this, [this](const QString &name, const QString &fp) {
        if (m_state != "pairing")
            return; // The joining side says so once it has the list.
        m_otherName = name;
        m_otherFingerprint = fp;
        loadDevices();
        if (!m_universe.isEmpty())
            set("done", tr("“%1” joined “%2”.").arg(name, m_universe));
        else
            set("done", tr("Paired with \"%1\". It's joining this computer's list.").arg(name));
    });
    connect(source, &Source::confirm, this, [this](const QString &name, const QString &fp, const QString &universe) {
        m_otherName = name;
        m_otherFingerprint = fp;
        m_universe = universe;
        set("confirm");
    });
    connect(source, &Source::joined, this, [this](const QVariantMap &result) {
        loadDevices();
        if (result["shared"].toBool()) {
            const QString universe = result["universe"].toString();
            const int tasks = result["tasks"].toInt();
            m_universe = universe;
            set("done", (result["already"].toBool() ? tr("This computer already has “%1”.").arg(universe)
                                                     : tr("Joined “%1” as a new universe.").arg(universe)) +
                            " " + tr("%n tasks.", "", tasks));
            emit joinedUniverse(universe);
            return;
        }
        const int tasks = result["tasks"].toInt();
        const QString merged = result["already"].toBool() ? tr("This computer already had its list.")
                               : tasks ? tr("Joined its list. Merged %n tasks that were only here.", "", tasks)
                                       : tr("Joined its list. Nothing was only here.");
        set("done", tr("Paired with \"%1\".").arg(result["name"].toString()) + " " + merged);
    });
    connect(source, &Source::failed, this, [this](const QString &message) {
        if (m_state.isEmpty() || m_state == "done")
            return;
        set("failed", message);
    });
}

void PeerPanel::set(const QString &state, const QString &message) {
    m_state = state;
    m_message = message;
    emit changed();
}

bool PeerPanel::connectClient() {
    m_client = std::make_unique<LinkClient>(m_dir);
    wire(m_client.get());
    if (m_client->connectToOwner())
        return true;
    set("failed", tr("Another Needful here holds the link but isn't answering. Close it and try again."));
    return false;
}

void PeerPanel::pair() {
    m_code.clear();
    m_otherName.clear();
    m_universe.clear();
    set("waiting");
    if (useLink())
        m_link->pair();
    else if (connectClient())
        m_client->pair();
}

void PeerPanel::join(const QString &code, const QString &at) {
    m_otherName.clear();
    m_universe.clear();
    set("joining");
    if (useLink())
        m_link->join(code, at);
    else if (connectClient())
        m_client->join(code, at);
}

void PeerPanel::share(const QString &universe) {
    m_code.clear();
    m_otherName.clear();
    m_universe = universe;
    set("waiting");
    if (useLink())
        m_link->share(universe);
    else if (connectClient())
        m_client->share(universe);
}

void PeerPanel::joinUniverse(const QString &code, const QString &at) {
    m_otherName.clear();
    m_universe.clear();
    set("joining");
    if (useLink())
        m_link->join(code, at, true);
    else if (connectClient())
        m_client->join(code, at, true);
}

QVariantMap PeerPanel::members(const QString &universe) const {
    const UniverseMembers read = UniverseMembers::read(usual(), universe);
    QVariantList others;
    for (const auto &e : read.others) {
        const QString status = !e.link.isEmpty() ? tr("connected")
                               : !e.via.isEmpty() ? tr("via %1").arg(e.via)
                               : e.seen.isValid()
                                   ? tr("last seen %1").arg(QLocale().toString(e.seen.toLocalTime().date(), QLocale::ShortFormat))
                                   : tr("not seen yet");
        others.append(QVariantMap{{"id", e.member.id},
                                  {"name", e.member.name},
                                  {"fingerprint", e.member.fingerprint()},
                                  {"status", status},
                                  {"removable", e.via.isEmpty()}});
    }
    return {{"yours", read.yours.join(", ")}, {"others", others}};
}

QString PeerPanel::unshare(const QString &universe, const QString &id) {
    QString error;
    if (UniverseMembers::remove(usual(), universe, id, &error).isEmpty())
        return error;
    return {};
}

void PeerPanel::answer(bool accept) {
    if (useLink())
        m_link->answer(accept);
    else if (m_client)
        m_client->answer(accept);
    if (accept)
        set("joining");
    else
        set("", {});
}

void PeerPanel::cancel() {
    if (m_state == "pairing" || m_state == "waiting") {
        if (useLink())
            m_link->cancelPair();
        else if (m_client)
            m_client->cancel();
    } else if (m_state == "confirm") {
        answer(false);
    }
    m_client.reset();
    m_universe.clear();
    set("", {});
}

bool PeerPanel::unpair(const QString &id) {
    Devices devices(m_dir);
    if (!devices.load(nullptr))
        return false;
    devices.unpair(id, QDateTime::currentDateTime());
    if (!devices.save(nullptr))
        return false;
    loadDevices();
    return true;
}

void PeerPanel::loadDevices() {
    Devices devices(m_dir);
    QVariantList list;
    // A computer that never paired gets no identity just for looking.
    if (QFile::exists(devices.path()) && devices.load(nullptr))
        for (const auto &d : devices.paired())
            list.append(QVariantMap{{"id", d.id}, {"name", d.name}, {"fingerprint", d.fingerprint()}});
    if (list == m_devices)
        return;
    m_devices = list;
    emit devicesChanged();
}
