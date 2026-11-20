#ifndef STICKERPICKER_H
#define STICKERPICKER_H

#include <bb/cascades/DataModel>
#include <bb/cascades/Image>
#include <bb/cascades/ImageView>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class EngineLink;
class StickerModel;

// The sticker page's data (StickerGrid; docs/protocol.md, "Stickers"; docs/stickers-plan.md, G4): Recent, Favourites and the
// installed sets, as the groups of a grid. One for the app (shared), so that what a page loaded is there when the next opens:
// the groups and the decoded pictures stay until the engine restarts (its file ids are gone then). The engine holds each list
// only while it answers, so up to four sets are asked at once; a picture is asked, downloaded and decoded only when the grid
// shows its cell, a few at a time, the decoding on a worker thread (a TGS takes 20 to 60 ms to parse).
class StickerPicker : public QObject
{
    Q_OBJECT
public:
    // Made on the first call, a child of the application.
    static StickerPicker *shared(EngineLink *engine);

    // The grid: the groups as the top level (itemType "header", data the title), their stickers as children (itemType
    // "item", data {file, emoji, format, image, hasImage, fave}). image is an empty Image until the picture is ready.
    bb::cascades::DataModel *model() const;
    // The strip over the grid: {image, key} for each group, in the grid's order. image is a string (an asset) or an Image (a
    // set's cover); key is the same for the same picture, so a cell can tell when its picture changed.
    QVariantList strip() const;
    // True until the sets list and every group have come, while a page is open.
    bool loading() const { return m_loading; }

    // A page opened. Asks for Recent, Favourites and the sets from nothing the first time and after the engine reconnected;
    // else only Recent and Favourites again (what the user changes), and the groups and pictures held show at once.
    void load();
    // The page closed: what is held stays, and an engine that reconnects is not asked for it until a page opens again.
    void release();
    // Adds the sticker to Favourites or takes it out; the group and every flag follow when the engine has done it.
    void fave(const QString &file, bool on);
    // Shows the sticker in imageView, which already has its grid picture: a WebP or a TGS (its frame 0) is shown still at the
    // preview's size, a WebM keeps the grid picture. Nothing animates on the sticker page (the user, 2026-11-20).
    void preview(QObject *imageView, const QString &file, const QString &format);
    void endPreview(QObject *imageView);

signals:
    void stripChanged();
    void loadingChanged();

private slots:
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onFailure();
    void onConnected();
    void onDecoded();
    void sweep();
    void flush();

private:
    friend class StickerModel;

    struct Item {
        QString file, emoji, format, pic, picPath;
    };
    struct Group {
        int order; // Recent -2, Favourites -1, a set its place in the engine's list: where the group goes among the others
        QString title;
        QString icon; // the strip's picture until the cover is ready
        Item cover;   // a set's cover; no pic for Recent and Favourites
        QList<Item> items;
    };
    struct Download {
        QString pic;
        qint64 since;
    };
    struct Set {
        QString id, title;
        Item cover;
    };

    explicit StickerPicker(EngineLink *engine);

    static Item parseItem(const QVariantMap &sticker);
    // Moves' keys: a group's is its order; a cell's, its group's and its sticker's file.
    static QString groupKey(const Group &g);
    static QString cellKey(const Group &g, int i);
    static QList<Item> parseItems(const QVariant &stickers);
    static bool sameStickers(const QList<Item> &a, const QList<Item> &b);
    void reset();
    void settle();
    QString askGet(const QString &set);
    void nextSets();
    void dropLost();
    void insertGroup(const Group &group);
    void setList(int order, const QString &title, const QString &icon, const QList<Item> &items);
    void setFaves(const QList<Item> &items);
    void ask(const QString &pic, const QString &path);
    void pump();
    void decode(const QString &key, const QString &path, int side);
    void shown(const QString &pic);

    // For StickerModel.
    int groupCount() const { return m_groups.size(); }
    QVariant rowData(const QVariantList &indexPath);

    QPointer<EngineLink> m_engine;
    StickerModel *m_model;
    QList<Group> m_groups;  // what the grid shows: changed only next to the signal that tells it
    QList<Group> m_pending; // sets in, not shown yet: flush() adds them all at once
    QTimer m_flush;         // single shot, 250 ms: the next flush()
    bool m_stripDirty;      // a cover arrived: the strip is sent again at the next flush()
    QSet<QString> m_faves; // the files in Favourites
    bool m_wanted;         // a page is open
    bool m_started;        // the requests of a full load went out: what is held is the engine's run now
    bool m_setsFailed;     // stickers.sets failed: the next page asks for everything again
    bool m_loading;
    int m_gen; // what a decode is tagged with: one that isn't the current is for a list that is gone
    QString m_idRecent, m_idFaves, m_idSets; // the requests we wait for, "" for none
    QHash<QString, int> m_setIds;            // stickers.get requests out for a set, by request id: the set's place in m_sets
    int m_lost;                              // stickers.get failures that named no request: see dropLost()
    QSet<QString> m_faveIds;                 // sticker.fave requests out
    QList<Set> m_sets;
    int m_next; // the next set to ask
    // The pictures: asked once each, queued, then downloaded when TDLib hasn't the file, then decoded.
    QHash<QString, bb::cascades::Image> m_images; // by pic, the file id of the picture
    QSet<QString> m_asked;
    QHash<QString, QString> m_picPath; // pic -> its path when TDLib had it, else ""
    QStringList m_queue;               // the last asked goes first: it is the one on screen
    QHash<QString, Download> m_files;  // stickers.file requests out, by request id
    int m_decoding;
    QTimer m_sweeper;
    // The preview.
    QPointer<bb::cascades::ImageView> m_previewView;
    QString m_previewFormat;
    QString m_previewId; // the stickers.file we wait for
    int m_previewTok;    // a decode or an answer for an earlier preview is dropped
};

#endif
