#include "StickerPicker.h"

#include "Moves.h"

#include "EngineLink.h"
#include "Sticker.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QUrl>

// At most this many pictures are downloaded or decoded at once: the engine is under a 30 MB cap and the phone has two cores.
static const int kJobs = 4;
// At most this many sets' stickers are asked at once: each answer is a list of up to 120 stickers, and the engine holds it
// only while it answers.
static const int kSetJobs = 4;
// The grid's cells are 128 px (a sticker's thumbnail); the preview shows the sticker's own file at 360, still.
static const int kGridSide = 128;
static const int kPreviewSide = 360;
// A download the engine hasn't answered in this long is given up: requestQuiet tells nothing of a failure, and four that
// failed would take every place.
static const int kStallMs = 20000;

static QVariantList at(int group, int item = -1)
{
    QVariantList path;
    path << group;
    if (item >= 0)
        path << item;
    return path;
}

// The grid's model: the picker's groups, which it reads in place. The signals are protected (Qt 4), so it sends them.
class StickerModel : public bb::cascades::DataModel
{
public:
    explicit StickerModel(StickerPicker *picker) : DataModel(picker), m_picker(picker) {}

    int childCount(const QVariantList &indexPath)
    {
        if (indexPath.isEmpty())
            return m_picker->groupCount();
        if (indexPath.size() == 1)
            return m_picker->m_groups.value(indexPath[0].toInt()).items.size();
        return 0;
    }
    bool hasChildren(const QVariantList &indexPath) { return indexPath.size() < 2 && childCount(indexPath) > 0; }
    QString itemType(const QVariantList &indexPath) { return indexPath.size() == 1 ? "header" : "item"; }
    QVariant data(const QVariantList &indexPath) { return m_picker->rowData(indexPath); }

    void added(const QVariantList &path) { emit itemAdded(path); }
    void removed(const QVariantList &path) { emit itemRemoved(path); }
    void updated(const QVariantList &path) { emit itemUpdated(path); }
    void reset() { emit itemsChanged(bb::cascades::DataModelChangeType::Init); }
    // Moves maps where each held header and cell went, so the grid keeps them: a bare AddRemove made every cell on screen
    // again, and the page blinked each 250 ms while sets came in (the user, 0.1.0.73).
    void changed(Moves *moves)
    {
        emit itemsChanged(bb::cascades::DataModelChangeType::AddRemove,
                          QSharedPointer<bb::cascades::DataModel::IndexMapper>(moves));
    }

private:
    StickerPicker *m_picker;
};

// The one picker, once made.
static StickerPicker *s_picker = 0;

StickerPicker *StickerPicker::shared(EngineLink *engine)
{
    if (!s_picker)
        s_picker = new StickerPicker(engine);
    return s_picker;
}

// The pictures are files in data/cache, which Clear cache deletes: forget them, and the cells and the strip read again and
// ask again. Nothing when the picker was never made.
void StickerPicker::forgetPictures()
{
    if (!s_picker)
        return;
    // m_decoding is zeroed as reset() does: onDecoded returns before its --m_decoding for another gen.
    ++s_picker->m_gen;
    s_picker->m_decoding = 0;
    s_picker->m_images.clear();
    s_picker->m_asked.clear();
    s_picker->m_queue.clear();
    s_picker->m_model->reset();
    emit s_picker->stripChanged();
}

// A QPointer for the engine, as ChatSearch's: at app exit the picker may outlive the EngineLink.
StickerPicker::StickerPicker(EngineLink *engine)
    : QObject(QCoreApplication::instance())
    , m_engine(engine)
    , m_model(new StickerModel(this))
    , m_stripDirty(false)
    , m_wanted(false)
    , m_started(false)
    , m_setsFailed(false)
    , m_loading(false)
    , m_gen(0)
    , m_lost(0)
    , m_next(0)
    , m_decoding(0)
    , m_previewTok(0)
{
    m_sweeper.setInterval(5000);
    connect(&m_sweeper, SIGNAL(timeout()), this, SLOT(sweep()));
    m_flush.setSingleShot(true);
    m_flush.setInterval(250);
    connect(&m_flush, SIGNAL(timeout()), this, SLOT(flush()));
    connect(engine, SIGNAL(replied(QString, QString, QVariant)), this, SLOT(onReplied(QString, QString, QVariant)));
    connect(engine, SIGNAL(failureChanged()), this, SLOT(onFailure()));
    connect(engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
}

bb::cascades::DataModel *StickerPicker::model() const
{
    return m_model;
}

QVariantList StickerPicker::strip() const
{
    QVariantList list;
    foreach (const Group &g, m_groups) {
        QVariantMap row;
        if (!g.cover.pic.isEmpty() && m_images.contains(g.cover.pic)) {
            row["image"] = QVariant::fromValue(m_images.value(g.cover.pic));
            row["key"] = "pic:" + g.cover.pic;
        } else {
            row["image"] = g.icon;
            row["key"] = g.icon;
        }
        list << QVariant(row);
    }
    return list;
}

StickerPicker::Item StickerPicker::parseItem(const QVariantMap &sticker)
{
    Item i;
    i.file = sticker["file"].toString();
    i.emoji = sticker["emoji"].toString();
    i.format = sticker["format"].toString();
    i.pic = sticker["pic"].toString();
    i.picPath = sticker["picPath"].toString();
    return i;
}

QList<StickerPicker::Item> StickerPicker::parseItems(const QVariant &stickers)
{
    QList<Item> list;
    foreach (const QVariant &s, stickers.toList())
        list << parseItem(s.toMap());
    return list;
}

// The same stickers in the same order: a list the engine sends again unchanged leaves the group alone.
bool StickerPicker::sameStickers(const QList<Item> &a, const QList<Item> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i).file != b.at(i).file)
            return false;
    }
    return true;
}

// Everything held is for the engine's earlier run: its file ids are gone, and what a worker is still decoding is dropped
// when it ends (m_gen).
void StickerPicker::reset()
{
    ++m_gen;
    m_groups.clear();
    m_pending.clear();
    m_flush.stop();
    m_stripDirty = false;
    m_faves.clear();
    m_idRecent.clear();
    m_idFaves.clear();
    m_idSets.clear();
    m_setIds.clear();
    m_lost = 0;
    m_faveIds.clear();
    m_sets.clear();
    m_next = 0;
    m_images.clear();
    m_asked.clear();
    m_picPath.clear();
    m_queue.clear();
    m_files.clear();
    m_decoding = 0;
    m_sweeper.stop();
    m_started = false;
    m_setsFailed = false;
    m_model->reset();
    emit stripChanged();
    settle();
}

void StickerPicker::settle()
{
    const bool loading = m_wanted
        && (!m_started || !m_idRecent.isEmpty() || !m_idFaves.isEmpty() || !m_idSets.isEmpty() || !m_setIds.isEmpty()
            || m_next < m_sets.size() || !m_pending.isEmpty());
    if (loading != m_loading) {
        m_loading = loading;
        emit loadingChanged();
    }
}

QString StickerPicker::askGet(const QString &set)
{
    QVariantMap args;
    args["set"] = set;
    return m_engine ? m_engine->request("stickers.get", args) : QString();
}

// The engine (re)connected: what is held is for its earlier run, its file ids are gone. A page that is open asks again; with
// none open the next to open does.
void StickerPicker::onConnected()
{
    if (!m_engine || !m_engine->connected())
        return;
    if (m_started)
        reset();
    if (m_wanted)
        load();
}

// A page opened while the engine (re)starts asks before it's there: it asks again on connecting (onConnected). Otherwise
// the sets, covers and pictures held are still good, so they show at once and only Recent and Favourites are asked again:
// the two the user changes by sending and faving. A full load that failed at stickers.sets is not held.
void StickerPicker::load()
{
    m_wanted = true;
    const bool held = m_started && !m_setsFailed;
    if (!held)
        reset();
    if (!m_engine || !m_engine->connected()) {
        settle();
        return;
    }
    m_idRecent = askGet("recent");
    m_idFaves = askGet("faves");
    if (!held) {
        m_idSets = m_engine->request("stickers.sets", QVariantMap());
        m_started = true;
    }
    settle();
}

void StickerPicker::release()
{
    m_wanted = false;
    settle();
}

// The sets, up to kSetJobs at once: the next is asked whenever one answers. A request that failed names no id (see
// dropLost), so a place is taken by a request out that did not fail.
void StickerPicker::nextSets()
{
    while (m_engine && m_engine->connected() && m_next < m_sets.size()
           && qMax(0, m_setIds.size() - m_lost) < kSetJobs) {
        const QString id = askGet(m_sets.at(m_next).id);
        if (id.isEmpty())
            break;
        m_setIds.insert(id, m_next++);
    }
}

// The failure of a stickers.get carries no id (EngineLink::failure), so it is only counted: which of the requests out it was
// is not known. When nothing is left to ask and no more are out than failed, those left will never answer: they are dropped,
// and the set or list among them is skipped (Recent and Favourites are asked again by the next page).
// ponytail: a failed Recent or Favourites is counted as a set's: one more set is asked at once, and loading lasts to the end;
// asking it again means opening the page again.
void StickerPicker::dropLost()
{
    const int out = (m_idRecent.isEmpty() ? 0 : 1) + (m_idFaves.isEmpty() ? 0 : 1) + m_setIds.size();
    if (m_lost == 0 || m_next < m_sets.size() || !m_idSets.isEmpty() || out > m_lost)
        return;
    m_idRecent.clear();
    m_idFaves.clear();
    m_setIds.clear();
    m_lost = 0;
}

// A group waits in m_pending, and flush() puts every waiting one in at once, at most every 250 ms. Telling the grid of each
// sticker (an itemAdded each, ~45 sets of up to 120) held the UI thread half a second a set: 5.8 s stalls, the list black
// while scrolled, and the next set asked only after (the Classic, 0.1.0.67, 2026-11-20).
void StickerPicker::insertGroup(const Group &group)
{
    m_pending.append(group);
    if (!m_flush.isActive())
        m_flush.start();
    ask(group.cover.pic, group.cover.picPath);
}

// The waiting groups go in by their order (a group of the same order is replaced), and the grid and the strip read everything
// again: one change for the lot.
QString StickerPicker::groupKey(const Group &g)
{
    return QString::number(g.order);
}

QString StickerPicker::cellKey(const Group &g, int i)
{
    return QString::number(g.order) + ":" + g.items.at(i).file;
}

void StickerPicker::flush()
{
    const bool added = !m_pending.isEmpty();
    Moves *moves = added ? new Moves : 0;
    if (moves) {
        foreach (const Group &g, m_groups) {
            moves->oldGroups << groupKey(g);
            QStringList cells;
            for (int i = 0; i < g.items.size(); ++i)
                cells << cellKey(g, i);
            moves->oldItems << cells;
        }
    }
    foreach (const Group &group, m_pending) {
        int pos = 0;
        while (pos < m_groups.size() && m_groups.at(pos).order < group.order)
            ++pos;
        if (pos < m_groups.size() && m_groups.at(pos).order == group.order)
            m_groups[pos] = group; // Recent or Favourites again: no hole where it was
        else
            m_groups.insert(pos, group);
    }
    m_pending.clear();
    if (moves) {
        for (int g = 0; g < m_groups.size(); ++g) {
            moves->groups.insert(groupKey(m_groups.at(g)), g);
            for (int i = 0; i < m_groups.at(g).items.size(); ++i)
                moves->items.insert(cellKey(m_groups.at(g), i), qMakePair(g, i));
        }
        m_model->changed(moves);
    }
    if (added || m_stripDirty)
        emit stripChanged();
    m_stripDirty = false;
    settle();
}

// Recent and Favourites are the engine's list again, at each page and when the user changes one. A list that is the same as
// the group's leaves it alone; an empty one takes the group out; else the new group waits for flush(), which replaces the old
// one in place (the grid saw a hole while it waited, when the old group was removed first).
void StickerPicker::setList(int order, const QString &title, const QString &icon, const QList<Item> &items)
{
    for (int g = 0; g < m_pending.size(); ++g) {
        if (m_pending.at(g).order == order) {
            m_pending.removeAt(g); // an earlier answer, not shown yet
            break;
        }
    }
    int shown = -1;
    for (int g = 0; g < m_groups.size() && shown < 0; ++g) {
        if (m_groups.at(g).order == order)
            shown = g;
    }
    if (shown >= 0 && sameStickers(m_groups.at(shown).items, items))
        return;
    if (items.isEmpty()) {
        if (shown >= 0) {
            m_groups.removeAt(shown);
            m_model->removed(at(shown));
            emit stripChanged();
        }
        return;
    }
    Group g;
    g.order = order;
    g.title = title;
    g.icon = icon;
    g.items = items;
    insertGroup(g);
}

// The flag of every sticker that went into Favourites or out of it is read again.
void StickerPicker::setFaves(const QList<Item> &list)
{
    QSet<QString> now;
    foreach (const Item &i, list)
        now.insert(i.file);
    QSet<QString> changed = now;
    changed.unite(m_faves);
    QSet<QString> both = now;
    both.intersect(m_faves);
    changed.subtract(both);
    m_faves = now;
    setList(-1, tr("Favourites"), "asset:///emoji/2b50.png", list);
    for (int g = 0; g < m_groups.size(); ++g) {
        if (m_groups.at(g).order == -1)
            continue;
        for (int i = 0; i < m_groups.at(g).items.size(); ++i) {
            if (changed.contains(m_groups.at(g).items.at(i).file))
                m_model->updated(at(g, i));
        }
    }
}

void StickerPicker::onReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    Q_UNUSED(cmd);
    const QVariantMap r = result.toMap();
    if (id.isEmpty())
        return;
    if (m_files.contains(id)) {
        const Download d = m_files.take(id);
        if (!r["path"].toString().isEmpty())
            decode(d.pic, r["path"].toString(), kGridSide);
        pump();
    } else if (id == m_idRecent) {
        m_idRecent.clear();
        setList(-2, tr("Recent"), "asset:///emoji/1f558.png", parseItems(r["stickers"]));
    } else if (id == m_idFaves) {
        m_idFaves.clear();
        setFaves(parseItems(r["stickers"]));
    } else if (id == m_idSets) {
        m_idSets.clear();
        foreach (const QVariant &s, r["sets"].toList()) {
            Set set;
            set.id = s.toMap()["set"].toString();
            set.title = s.toMap()["title"].toString();
            set.cover = parseItem(s.toMap()["cover"].toMap());
            m_sets << set;
        }
        nextSets();
    } else if (m_setIds.contains(id)) {
        const int n = m_setIds.take(id);
        const QList<Item> list = parseItems(r["stickers"]);
        if (!list.isEmpty()) {
            Group g;
            g.order = n;
            g.title = m_sets.at(n).title;
            g.icon = "asset:///emoji/1f43b.png";
            g.cover = m_sets.at(n).cover;
            g.items = list;
            insertGroup(g);
        }
        nextSets();
    } else if (m_faveIds.remove(id)) {
        m_idFaves = askGet("faves");
    } else if (id == m_previewId) {
        m_previewId.clear();
        const QString path = r["path"].toString();
        if (m_previewView && !path.isEmpty()) {
            // Still, a TGS too (its frame 0): nothing animates on the sticker page (the user, 2026-11-20).
            decode(QString(), path, kPreviewSide);
        }
    }
    dropLost();
    settle();
}

// The failure carries no id, only its cmd: a failed stickers.get is counted (dropLost) and the next set goes on. A failed
// stickers.sets leaves no sets, and the next page asks for everything again.
void StickerPicker::onFailure()
{
    if (!m_engine)
        return;
    const QString cmd = m_engine->failure().value("cmd").toString();
    if (cmd == "stickers.sets") {
        m_idSets.clear();
        m_setsFailed = true;
    } else if (cmd == "stickers.get") {
        ++m_lost;
        nextSets();
        dropLost();
    } else {
        return;
    }
    settle();
}

void StickerPicker::fave(const QString &file, bool on)
{
    if (!m_engine)
        return;
    QVariantMap args;
    args["file"] = file;
    args["on"] = on;
    const QString id = m_engine->request("sticker.fave", args);
    if (!id.isEmpty())
        m_faveIds.insert(id);
}

// The row of a cell. Reading a sticker's row is what asks for its picture, so only the cells the grid shows cost anything.
QVariant StickerPicker::rowData(const QVariantList &indexPath)
{
    const int g = indexPath.value(0).toInt();
    if (g < 0 || g >= m_groups.size())
        return QVariant();
    if (indexPath.size() == 1)
        return m_groups.at(g).title;
    const int i = indexPath.value(1).toInt();
    if (i < 0 || i >= m_groups.at(g).items.size())
        return QVariant();
    const Item &it = m_groups.at(g).items.at(i);
    ask(it.pic, it.picPath);
    QVariantMap row;
    row["file"] = it.file;
    row["emoji"] = it.emoji;
    row["format"] = it.format;
    row["image"] = QVariant::fromValue(m_images.value(it.pic));
    row["hasImage"] = m_images.contains(it.pic);
    row["fave"] = m_faves.contains(it.file);
    return row;
}

// A sticker's picture is asked once, the first time a cell wants it, and waits in a queue for a place. A sticker with no
// picture (a WebM without a thumbnail) shows its emoji.
void StickerPicker::ask(const QString &pic, const QString &path)
{
    if (pic.isEmpty() || m_asked.contains(pic))
        return;
    m_asked.insert(pic);
    m_picPath.insert(pic, path);
    m_queue.append(pic);
    pump();
}

// Starts what the places allow. The last asked goes first: it is the cell the user is looking at. A file TDLib has is
// decoded; any other is downloaded first (stickers.file), and decoded when it comes.
void StickerPicker::pump()
{
    while (m_files.size() + m_decoding < kJobs && !m_queue.isEmpty()) {
        const QString pic = m_queue.takeLast();
        const QString path = m_picPath.value(pic);
        if (!path.isEmpty()) {
            decode(pic, path, kGridSide);
            continue;
        }
        QVariantMap args;
        args["file"] = pic;
        const QString id = m_engine ? m_engine->requestQuiet("stickers.file", args) : QString();
        if (id.isEmpty()) {
            m_asked.remove(pic); // the engine is down: the next read of the cell asks again
            break;
        }
        Download d;
        d.pic = pic;
        d.since = QDateTime::currentMSecsSinceEpoch();
        m_files.insert(id, d);
        if (!m_sweeper.isActive())
            m_sweeper.start();
    }
}

void StickerPicker::sweep()
{
    const qint64 old = QDateTime::currentMSecsSinceEpoch() - kStallMs;
    for (QHash<QString, Download>::iterator it = m_files.begin(); it != m_files.end();) {
        if (it.value().since < old) {
            m_asked.remove(it.value().pic); // a cell that is read again asks again
            it = m_files.erase(it);
        } else {
            ++it;
        }
    }
    if (m_files.isEmpty())
        m_sweeper.stop();
    pump();
}

// Decodes on a worker thread (a TGS takes 20 to 60 ms to parse); onDecoded takes the result on this one. key is the pic
// of a grid picture, "" for the preview's.
void StickerPicker::decode(const QString &key, const QString &path, int side)
{
    QFutureWatcher<QString> *watcher = new QFutureWatcher<QString>(this);
    watcher->setProperty("key", key);
    watcher->setProperty("gen", m_gen);
    watcher->setProperty("tok", m_previewTok);
    connect(watcher, SIGNAL(finished()), this, SLOT(onDecoded()));
    watcher->setFuture(QtConcurrent::run(&Sticker::gridPicture, path, side));
    if (!key.isEmpty())
        ++m_decoding;
}

void StickerPicker::onDecoded()
{
    QFutureWatcher<QString> *watcher = static_cast<QFutureWatcher<QString> *>(sender());
    watcher->deleteLater();
    if (watcher->property("gen").toInt() != m_gen)
        return;
    const QString file = watcher->result(); // the picture's PNG, "" when it failed
    const QString key = watcher->property("key").toString();
    if (key.isEmpty()) {
        if (watcher->property("tok").toInt() == m_previewTok && m_previewView && !file.isEmpty())
            m_previewView->setImage(bb::cascades::Image(QUrl::fromLocalFile(file)));
        return;
    }
    --m_decoding;
    if (!file.isEmpty()) {
        m_images.insert(key, bb::cascades::Image(QUrl::fromLocalFile(file)));
        shown(key);
    }
    pump();
}

// A picture is ready: every cell that has it is read again (a sticker may be in Recent and in its set), and the strip when
// it is a set's cover.
void StickerPicker::shown(const QString &pic)
{
    bool cover = false;
    for (int g = 0; g < m_groups.size(); ++g) {
        cover = cover || m_groups.at(g).cover.pic == pic;
        for (int i = 0; i < m_groups.at(g).items.size(); ++i) {
            if (m_groups.at(g).items.at(i).pic == pic)
                m_model->updated(at(g, i));
        }
    }
    if (cover) {
        m_stripDirty = true; // the strip too is sent at most every 250 ms
        if (!m_flush.isActive())
            m_flush.start();
    }
}

// The sticker's own file, for the view: the grid's picture is there already. A WebM keeps it (no VP9 decoder). An answer
// after endPreview, or for another sticker, finds m_previewId changed and is dropped (onReplied); so is a decode (m_previewTok).
void StickerPicker::preview(QObject *imageView, const QString &file, const QString &format)
{
    m_previewView = qobject_cast<bb::cascades::ImageView *>(imageView);
    m_previewFormat = format;
    m_previewId.clear();
    ++m_previewTok;
    if (!m_engine || !m_previewView || (format != "tgs" && format != "webp"))
        return;
    QVariantMap args;
    args["file"] = file;
    m_previewId = m_engine->requestQuiet("stickers.file", args);
}

void StickerPicker::endPreview(QObject *imageView)
{
    Q_UNUSED(imageView);
    m_previewView = 0;
    m_previewId.clear();
    ++m_previewTok;
}
