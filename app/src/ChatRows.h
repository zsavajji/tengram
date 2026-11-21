#ifndef CHATROWS_H
#define CHATROWS_H

#include <bb/cascades/CustomControl>
#include <bb/cascades/ListItemListener>
#include <bb/cascades/ListItemProvider>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace bb {
namespace cascades {
class ActionItem;
class ActionSet;
class Container;
class ImageView;
class Label;
class ListView;
class UIConfig;
}
}

// The rows of the chat lists, in C++ as the message cards are (MessageCards.h): a row is built once,
// in createItem, and filled on each recycle, in updateItem, with setters in place of QML's bindings.
// ChatList.qml sets it as the list's listItemProvider; the rows reach the work through the list's
// functions and properties. Types: "header" and "item". A forum's topics (TopicModel) are items of the same rows,
// in a list with topicList set.
class ChatRows : public bb::cascades::ListItemProvider
{
    Q_OBJECT
public:
    ChatRows(QObject *parent = 0);

    bb::cascades::VisualNode *createItem(bb::cascades::ListView *list, const QString &type);
    void updateItem(bb::cascades::ListView *list, bb::cascades::VisualNode *listItem,
                    const QString &type, const QVariantList &indexPath, const QVariant &data);

private:
    QPointer<bb::cascades::ListView> m_list; // the list whose text size change is wired
};

// One row of a chat list (PLAN.md §7), mirroring ChatRow.qml (gone since this port): at least 9.4 du
// (85 px on the Q10, the stock Text Messages row), then a 2 px separator. chat is a row of
// ChatListModel. Every part is built at once, in the constructor, and shown or hidden by show(): the
// row is small. Every text that can hold emoji takes the list's shared emoji style: a FontFaceRule's
// font reaches only the styles built on it (Q10, 2026-10-31). Its percentages are the system styles'
// sizes as measured on the Q10. 100 % draws capitals 20 px tall, the tile's TitleText 24 px (118 %).
// The name 115 % and the preview 90 %: the cards' 105 and 85, capitals 2 and 1 px taller (the user,
// 2026-11-08). Both grow with the text size setting.
class ChatRow : public bb::cascades::CustomControl, public bb::cascades::ListItemListener
{
    Q_OBJECT
public:
    ChatRow(bb::cascades::ListView *list);

    // chat is a row of ChatListModel.
    void show(const QVariantMap &chat);

    // ListItemListener: a press tints the row; selection isn't used.
    void select(bool selected);
    void activate(bool activated);
    void reset(bool selected, bool activated);

private slots:
    void onPopulating();
    void onRead();
    void onPin();
    void onUp();
    void onDown();
    void onMute();
    void onTranslate();
    void onArchive();

private:
    QString str(const char *key) const { return m_chat.value(key).toString(); }
    bool flag(const char *key) const { return m_chat.value(key).toBool(); }

    void paintBackground();
    void makeAt();
    void makeActions();
    void showMoves(bool on);
    bb::cascades::ActionItem *action(const char *slot);

    bb::cascades::ListView *m_list;
    bb::cascades::UIConfig *m_ui;
    QVariantMap m_chat;
    bool m_pressed;
    QString m_photoPath; // the picture last given to m_photo: the file, or in Round mode its round copy
    bool m_styled;
    float m_scale; // the text scale the sizes below were last set for
    bool m_compact; // and the interface
    bool m_topics; // the list is a forum's topics (TopicModel): no long-press menu, the name bold while unread

    bb::cascades::Container *m_root; // carries the press colour, the separator included
    bb::cascades::Container *m_row;
    bb::cascades::Container *m_col; // the name and preview lines
    bb::cascades::Container *m_tile;
    bb::cascades::ImageView *m_disc; // Round mode's colour of the tile
    bb::cascades::ImageView *m_photo;
    bb::cascades::Label *m_initials;
    bb::cascades::Label *m_name;
    bb::cascades::ImageView *m_secret;
    bb::cascades::ImageView *m_lock;
    bb::cascades::ImageView *m_pin;
    bb::cascades::ImageView *m_mute;
    bb::cascades::Label *m_time;
    bb::cascades::ImageView *m_ticks;
    bb::cascades::Label *m_bang;
    bb::cascades::Label *m_preview;
    bb::cascades::Container *m_bottom;    // the row's last line, which makeAt() adds the @ to
    bb::cascades::Container *m_at;        // an unread mention: "@" on a disc (a square), before the count; 0 until a mention
    bb::cascades::ImageView *m_atDisc;    // Round mode: its disc
    bb::cascades::Container *m_badge;
    bb::cascades::ImageView *m_badgePill; // Round mode: the badge's colour, under the count (Cascades has no radius)
    bb::cascades::Label *m_count;
    bb::cascades::Container *m_dot;
    bb::cascades::ImageView *m_dotDisc; // Round mode: the dot as a disc
    bb::cascades::Container *m_sep;

    bb::cascades::ActionSet *m_menu;
    bb::cascades::ActionItem *m_readItem;
    bb::cascades::ActionItem *m_pinItem;
    bb::cascades::ActionItem *m_upItem; // Move up and down: in the menu for a pinned chat only (showMoves)
    bb::cascades::ActionItem *m_downItem;
    bb::cascades::ActionItem *m_muteItem;
    bb::cascades::ActionItem *m_translateItem;
    bb::cascades::ActionItem *m_archiveItem;
};

#endif
