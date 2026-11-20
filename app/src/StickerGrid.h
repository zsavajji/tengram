#ifndef STICKERGRID_H
#define STICKERGRID_H

#include <bb/cascades/CustomControl>
#include <bb/cascades/ListItemListener>
#include <bb/cascades/ListItemProvider>
#include <bb/cascades/TextStyle>
#include <QList>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace bb {
namespace cascades {
class ActivityIndicator;
class Button;
class Container;
class ImageView;
class Label;
class ListView;
class UIConfig;
}
}

class StickerGrid;
class StickerPicker;
class StripCell;

// The sticker page's content (StickersPage.qml, docs/stickers-plan.md, G4), in C++ as the chat's cards are: a strip of the
// groups over a grid of four columns (Recent, Favourites, then the installed sets; StickerPicker holds the data, one for the
// app), the empty state, and the preview over everything. A tap on a sticker sends it (picked) and a long press shows it large
// over a dimmed page, still, with Send and Add to or Remove from favourites. Nothing on the page animates (the user,
// 2026-11-20). It fills what it is in.
class StickerGrid : public bb::cascades::CustomControl
{
    Q_OBJECT
    // The EngineLink, set as ChatSearch's link is. Setting it loads the data (StickerPicker::load).
    Q_PROPERTY(QObject *link READ link WRITE setLink)
    // The page's colours and the emoji style, from main.qml's nav (pageColor "#RRGGBB"): what a QML list item read through
    // its view, and the C++ cards read from the list.
    Q_PROPERTY(QString pageColor READ pageColor WRITE setPageColor)
    Q_PROPERTY(QString pressColor READ pressColor WRITE setPressColor)
    Q_PROPERTY(QVariant emojiStyle READ emojiStyle WRITE setEmojiStyle)
public:
    explicit StickerGrid(bb::cascades::Container *parent = 0);
    ~StickerGrid();

    QObject *link() const { return m_link; }
    void setLink(QObject *link);
    QString pageColor() const { return m_pageColor; }
    void setPageColor(const QString &color);
    QString pressColor() const { return m_pressColor; }
    void setPressColor(const QString &color) { m_pressColor = color; }
    QVariant emojiStyle() const { return m_emojiStyle; }
    void setEmojiStyle(const QVariant &style) { m_emojiStyle = style; }
    bb::cascades::TextStyle emojiTextStyle() const { return m_emojiStyle.value<bb::cascades::TextStyle>(); }

    // A cell's long press: the sticker's row, {file, emoji, format, image, hasImage, fave}.
    void openPreview(const QVariantMap &sticker);

signals:
    // A tap on a sticker, or Send in the preview: its file, which sticker.send takes.
    void picked(const QString &file);

private slots:
    void syncStrip();
    void syncState();
    void go(int group);
    void onFirstVisible(const QVariantList &path);
    void onTriggered(const QVariantList &path);
    void closePreview();
    void onSend();
    void onFave();

private:
    void mark(int group);

    QPointer<QObject> m_link;
    QPointer<StickerPicker> m_picker;
    QString m_pageColor, m_pressColor;
    QVariant m_emojiStyle;
    bb::cascades::Container *m_root;
    bb::cascades::Container *m_strip;
    QList<StripCell *> m_cells;
    int m_marked; // the group under the grid's first visible item
    bb::cascades::ActivityIndicator *m_spin;
    bb::cascades::ListView *m_list;
    bb::cascades::Label *m_empty;
    bb::cascades::Container *m_overlay;
    bb::cascades::ImageView *m_previewView;
    bb::cascades::Button *m_faveButton;
    QVariantMap m_preview; // the sticker shown
    bool m_previewing;
};

// The grid's cells, as MessageCards makes the chat's: a Header for a group's title, a StickerCell for a sticker.
class StickerCells : public bb::cascades::ListItemProvider
{
    Q_OBJECT
public:
    explicit StickerCells(StickerGrid *grid);

    bb::cascades::VisualNode *createItem(bb::cascades::ListView *list, const QString &type);
    void updateItem(bb::cascades::ListView *list, bb::cascades::VisualNode *listItem, const QString &type,
                    const QVariantList &indexPath, const QVariant &data);

private:
    StickerGrid *m_grid; // the list is the grid's, so the grid outlives the cells
};

// One sticker's cell: its grid picture 90 % of the cell, or its emoji when it has none (a WebM without a thumbnail). A press
// tints it; a long press opens the grid's preview.
class StickerCell : public bb::cascades::CustomControl, public bb::cascades::ListItemListener
{
    Q_OBJECT
public:
    explicit StickerCell(StickerGrid *grid);

    // sticker is a row of StickerPicker's model.
    void show(const QVariantMap &sticker);

    // ListItemListener: a press tints the cell; selection isn't used.
    void select(bool selected);
    void activate(bool activated);
    void reset(bool selected, bool activated);

private slots:
    void onLongPress();

private:
    StickerGrid *m_grid;
    QVariantMap m_sticker;
    bb::cascades::Container *m_root;
    bb::cascades::ImageView *m_image;
    bb::cascades::Label *m_emoji;
};

// A cell of the strip: a group's picture 54 px in 72, underlined in the primary colour while the grid's first visible item
// is in its group.
class StripCell : public bb::cascades::CustomControl
{
    Q_OBJECT
public:
    StripCell(int index, bb::cascades::UIConfig *ui);

    // row is StickerPicker::strip's {image, key}: the picture is set only when the key changed.
    void show(const QVariantMap &row);
    void mark(bool on);

signals:
    void tapped(int index);

private slots:
    void onTap();

private:
    int m_index;
    QString m_key;
    bb::cascades::ImageView *m_icon;
    bb::cascades::Container *m_underline;
};

#endif
