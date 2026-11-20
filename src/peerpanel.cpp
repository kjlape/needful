#include "peerpanel.h"

#include <QDateTime>
#include <QFile>

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
        set("done", tr("Paired with \"%1\". It's joining this computer's list.").arg(name));
    });
    connect(source, &Source::confirm, this, [this](const QString &name, const QString &fp) {
        m_otherName = name;
        m_otherFingerprint = fp;
        set("confirm");
    });
    connect(source, &Source::joined, this, [this](const QVariantMap &result) {
        loadDevices();
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

void PeerPanel::pair() {
    m_code.clear();
    m_otherName.clear();
    set("waiting");
    if (useLink()) {
        m_link->pair();
        return;
    }
    m_client = std::make_unique<LinkClient>(m_dir);
    wire(m_client.get());
    if (!m_client->connectToOwner()) {
        set("failed", tr("Another Needful here holds the link but isn't answering. Close it and try again."));
        return;
    }
    m_client->pair();
}

void PeerPanel::join(const QString &code, const QString &at) {
    m_otherName.clear();
    set("joining");
    if (useLink()) {
        m_link->join(code, at);
        return;
    }
    m_client = std::make_unique<LinkClient>(m_dir);
    wire(m_client.get());
    if (!m_client->connectToOwner()) {
        set("failed", tr("Another Needful here holds the link but isn't answering. Close it and try again."));
        return;
    }
    m_client->join(code, at);
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
