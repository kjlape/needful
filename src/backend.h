#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariant>
#include <memory>

#include "store.h"
#include "sync.h"

// The window's view of the store: which list is showing, filtered to which
// context, and every change the person makes, each one saved at once and
// undoable. Lists go to QML as plain maps; a life's chores are small enough
// that rebuilding them on each change is instant.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(int contextFilter READ contextFilter WRITE setContextFilter NOTIFY viewChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY viewChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(QVariantList doneToday READ doneToday NOTIFY changed)
    Q_PROPERTY(QVariantList contexts READ contexts NOTIFY changed)
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY changed)
    Q_PROPERTY(QString todayText READ todayText NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString dataPath READ dataPath CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    // For a synced machine, when the others were last heard from; empty when
    // this machine doesn't sync. syncLate says one hasn't been for a while.
    Q_PROPERTY(QString syncText READ syncText NOTIFY syncChanged)
    Q_PROPERTY(bool syncLate READ syncLate NOTIFY syncChanged)

public:
    // An empty path means the platform's app data folder: ~/.local/share on
    // Linux, inside the sandbox under Flatpak, Application Support on macOS.
    explicit Backend(const QString &dataPath = {}, QObject *parent = nullptr);

    static QString defaultDataPath();

    QString view() const { return m_view; }
    void setView(const QString &view);
    int contextFilter() const { return m_contextFilter; }
    void setContextFilter(int id);
    QString search() const { return m_search; }
    void setSearch(const QString &search);

    QVariantList items() const { return m_items; }
    QVariantList doneToday() const { return m_doneToday; }
    QVariantList contexts() const { return m_contexts; }
    QVariantMap counts() const { return m_counts; }
    QString todayText() const;
    QString error() const { return m_error; }
    QString dataPath() const { return m_path; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString syncText() const { return m_syncText; }
    bool syncLate() const { return m_syncLate; }
    // The folder this machine syncs through, or null for one file.
    SyncFolder *sync() const { return m_sync.get(); }
    // "just now", "4 minutes ago", "yesterday"; brief says "4 min ago".
    QString heardText(const QDateTime &at, bool brief = false) const;

    // Tests pin the date; the app follows the calendar.
    void setToday(const QDate &day);
    QDate today() const { return m_fixedToday.isValid() ? m_fixedToday : QDate::currentDate(); }
    // A pinned day ticks a second per change, so order stays clear in tests.
    QDateTime now() { return m_fixedToday.isValid() ? QDateTime(m_fixedToday, QTime(12, 0)).addSecs(m_tick++) : QDateTime::currentDateTime(); }
    const Store &store() const { return m_store; }

    Q_INVOKABLE QVariantMap task(int id) const;
    Q_INVOKABLE int saveTask(const QVariantMap &fields); // id 0 adds a task.
    Q_INVOKABLE void removeTask(int id);
    Q_INVOKABLE void complete(int id, const QString &note, const QString &nextDue = {});
    Q_INVOKABLE void snooze(int id, int days);
    Q_INVOKABLE void unsnooze(int id);
    Q_INVOKABLE void toggleToday(int id);
    Q_INVOKABLE int addContext(const QString &name, int parent);
    Q_INVOKABLE void renameContext(int id, const QString &name);
    Q_INVOKABLE void removeContext(int id);
    Q_INVOKABLE void removeCompletion(int id);
    // A note on a context; with followUp, a one-time task to deal with it.
    // Fields: context, text, followUp, priority, due (ISO). Returns its id.
    Q_INVOKABLE int addLog(const QVariantMap &fields);
    Q_INVOKABLE void removeLog(int id);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void dismissError();

    // Dates for the dialogs, as ISO text QML can hand back.
    Q_INVOKABLE QString dateAfter(int amount, const QString &unit) const;
    Q_INVOKABLE QString formatDate(const QString &iso) const;
    Q_INVOKABLE QString suggestedNext(int id) const;

public slots:
    void refresh();

signals:
    void viewChanged();
    void changed();
    void errorChanged();
    void syncChanged();

private:
    template <typename Change> bool change(Change apply);
    void restore(const QByteArray &json);
    bool persist(QString *message);
    void updateSync();
    void reloadFromDisk();
    enum CatchUp { Current, CaughtUp, Unreadable };
    CatchUp catchUp();
    void watch();
    void setError(const QString &message);
    bool matches(const QString &text) const;

    QVariantMap describe(const Task &task, const QDate &today) const;
    QVariantMap describe(const Completion &done) const;
    QVariantMap describe(const LogEntry &entry) const;
    QString dueText(const Task &task, const QDate &today) const;
    QString scheduleText(const Task &task) const;
    QString seasonText(const Task &task) const;
    QString agoText(const QDate &day, const QDate &today) const;
    QDate returnsOn(const Task &task, const QDate &today) const;

    QString m_path;
    Store m_store;
    QString m_view = "needful";
    int m_contextFilter = 0;
    QString m_search;
    QVariantList m_items, m_doneToday, m_contexts;
    QVariantMap m_counts;
    QString m_error;
    std::unique_ptr<SyncFolder> m_sync;
    QString m_syncText;
    bool m_syncLate = false;
    QList<QByteArray> m_undo, m_redo;
    QByteArray m_written;
    QDate m_fixedToday, m_shownDay;
    int m_tick = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadDelay, m_clock;
};
