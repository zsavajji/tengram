#include "StickerPlayer.h"

#include "Settings.h"
#include "Sticker.h"

#include <bb/ImageData>
#include <bb/PixelFormat>
#include <bb/cascades/Application>
#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <rlottie_capi.h>
#include <string.h>

using Sticker::kStickerPx;

// The thread's side: holds the loaded animation and draws a frame at each tick of its timer. Everything but the signals
// and the slots runs on the worker's thread.
class StickerWorker : public QObject
{
    Q_OBJECT
public:
    explicit StickerWorker(QAtomicInt *inFlight)
        : m_inFlight(inFlight), m_animation(0), m_token(0), m_frame(0), m_frames(0), m_step(1), m_loop(false),
          m_buffer(kStickerPx * kStickerPx)
    {
        // A child, so it moves to the thread with the worker.
        m_timer = new QTimer(this);
        m_timer->setInterval(33); // 30 fps
        connect(m_timer, SIGNAL(timeout()), this, SLOT(tick()));
    }

    ~StickerWorker()
    {
        if (m_animation)
            lottie_animation_destroy(m_animation);
    }

public slots:
    // Plays the TGS at path from its first frame. The animation of the last path stays loaded, so a tap that plays the same
    // sticker again parses nothing.
    void start(int token, const QString &path, bool loop)
    {
        m_timer->stop();
        m_token = token;
        if (!m_animation || path != m_path) {
            if (m_animation)
                lottie_animation_destroy(m_animation);
            m_animation = 0;
            m_path.clear();
            const QByteArray json = Sticker::tgsJson(path);
            if (!json.isEmpty())
                m_animation = lottie_animation_from_data(json.constData(), path.toUtf8().constData(), "");
            m_frames = m_animation ? int(lottie_animation_get_totalframe(m_animation)) : 0;
            if (m_frames <= 0) {
                if (m_animation)
                    lottie_animation_destroy(m_animation);
                m_animation = 0;
                emit finished(token);
                return;
            }
            m_path = path;
            // A 60 fps sticker draws every other frame.
            m_step = qMax(1, qRound(lottie_animation_get_framerate(m_animation) / 30.0));
        }
        m_loop = loop;
        m_frame = 0;
        m_timer->start();
    }

    // Stops drawing; the animation stays loaded.
    void halt() { m_timer->stop(); }

signals:
    // A frame as RGBA bytes, premultiplied, kStickerPx square; token is the start()'s.
    void frameReady(int token, const QByteArray &bytes);
    // A failed load, or the last frame of a sticker that plays once.
    void finished(int token);

private slots:
    void tick()
    {
        // The UI hasn't taken the last frame yet: skip drawing, but go on, or the animation would slow down with it.
        if (!int(*m_inFlight)) {
            lottie_animation_render(m_animation, m_frame, m_buffer.data(), kStickerPx, kStickerPx, kStickerPx * 4);
            // rlottie's pixels are 0xAARRGGBB; ImageData's RGBA_Premultiplied wants the bytes R, G, B, A.
            QByteArray bytes(kStickerPx * kStickerPx * 4, 0);
            unsigned char *out = reinterpret_cast<unsigned char *>(bytes.data());
            for (int i = 0; i < m_buffer.size(); ++i) {
                const uint32_t p = m_buffer.at(i);
                out[4 * i] = (p >> 16) & 0xff;
                out[4 * i + 1] = (p >> 8) & 0xff;
                out[4 * i + 2] = p & 0xff;
                out[4 * i + 3] = p >> 24;
            }
            m_inFlight->fetchAndStoreOrdered(1);
            emit frameReady(m_token, bytes);
        }
        m_frame += m_step;
        if (m_frame >= m_frames) {
            if (m_loop) {
                m_frame = 0;
            } else {
                m_timer->stop();
                emit finished(m_token);
            }
        }
    }

private:
    QAtomicInt *m_inFlight; // the player's
    Lottie_Animation *m_animation;
    QString m_path; // of m_animation
    int m_token;
    int m_frame;
    int m_frames;
    int m_step;
    bool m_loop;
    QVector<uint32_t> m_buffer; // reused by every frame
    QTimer *m_timer;
};

StickerPlayer *StickerPlayer::instance()
{
    static StickerPlayer *player = 0;
    if (!player)
        player = new StickerPlayer(QCoreApplication::instance());
    return player;
}

StickerPlayer::StickerPlayer(QObject *parent)
    : QObject(parent), m_thread(new QThread(this)), m_worker(new StickerWorker(&m_inFlight)), m_token(0), m_inFlight(0)
{
    m_worker->moveToThread(m_thread);
    // The worker dies on its own thread, when the thread ends (~StickerPlayer).
    connect(m_thread, SIGNAL(finished()), m_worker, SLOT(deleteLater()));
    connect(m_worker, SIGNAL(frameReady(int, QByteArray)), this, SLOT(onFrame(int, QByteArray)));
    connect(m_worker, SIGNAL(finished(int)), this, SLOT(onFinished(int)));
    m_thread->start();
    // Nothing plays behind the thumbnail or with the screen off.
    bb::cascades::Application *app = bb::cascades::Application::instance();
    connect(app, SIGNAL(thumbnail()), this, SLOT(stop()));
    connect(app, SIGNAL(invisible()), this, SLOT(stop()));
}

StickerPlayer::~StickerPlayer()
{
    m_thread->quit();
    m_thread->wait();
}

void StickerPlayer::play(bb::cascades::ImageView *view, const bb::cascades::Image &still, const QString &path, bool loop)
{
    if (m_view && m_view != view)
        stop();
    m_view = view;
    m_still = still;
    ++m_token;
    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection, Q_ARG(int, m_token), Q_ARG(QString, path),
                              Q_ARG(bool, loop));
}

void StickerPlayer::release(bb::cascades::ImageView *view)
{
    if (m_view && m_view == view)
        stop();
}

bool StickerPlayer::playing(bb::cascades::ImageView *view) const
{
    return view && m_view == view;
}

void StickerPlayer::stop()
{
    ++m_token; // what the worker has queued is stale now
    QMetaObject::invokeMethod(m_worker, "halt", Qt::QueuedConnection);
    if (m_view)
        m_view->setImage(m_still);
    m_view = 0;
}

void StickerPlayer::onFrame(int token, const QByteArray &bytes)
{
    m_inFlight.fetchAndStoreOrdered(0); // taken, drawn or not: the worker may send the next
    if (token != m_token)
        return;
    // The view went with its card, or Off was chosen while it played.
    if (!m_view || Settings::stickerPlayMode() == 2) {
        stop();
        return;
    }
    bb::ImageData image(bb::PixelFormat::RGBA_Premultiplied, kStickerPx, kStickerPx);
    unsigned char *pixels = image.pixels();
    const int stride = image.bytesPerLine();
    for (int y = 0; y < kStickerPx; ++y)
        memcpy(pixels + y * stride, bytes.constData() + y * kStickerPx * 4, kStickerPx * 4);
    m_view->setImage(bb::cascades::Image(image));
}

void StickerPlayer::onFinished(int token)
{
    // Back to the still: the card puts it back anyway at the row's next update, and a jump then would look like a glitch.
    // A sticker's last frame is usually its first.
    if (token == m_token && m_view) {
        m_view->setImage(m_still);
        m_view = 0;
    }
}

#include "StickerPlayer.moc"
