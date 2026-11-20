#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <memory>

class PeerLink;
class LinkClient;

// Pairing, joining and unpairing from the window, through the link this
// window holds, or through whichever Needful here holds it.
//
// state is "" (nothing going on), "pairing" (showing a code), "joining"
// (looking for the other computer), "confirm" (asking whether to pair with
// otherName), "done" or "failed", with message saying what happened.
class PeerPanel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString code READ code NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString thisName READ thisName NOTIFY changed)
    Q_PROPERTY(QString thisFingerprint READ thisFingerprint NOTIFY changed)
    Q_PROPERTY(QString otherName READ otherName NOTIFY changed)
    Q_PROPERTY(QString otherFingerprint READ otherFingerprint NOTIFY changed)
    // Paired computers, as {id, name, fingerprint}.
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)

public:
    // link may be null, or not holding: then a Needful that does is asked.
    PeerPanel(PeerLink *link, const QString &appDir, QObject *parent = nullptr);
    ~PeerPanel() override;

    QString state() const { return m_state; }
    QString code() const { return m_code; }
    QString message() const { return m_message; }
    QString thisName() const { return m_thisName; }
    QString thisFingerprint() const { return m_thisFingerprint; }
    QString otherName() const { return m_otherName; }
    QString otherFingerprint() const { return m_otherFingerprint; }
    QVariantList devices() const { return m_devices; }

    Q_INVOKABLE void pair();
    Q_INVOKABLE void join(const QString &code, const QString &at);
    Q_INVOKABLE void answer(bool accept);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool unpair(const QString &id);

signals:
    void changed();
    void devicesChanged();

private:
    template <typename Source> void wire(Source *source);
    void set(const QString &state, const QString &message = {});
    void loadDevices();
    bool useLink();

    PeerLink *m_link;
    std::unique_ptr<LinkClient> m_client;
    QString m_dir;
    QString m_state, m_code, m_message, m_thisName, m_thisFingerprint, m_otherName, m_otherFingerprint;
    QVariantList m_devices;
    QTimer m_poll;
};
