#include "MediaOpener.h"

#include "EngineLink.h"
#include "sizes.h"

#include <bb/cascades/Image>
#include <bb/data/JsonDataAccess>
#include <bb/system/InvokeReplyError>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTargetReply>
#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPolygonF>
#include <QUrl>

using namespace bb::system;

MediaOpener::MediaOpener(QObject *parent)
    : QObject(parent)
    , m_progress(0)
    , m_saving(false)
    , m_editing(false)
    , m_region(bb::system::LocaleType::Region)
{
    connect(&m_invoker, SIGNAL(childCardDone(bb::system::CardDoneMessage)), this,
            SLOT(onCardDone(bb::system::CardDoneMessage)));
}

QObject *MediaOpener::link() const
{
    return m_engine;
}

// A QPointer, as ContactInfo's: at app exit the page may outlive the EngineLink.
void MediaOpener::setLink(QObject *link)
{
    m_engine = qobject_cast<EngineLink *>(link);
    if (m_engine) {
        connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
                SLOT(onReplied(QString, QString, QVariant)));
        connect(m_engine, SIGNAL(failureChanged()), this, SLOT(onFailure()));
        connect(m_engine, SIGNAL(mediaProgress(QString, QString, qint64, qint64)), this,
                SLOT(onProgress(QString, QString, qint64, qint64)));
    }
}

QString MediaOpener::loadingId() const
{
    return m_loadingId;
}

qreal MediaOpener::progress() const
{
    return m_progress;
}

QString MediaOpener::progressText() const
{
    return m_progressText;
}

void MediaOpener::setLoadingId(const QString &id)
{
    if (id == m_loadingId)
        return;
    m_loadingId = id;
    m_progress = 0;
    m_progressText.clear();
    emit loadingIdChanged();
    emit progressChanged();
}

void MediaOpener::get(const QString &chat, const QString &id, const QString &kind, bool save)
{
    if (!m_engine)
        return;
    QVariantMap args;
    args["chat"] = chat;
    args["id"] = id;
    if (save)
        args["save"] = true;
    m_saving = save;
    m_kind = kind;
    m_chat = chat;
    // "" when the engine is down: request() has set the failure, and nothing is loading.
    m_reqId = m_engine->request("media.get", args);
    setLoadingId(m_reqId.isEmpty() ? QString() : id);
}

bool MediaOpener::saving() const
{
    return m_saving;
}

// The engine's media.progress. Until the first one comes the bubble keeps its spinner.
void MediaOpener::onProgress(const QString &chat, const QString &id, qint64 done, qint64 size)
{
    if (id != m_loadingId || chat != m_chat || done <= 0 || size <= 0)
        return;
    m_progress = qMin<qreal>(1, qreal(done) / size);
    m_progressText = sizeText(done, m_region.locale()) + " / " + sizeText(size, m_region.locale());
    emit progressChanged();
}

// The engine answers the get with a "cancelled" failure, which onFailure skips.
void MediaOpener::cancel()
{
    if (!m_engine || m_loadingId.isEmpty())
        return;
    QVariantMap args;
    args["chat"] = m_chat;
    args["id"] = m_loadingId;
    m_engine->requestQuiet("media.cancel", args);
    m_reqId.clear();
    setLoadingId(QString());
}

void MediaOpener::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    if (cmd != "media.get" || id != m_reqId)
        return;
    const QString msgId = m_loadingId;
    m_reqId.clear();
    setLoadingId(QString());
    if (m_saving)
        emit saved(result.toMap()["path"].toString());
    else
        emit ready(msgId, m_kind, result.toMap()["path"].toString());
}

// ponytail: a failure names no request, so an older one's, after a newer get, ends the newer
// one's spinner too; the page's toast shows either way.
void MediaOpener::onFailure()
{
    if (m_loadingId.isEmpty() || !m_engine
        || m_engine->failure().value("cmd").toString() != "media.get"
        || m_engine->failure().value("code").toString() == "cancelled") // cancel()'s own: a newer get may be loading
        return;
    m_reqId.clear();
    setLoadingId(QString());
}

// The file is in Downloads, not our sandbox, because another app can't read the sandbox
// (phase6.md, choice 1). No target: the system picks the app by the file's type.
bool MediaOpener::openFile(const QString &path)
{
    InvokeRequest request;
    request.setAction("bb.action.OPEN");
    request.setUri(QUrl::fromLocalFile(path));
    InvokeTargetReply *reply = m_invoker.invoke(request);
    if (!reply) {
        qWarning() << "media: no reply from the system to OPEN" << path;
        return false;
    }
    connect(reply, SIGNAL(finished()), this, SLOT(onInvoked()));
    return true;
}

// The picture editor's manifest (read on the phone, 10.3.3) takes bb.action.EDIT for image/jpeg, png, gif and bmp
// with file:// uris, as a card. What it returns is logged by onCardDone.
void MediaOpener::editPicture(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    InvokeRequest request;
    request.setTarget("sys.pictureeditor.cardeditor");
    request.setAction("bb.action.EDIT");
    request.setMimeType(ext == "jpg" || ext == "jpeg" ? "image/jpeg" : "image/" + ext);
    request.setUri(QUrl::fromLocalFile(path));
    InvokeTargetReply *reply = m_invoker.invoke(request);
    if (!reply) {
        qWarning() << "media: no reply from the system to EDIT" << path;
        emit openFailed();
        return;
    }
    m_editing = true;
    connect(reply, SIGNAL(finished()), this, SLOT(onInvoked()));
}

// The Contacts app's manifest (read on the Q10) takes bb.action.OPEN with text/vcard as data in the
// request, and answers with its import screen: the name and every number. Replies like openFile's.
// ponytail: with several contacts it may import only the first; a list to pick one if it shows.
void MediaOpener::importContact(const QString &vcard)
{
    if (vcard.isEmpty())
        return;
    InvokeRequest request;
    request.setTarget("sys.pim.contacts.app");
    request.setAction("bb.action.OPEN");
    request.setMimeType("text/vcard");
    request.setData(vcard.toUtf8());
    InvokeTargetReply *reply = m_invoker.invoke(request);
    if (!reply) {
        qWarning() << "media: no reply from the system to the contact import";
        return;
    }
    connect(reply, SIGNAL(finished()), this, SLOT(onInvoked()));
}

void MediaOpener::onInvoked()
{
    InvokeTargetReply *reply = qobject_cast<InvokeTargetReply *>(sender());
    if (!reply)
        return;
    if (reply->error() != InvokeReplyError::None) {
        qWarning() << "media: OPEN failed, error" << reply->error();
        emit openFailed(); // no app for the type, or no access to Downloads
    }
    reply->deleteLater();
}

// Seen on the phone (2026-11-17): a copy saved comes back as reason "SaveDone" with its path; the original saved over
// as reason "save" with [{"uri": "file://…"}], the same file now edited. Success is any reason but "cancel" or "closed"
// with data that names an existing file. Every answer is logged.
void MediaOpener::onCardDone(const CardDoneMessage &done)
{
    qDebug() << "media: card done, reason" << done.reason() << "dataType" << done.dataType() << "data" << done.data();
    if (!m_editing)
        return;
    m_editing = false;
    const QString reason = done.reason().toLower();
    if (done.data().isEmpty() || reason == "cancel" || reason == "closed")
        return;
    QString path = done.data();
    if (path.startsWith("[")) {
        bb::data::JsonDataAccess json;
        const QVariantList list = json.loadFromBuffer(path.toUtf8()).toList();
        path = list.isEmpty() ? QString() : list.first().toMap().value("uri").toString();
    }
    if (path.startsWith("file://"))
        path = QUrl::fromPercentEncoding(path.mid(7).toUtf8());
    if (QFileInfo(path).isFile())
        emit pictureEdited(path);
    else
        qWarning() << "media: the editor returned no file" << done.data();
}

// A received photo lives in the sandbox (phase6.md, choice 1), where neither Pictures nor File
// Manager looks: Save puts a copy where both do. A copy already there is the answer.
QString MediaOpener::saveToPictures(const QString &path)
{
    const QString dir = "/accounts/1000/shared/photos/Tengram";
    if (!QDir().mkpath(dir))
        return QString();
    const QString target = dir + "/" + QFileInfo(path).fileName();
    if (QFile::exists(target) || QFile::copy(path, target))
        return target;
    return QString();
}

// ponytail: EXIF orientation isn't applied (Qt 4.8's reader ignores it): a portrait camera photo's
// thumbnail may lie on its side. Decoded on the UI thread, at reduced size.
QVariantMap MediaOpener::photoInfo(const QString &path)
{
    QVariantMap info;
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext != "jpg" && ext != "jpeg")
        return info;
    QImageReader reader(path);
    const QSize size = reader.size(); // from the header
    if (!size.isValid())
        return info;
    QSize to = size;
    if (to.width() > 96 || to.height() > 96)
        to.scale(96, 96, Qt::KeepAspectRatio);
    reader.setScaledSize(to);
    const QImage image = reader.read();
    if (image.isNull())
        return info;
    info["width"] = size.width();
    info["height"] = size.height();
    // The engine takes at most 16 KB; a busy picture may need a lower quality.
    for (int quality = 60; quality >= 40; quality -= 20) {
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        if (image.save(&buffer, "JPEG", quality) && jpeg.size() <= 16 * 1024) {
            info["thumb"] = QString::fromLatin1(jpeg.toBase64().constData());
            break;
        }
    }
    return info;
}

QString MediaOpener::asJpeg(const QString &path)
{
    const QFileInfo file(path);
    const QString ext = file.suffix().toLower();
    if (ext == "jpg" || ext == "jpeg")
        return path;
    if (ext != "png")
        return QString();
    QImageReader reader(path);
    QSize to = reader.size(); // from the header
    if (!to.isValid())
        return QString();
    // WhatsApp shrinks a picture to about 1600 px anyway, and a big PNG decoded whole is tens of MB.
    if (qMax(to.width(), to.height()) > 2048) {
        to.scale(2048, 2048, Qt::KeepAspectRatio);
        reader.setScaledSize(to);
    }
    const QImage image = reader.read();
    if (image.isNull())
        return QString();
    // A PNG can be transparent, a JPEG can't be: it goes on black (the user's choice, 2026-11-13).
    QImage flat(image.size(), QImage::Format_RGB32);
    flat.fill(0xFF000000);
    QPainter painter(&flat);
    painter.drawImage(0, 0, image);
    painter.end();
    const QString dir = QDir::homePath() + "/cache/convert/";
    const QString out = dir + file.completeBaseName() + ".jpg";
    if (!QDir().mkpath(dir) || !flat.save(out, "JPEG", 90)) {
        QFile::remove(out);
        qWarning() << "media: could not convert" << path;
        return QString();
    }
    return out;
}

QString MediaOpener::composePreview(const QString &path)
{
    QImageReader reader(path);
    QSize to = reader.size(); // from the header
    if (!to.isValid())
        return QString();
    if (to.width() > 720 || to.height() > 720) {
        to.scale(720, 720, Qt::KeepAspectRatio);
        reader.setScaledSize(to);
    }
    const QImage image = reader.read();
    if (image.isNull())
        return QString();
    const QString dir = QDir::homePath() + "/cache/previews/";
    const QString out = dir + "compose-" + QString::number(qHash(path), 16) + ".jpg";
    if (!QDir().mkpath(dir) || !image.save(out, "JPEG", 90)) {
        QFile::remove(out);
        return QString();
    }
    return out;
}

QString MediaOpener::videoPoster()
{
    // The mark of the call and chat play discs (images/play.png): a white disc, a dark triangle, on grey.
    QImage poster(96, 96, QImage::Format_RGB32);
    poster.fill(0xFF2B2B2B);
    QPainter painter(&poster);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255));
    painter.drawEllipse(QRectF(24, 24, 48, 48));
    painter.setBrush(QColor(0x2B, 0x2B, 0x2B));
    QPolygonF triangle;
    triangle << QPointF(41, 35) << QPointF(41, 61) << QPointF(63, 48);
    painter.drawPolygon(triangle);
    painter.end();
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    if (!poster.save(&buffer, "JPEG", 70) || jpeg.size() > 16 * 1024)
        return QString();
    return QString::fromLatin1(jpeg.toBase64().constData());
}

// The thumbnail is photoInfo's, decoded back from its base64: one place makes it.
QVariantMap MediaOpener::fileInfo(const QString &path)
{
    QVariantMap info;
    const QFileInfo file(path);
    if (!file.isFile())
        return info;
    info["name"] = file.fileName();
    info["sizeText"] = sizeText(file.size(), m_region.locale());
    const QString ext = file.suffix().toLower();
    info["picture"] = ext == "jpg" || ext == "jpeg" || ext == "mp4" || ext == "3gp" || ext == "mov";
    const QByteArray thumb = QByteArray::fromBase64(photoInfo(path)["thumb"].toString().toLatin1());
    if (!thumb.isEmpty())
        info["image"] = QVariant::fromValue(bb::cascades::Image(thumb));
    return info;
}
