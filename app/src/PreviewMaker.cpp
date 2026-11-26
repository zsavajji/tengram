#include "PreviewMaker.h"

#include "MediaData.h"
#include "Settings.h"
#include "Sticker.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QtConcurrentRun>

// At most this many files are made at once: the phone has two cores, and a photo decodes to a few MB.
static const int kJobs = 2;
// How long the files made are collected before ready() names them.
static const int kBatchMs = 100;

// The whole job, on a worker thread: QImage, QImageReader, QFile and QDir only, nothing of Cascades. The file is written
// beside target and renamed into place: a row read while the worker is still writing takes a file that exists for a
// finished one, and hands the half of a JPEG to Cascades.
static bool make(int job, const QString &source, const QString &target)
{
    // RoundPicture: a sender's picture cut to a disc for the chat's cards (Settings.cpp's roundImage, as roundPhoto makes it).
    const QImage image = job == PreviewMaker::PhotoPreview   ? MediaData::previewImage(source)
                         : job == PreviewMaker::RoundPicture ? roundImage(source, 160)
                                                             : Sticker::still(source, Sticker::kStickerPx);
    if (image.isNull() || !QDir().mkpath(QFileInfo(target).path()))
        return false;
    const QString part = target + ".part";
    const bool saved = job == PreviewMaker::PhotoPreview ? image.save(part, "JPEG", 80) : image.save(part, "PNG");
    if (!saved || !QFile::rename(part, target)) {
        QFile::remove(part); // no half-written file left to be taken for a good one
        return false;
    }
    return true;
}

PreviewMaker *PreviewMaker::instance()
{
    static PreviewMaker *maker = 0;
    if (!maker)
        maker = new PreviewMaker;
    return maker;
}

PreviewMaker::PreviewMaker()
    : QObject(QCoreApplication::instance())
    , m_running(0)
{
    m_flush.setSingleShot(true);
    m_flush.setInterval(kBatchMs);
    connect(&m_flush, SIGNAL(timeout()), this, SLOT(flush()));
}

void PreviewMaker::want(const QString &id, Job job, const QString &source, const QString &target)
{
    const bool queued = m_known.contains(target);
    m_known[target].insert(id);
    if (queued)
        return;
    const Want w = { id, job, source, target };
    m_queue.append(w);
    pump();
}

void PreviewMaker::pump()
{
    while (m_running < kJobs && !m_queue.isEmpty()) {
        const Want w = m_queue.takeFirst();
        QFutureWatcher<bool> *watcher = new QFutureWatcher<bool>(this);
        watcher->setProperty("target", w.target);
        connect(watcher, SIGNAL(finished()), this, SLOT(onMade()));
        watcher->setFuture(QtConcurrent::run(&make, w.job, w.source, w.target));
        ++m_running;
    }
}

void PreviewMaker::onMade()
{
    QFutureWatcher<bool> *watcher = static_cast<QFutureWatcher<bool> *>(sender());
    watcher->deleteLater();
    --m_running;
    const QString target = watcher->property("target").toString();
    if (watcher->result()) {
        m_done += m_known.take(target);
        if (!m_flush.isActive())
            m_flush.start();
    } else {
        // Stays in m_known: not tried again until the app restarts. Its rows are read again all the same: a sticker's
        // spinner gives way to its word (failed()).
        qDebug() << "preview: not made:" << target;
        m_failed.insert(target);
        m_done += m_known.value(target);
        if (!m_flush.isActive())
            m_flush.start();
    }
    pump();
}

void PreviewMaker::flush()
{
    const QStringList ids = m_done.toList();
    m_done.clear();
    emit ready(ids);
}
