#include <cstring>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <bb/platform/NotificationDefaultApplicationSettings>
#include <bb/system/LocaleHandler>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextCodec>
#include <QTimer>
#include <QTranslator>
#include <QtDeclarative>
#include <cstdio>
#include <devctl.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/procfs.h>
#include <sys/slog2.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "CallAudio.h"
#include "CallLog.h"
#include "CallVideo.h"
#include "ChatController.h"
#include "ChatRows.h"
#include "ChatSearch.h"
#include <bb/ApplicationInfo>
#include <bb/device/DisplayInfo>
#include <QDeclarativeContext>
#include "ChatListModel.h"
#include "ChatsDb.h"
#include "ContactBook.h"
#include "ContactInfo.h"
#include "ContactPicker.h"
#include "EmojiModel.h"
#include "EngineLink.h"
#include "Invites.h"
#include "MapChooser.h"
#include "MediaOpener.h"
#include "MemoryPatch.h"
#include "MessageCards.h"
#include "OpenRequests.h"
#include "Privacy.h"
#include "Profile.h"
#include "Proximity.h"
#include "QrImage.h"
#include "Sessions.h"
#include "Settings.h"
#include "StickerGrid.h"
#include "Storage.h"
#include "TopicModel.h"

using namespace bb::cascades;

// qDebug and qWarning, QML's errors included, go nowhere on the phone by themselves: they go to
// the system log, where `slog2info -b tengram` shows them, and to data/logs/ui.log. The file
// outlives the process: the system recycles a closed app's log within minutes, and a freeze on
// 0.0.1.132 left nothing to read (Q10, 2026-11-02).
static slog2_buffer_t logBuffer;
static bool toSlog = false;
// A start whose scene is set later than this (ms) is slow, and the address book's first read then waits SlowBookWaitMs.
// Through the launcher the scene is set at 1.3 to 1.5 s (Classic, 1.0.2.129, two starts).
static const int SlowStartMs = 2000;
static const int SlowBookWaitMs = 15000;
static FILE *logFile = 0;

static void writeLog(QtMsgType type, const char *msg)
{
    // Cascades logs "Invalid scaling method." at every ImageView::setScalingMethod(ScalingMethod::Fill), a valid value
    // that it applies (RoundFill.qml's pill is stretched by it): 331 lines in 12 chat opens on the Classic (1.0.0.94),
    // each a write and a flush here. Dropped.
    if (strncmp(msg, "Invalid scaling method", 22) == 0)
        return;
    if (toSlog)
        slog2c(logBuffer, 0, type == QtDebugMsg ? SLOG2_INFO : SLOG2_WARNING, msg);
    if (!logFile)
        return;
    timeval now;
    gettimeofday(&now, 0);
    tm local;
    localtime_r(&now.tv_sec, &local);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%b %d %H:%M:%S", &local); // slog2info's form, phone time
    fprintf(logFile, "%s.%03d %s\n", stamp, int(now.tv_usec / 1000), msg);
    fflush(logFile); // with the OS at once: a freeze or a kill keeps every line before it
}

static void startLog()
{
    slog2_buffer_set_config_t config = {};
    config.num_buffers = 1;
    config.buffer_set_name = "tengram";
    config.verbosity_level = SLOG2_INFO;
    config.buffer_config[0].buffer_name = "ui";
    config.buffer_config[0].num_pages = 4;
    toSlog = slog2_register(&config, &logBuffer, 0) == 0;
    const QString path = QDir::homePath() + "/logs/ui.log";
    QDir().mkpath(QFileInfo(path).path());
    // ponytail: rotated at start only, so one long run can pass 256 KB; the UI writes little.
    if (QFileInfo(path).size() > 256 * 1024) {
        QFile::remove(path + ".1");
        QFile::rename(path, path + ".1");
    }
    logFile = fopen(QFile::encodeName(path).constData(), "a");
    qInstallMsgHandler(writeLog);
    qDebug("UI started, pid %d", int(getpid()));
}

// Milliseconds since the system started this process, -1 when it won't say. With "ui: main" and "ui: scene
// set" it times a cold start. As a plain executable main came 1.5 s in and the application was made by 3.4 s, the
// system's share (103 libraries, 97 of them Cascades' own, then the Application's own file lookups), and the scene
// was set at 4.35 s with 500 chats (Classic, 1.0.2.128). Forked from the launcher's /bin/cascades, which holds all
// that already: 0.14 s, 0.29 s and 1.47 s (1.0.2.129, the engine up).
static int sinceLaunch()
{
    procfs_info info;
    const int fd = open("/proc/self/as", O_RDONLY);
    if (fd < 0)
        return -1;
    const int rc = devctl(fd, DCMD_PROC_INFO, &info, sizeof info, 0);
    close(fd);
    if (rc != EOK)
        return -1;
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return int((qint64(now.tv_sec) * 1000000000LL + now.tv_nsec - qint64(info.start_time)) / 1000000);
}

// A QML error otherwise quits the app at start with nothing to show: write it where the user
// can find it (File Manager > misc, or the app's data folder).
static bool loaded(QmlDocument *document, const char *name)
{
    if (!document->hasErrors())
        return true;
    QFileInfo misc("/accounts/1000/shared/misc");
    QFile log((misc.isDir() && misc.isWritable() ? misc.filePath() : QDir::homePath())
              + "/tengram-qml-errors.log");
    if (log.open(QIODevice::WriteOnly | QIODevice::Append)) {
        foreach (const QDeclarativeError &error, document->errors())
            log.write(QString("%1: %2\n").arg(name, error.toString()).toUtf8());
    }
    return false;
}

Q_DECL_EXPORT int main(int argc, char **argv)
{
    const int atMain = sinceLaunch();
    Application app(argc, argv);
    startLog();
    qDebug("ui: main %d ms after launch, the application made at %d ms", atMain, sinceLaunch());
    // The sources are UTF-8; Qt 4 reads tr() and C-string literals as Latin-1 unless told, and
    // "typing…" showed as "typingà|" (Q10, 2026-10-31).
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
    // The phone's language, once at start (no live switching): the .qm is in the bar's assets, and
    // English, the source text, has none. Before the QML loads, whose qsTr reads it then. Not applicationDirPath():
    // that is the launcher's /bin now; the bar's folder is app/native under the working directory.
    QTranslator translator;
    const QString language = bb::system::LocaleHandler(bb::system::LocaleType::Messages).locale().name().section('_', 0, 0);
    if (translator.load("tengram_" + language, QDir::currentPath() + "/app/native/assets/translations"))
        app.installTranslator(&translator);
    EngineLink engine(&app);
    engine.setLanguage(language); // before engine.start(): the hello sends it
    // Nothing else starts the engine (no boot start, user 2026-10-29), so the UI starts it on open, and
    // again whenever the link drops. First of all, before the models and the QML: the engine comes up
    // while they load (it went last, and connected 1.2 s after the scene was set; Q10, 0.1.1.307).
    engine.start();
    // Before every model: its connectedChanged slot runs first, so the models' own find chats.db reopened.
    ChatsDbReconnect chatsDbReconnect(&engine, &app);
    // Before the book, which sends it only while Settings > Sync contacts is on.
    Settings settings(&engine, &app);
    ContactBook book(&engine, &settings, &app);
    ChatListModel chats(&engine, ChatListModel::Chats, &app);
    ChatListModel archivedChats(&engine, ChatListModel::Archived, &app);
    CallAudio callAudio(&engine, &app);
    CallVideo callVideo(&engine, &app);
    // The camera's resolution follows Settings > Data and storage > Video calls.
    callVideo.setQuality(settings.videoQuality());
    QObject::connect(&settings, SIGNAL(videoQualityChanged(int)), &callVideo, SLOT(setQuality(int)));
    Proximity proximity(&app);
    // One for the run: the picker's table is built once, not per chat.
    EmojiModel emojis(&app);
    QrImage qr(&app);
    // With a Hub account (step 5.7) instant previews become possible, but BB10 starts them off,
    // and 5.6's try left the LED off: both on by default, as BBTelega does. apply() leaves alone
    // what the user has changed in Settings -> Notifications (Q10, 0.0.1.100).
    bb::platform::NotificationDefaultApplicationSettings notifyDefaults;
    notifyDefaults.setPreview(bb::platform::NotificationPriorityPolicy::Allow);
    notifyDefaults.setLed(bb::platform::NotificationPolicy::Allow);
    if (notifyDefaults.apply() != bb::platform::NotificationSettingsError::None)
        qWarning() << "notification defaults refused";
    OpenRequests openRequests(engine.invoker(), &app);
    Invites invites(&engine, &app);
    // The headless-mem addon's install (docs/headless-cap.md), offered by main.qml when the engine runs under 30 MB.
    MemoryPatch memPatch(&app);

    // bb.cascades has no Timer: QTimer stands in (Hint.qml, VideoViewer.qml).
    qmlRegisterType<QTimer>("tengram", 1, 0, "Timer");
    qmlRegisterType<CallLog>("tengram", 1, 0, "CallLog");
    qmlRegisterType<ChatController>("tengram", 1, 0, "ChatController");
    qmlRegisterType<ChatRows>("tengram", 1, 0, "ChatRows");
    qmlRegisterType<ChatSearch>("tengram", 1, 0, "ChatSearch");
    qmlRegisterType<ContactInfo>("tengram", 1, 0, "ContactInfo");
    qmlRegisterType<ContactPicker>("tengram", 1, 0, "ContactPicker");
    qmlRegisterType<MapChooser>("tengram", 1, 0, "MapChooser");
    qmlRegisterType<MediaOpener>("tengram", 1, 0, "MediaOpener");
    qmlRegisterType<MessageCards>("tengram", 1, 0, "MessageCards");
    qmlRegisterType<Privacy>("tengram", 1, 0, "Privacy");
    qmlRegisterType<Profile>("tengram", 1, 0, "Profile");
    qmlRegisterType<Sessions>("tengram", 1, 0, "Sessions");
    qmlRegisterType<StickerGrid>("tengram", 1, 0, "StickerGrid");
    qmlRegisterType<Storage>("tengram", 1, 0, "Storage");
    qmlRegisterType<TopicModel>("tengram", 1, 0, "TopicModel");
    engine.awaitLink();
    qDebug("ui: link %s at %d ms", engine.connected() ? "up" : "not up yet", sinceLaunch());
    QmlDocument *document = QmlDocument::create("asset:///main.qml").parent(&app);
    if (!loaded(document, "main.qml"))
        return 1;
    document->setContextProperty("engine", &engine);
    document->setContextProperty("chats", &chats);
    document->setContextProperty("archivedChats", &archivedChats);
    document->setContextProperty("callAudio", &callAudio);
    document->setContextProperty("callVideo", &callVideo);
    // About shows it where a profile shows the number: versionNumber.buildId, from the .bar.
    document->documentContext()->setContextProperty("appVersion", bb::ApplicationInfo().version());
    // The pages' px(): a 720 px stage scaled to the screen's width.
    document->documentContext()->setContextProperty("screenWidth", bb::device::DisplayInfo().pixelSize().width());
    // How far a tall screen (the Z10's 1280) runs above that stage.
    document->documentContext()->setContextProperty("screenHeight", bb::device::DisplayInfo().pixelSize().height());
    document->setContextProperty("proximity", &proximity);
    document->setContextProperty("emojis", &emojis);
    document->setContextProperty("qr", &qr);
    document->setContextProperty("settings", &settings);
    document->setContextProperty("openRequests", &openRequests);
    document->setContextProperty("invites", &invites);
    document->setContextProperty("memPatch", &memPatch);
    // The phone's region and language ("IT", "it"): the linking page's first country, and the
    // language of country names.
    QVariantMap phoneLocale;
    phoneLocale["region"] = bb::system::LocaleHandler(bb::system::LocaleType::Region).locale().name().section('_', 1, 1);
    phoneLocale["language"] = bb::system::LocaleHandler(bb::system::LocaleType::Messages).locale().name().section('_', 0, 0);
    document->documentContext()->setContextProperty("phoneLocale", phoneLocale);
    AbstractPane *root = document->createRootObject<AbstractPane>();
    if (!root)
        return 1;
    app.setScene(root);
    const int sceneAt = sinceLaunch();
    qDebug("ui: scene set at %d ms", sceneAt);
    // The address book's first read makes the phone's contacts service work at a higher priority than the UI thread:
    // on a start that is already slow it stalled the UI over a second (the user, 2026-11-24; Classic, the plain
    // executable's scene at 5.8 to 7.2 s against 4.0 to 4.8). Such a start reads the book once things have settled.
    // ponytail: SlowStartMs is from two starts through the launcher, and no slow one has been seen yet: set it again
    // from ui.log's "scene set" lines if a "stalled" line follows a start it let through.
    const bool slow = sceneAt > SlowStartMs;
    if (slow)
        qDebug("ui: a slow start, the contact book waits %d s", SlowBookWaitMs / 1000);
    book.readAfter(slow ? SlowBookWaitMs : 2000);
    return Application::exec();
}
