#include "MemoryPatch.h"

#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QStringList>

#include <fcntl.h>
#include <unistd.h>

static const char *kResultFile = "/accounts/1000/shared/documents/tengram-memory-patch.txt";
static const char *kAddonStart = "/var/addons/headless-mem/init/start.sh";
static const char *kMarker = "Installation completed successfully.";

MemoryPatch::MemoryPatch(QObject *parent)
    : QObject(parent), m_installed(QFile::exists(kAddonStart)), m_busy(false)
{
    // AutoRoot v4 ships __root (setuid and setgid root); older setups have only u_root.
    if (QFile::exists("/base/bin/__root"))
        m_helper = "/base/bin/__root";
    else if (QFile::exists("/base/bin/u_root"))
        m_helper = "/base/bin/u_root";
    else
        m_headline = tr("Root is not available: Tengram needs AutoRoot v4 with root enabled (/var/rootdata/use.root).");

    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, SIGNAL(finished(int,QProcess::ExitStatus)),
            this, SLOT(processFinished(int,QProcess::ExitStatus)));
    connect(&m_process, SIGNAL(error(QProcess::ProcessError)),
            this, SLOT(processError(QProcess::ProcessError)));
}

void MemoryPatch::fail(const QString &headline)
{
    m_headline = headline;
    emit changed();
    emit finished(false);
}

void MemoryPatch::install()
{
    if (m_busy)
        return;
    if (m_helper.isEmpty()) {
        fail(m_headline); // the constructor's no-root text, still there: nothing clears it before a run
        return;
    }
    // The bar's app/native/ is the folder the app runs from.
    const QString zip = QDir::currentPath() + "/app/native/headless-mem.zip";
    const QString patcher = QDir::currentPath() + "/app/native/tg-install-patcher";
    if (!QFile::exists(zip)) {
        fail(tr("The memory addon (headless-mem.zip) is missing from this app."));
        return;
    }
    if (!QFile::exists(patcher)) {
        fail(tr("The helper tg-install-patcher is missing from this app."));
        return;
    }

    /* Only what the commands need. The app's own environment (a relative LD_LIBRARY_PATH, the
     * sandbox HOME and TMPDIR) must not leak into a root shell, nor into the processes the
     * installer starts from it. The zip path and the commands reach the root shell only in
     * environment variables, never inside its command text, so no quoting is needed. */
    QProcessEnvironment env;
    env.insert("PATH", "/proc/boot:/bin:/usr/bin:/sbin:/usr/sbin:/base/bin");
    env.insert("HOME", "/root");
    env.insert("TG_ZIP", zip);
    /* Install first (its start.sh patches authman at once), then end the running engine: the UI's usual
     * START brings it back, and the launcher gives it the new limit. */
    /* The installer launches the addon's start.sh in the background and returns before it has patched authman
     * (its own log says so): the engine is ended only once a new [HMEM] line is in installer.log, or after
     * 20 s, so the START that follows finds the larger limit. */
    env.insert("TG_CMD",
               "L=/var/log/installer.log; n0=$(grep -c HMEM $L 2>/dev/null); "
               "/var/rootdata/installer \"$TG_ZIP\" </dev/null; "
               "i=0; while [ $i -lt 20 ] && [ \"$(grep -c HMEM $L 2>/dev/null)\" = \"${n0:-0}\" ]; do sleep 1; i=$((i+1)); done; "
               "grep HMEM $L 2>/dev/null; "
               "slay -f tengramd 2>/dev/null; true");
    /* On BB10 the system ends every process carrying an app's id when the app exits, and children
     * inherit it, so the commands are started through tg-install-patcher, which gives them a new id
     * and session. Their output goes to a file, not the pipe to this app. The root shell only waits
     * for the .done file and prints it; if the app closes midway, the commands still finish. */
    env.insert("TG_PATCHER", patcher);
    m_process.setProcessEnvironment(env);

    m_headline.clear();
    /* Busy before start(): a start that fails at once reports through processError() inside
     * start() itself, which must find the run in progress to end it. */
    m_busy = true;
    emit changed();

    /* Cascades leaves the app's navigator, GPU, screen, input and PPS connections open across
     * exec. Without this the root shell inherits them, and through the installer so do the
     * processes it starts; when the app exits, the system ends every process holding them.
     * QProcess makes its own pipes after this, so they are not affected. */
    for (long fd = 3, max = sysconf(_SC_OPEN_MAX); fd < max; ++fd)
        fcntl((int)fd, F_SETFD, FD_CLOEXEC);

    m_process.start(m_helper, QStringList());
    if (!m_busy)
        return;
    /* QProcess keeps what is written until the helper is running; closing the channel gives the
     * root shell its EOF once the commands are through, so it exits after them. */
    m_process.write(
        "cd /\n"
        "export TG_OUT=/var/tmp/tg-patch.$$\n"
        "H=/var/tmp/tg-install-patcher.$$\n"
        "cp \"$TG_PATCHER\" \"$H\" && chmod 755 \"$H\" && /proc/boot/pathtrust \"!$H\" >/dev/null 2>&1\n"
        "if ! \"$H\" sh -c 'sh -c \"$TG_CMD\" </dev/null >\"$TG_OUT\" 2>&1; "
        "mv \"$TG_OUT\" \"$TG_OUT.done\"' </dev/null >/dev/null 2>&1; then\n"
        "echo \"Could not start the commands as a new application (tg-install-patcher).\"\n"
        "rm -f \"$H\"\n"
        "exit\n"
        "fi\n"
        "rm -f \"$H\"\n"
        "while [ ! -f \"$TG_OUT.done\" ]; do sleep 1; done\n"
        "cat \"$TG_OUT.done\"\n"
        "rm -f \"$TG_OUT.done\"\n");
    m_process.closeWriteChannel();
}

void MemoryPatch::processFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(status);
    if (!m_busy)
        return;

    /* The helper always exits 0, so only the output tells success from failure. */
    const QByteArray data = m_process.readAll();
    const QString text = QString::fromUtf8(data);
    const bool ok = text.contains(kMarker);

    // The whole output, for the user to read in Documents (the app has access_shared).
    QFile file(kResultFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(data);
    file.close();

    if (text.trimmed().isEmpty())
        m_headline = tr("The root helper gave no output. Is root enabled in AutoRoot (/var/rootdata/use.root)?");
    else if (ok)
        m_headline = tr("Done. Tengram's engine restarts with the larger memory limit.");
    else
        m_headline = tr("The install did not finish. The output is in Documents, tengram-memory-patch.txt.");

    m_installed = QFile::exists(kAddonStart);
    m_busy = false;
    emit changed();
    emit finished(ok);
}

/* Only a failed start needs handling here: after any other error finished() still follows. */
void MemoryPatch::processError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart || !m_busy)
        return;
    m_busy = false;
    fail(tr("Could not start the root helper %1.").arg(m_helper));
}
