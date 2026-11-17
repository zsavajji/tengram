#ifndef MEMORYPATCH_H
#define MEMORYPATCH_H

#include <QObject>
#include <QProcess>
#include <QString>

// Installs the headless-mem addon (../rlimit-patch, docs/headless-cap.md) as root, then ends the running engine so
// that the UI's usual START brings it back under the new limit. The addon raises authman's _sys_headless_xlarge
// from 30 MiB to 128 MiB: without it the engine cannot get through a first sync. Ported from VoLTE Manager's
// AddonSetup, reduced to this one action: the app runs the commands itself through AutoRoot v4's helper
// (/base/bin/__root, or the older /base/bin/u_root), which becomes root and starts /bin/ksh reading its commands
// from the helper's stdin until EOF. It always exits 0, so a run is judged by a marker line in its output.
class MemoryPatch : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // AutoRoot creates the helper only while its use.root flag is on.
    Q_PROPERTY(bool rootAvailable READ rootAvailable CONSTANT)
    // The addon is in /var/addons (re-read after a run). Not the same as patched: after a boot it patches
    // authman about 4 minutes in.
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    // The last run's result in words; without root, from the start, what to tell the user about it.
    Q_PROPERTY(QString headline READ headline NOTIFY changed)

public:
    explicit MemoryPatch(QObject *parent = 0);

    bool busy() const { return m_busy; }
    bool rootAvailable() const { return !m_helper.isEmpty(); }
    bool installed() const { return m_installed; }
    QString headline() const { return m_headline; }

    // Install the addon from app/native/headless-mem.zip, then end tengramd.
    Q_INVOKABLE void install();
    // After an install: ends the engine and this UI, so the next launch gets the new limit.
    Q_INVOKABLE void closeAll();

signals:
    void changed();
    // A run ended: ok if its output had the success marker; false also when it could not start.
    void finished(bool ok);

private slots:
    void processFinished(int exitCode, QProcess::ExitStatus status);
    void processError(QProcess::ProcessError error);

private:
    void runRoot(const QString &commands, const QString &zip);
    void fail(const QString &headline);

    QString m_helper;
    QString m_headline;
    bool m_installed;
    bool m_busy;
    QProcess m_process;
};

#endif /* MEMORYPATCH_H */
