#ifndef MESSAGECARD_H
#define MESSAGECARD_H

#include <bb/cascades/Color>
#include <bb/cascades/ContextMenuVisualState>
#include <bb/cascades/CustomControl>
#include <QRectF>
#include <bb/cascades/ImplicitAnimationController>
#include <bb/cascades/ListItemListener>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace bb {
namespace cascades {
class AbstractShortcut;
class ActionItem;
class ActionSet;
class ActiveTextTriggeredEvent;
class Button;
class Container;
class Control;
class DeleteActionItem;
class ImageView;
class Label;
class ListView;
class TouchEvent;
class UIConfig;
}
}

class AlbumBubble;
class Invites;
class LinkPreview;
class MediaBubble;
class PollBubble;
class VoiceBubble;

// One message as a BBM-style card (PLAN.md §7), mirroring MessageItem.qml (rich) and TextCard.qml
// (plain, what a chat is mostly made of), both gone since step 8.6: flat, on the black page, theirs at the left and
// ours at the right, as wide as what they hold, with BBM's accent bar and tail under them. Built once, with the body text and the time and
// ticks, and filled by show() on each recycle. Every other part is made the first time a message
// needs it, then kept and hidden when the next one doesn't, since rows are recycled; each goes into
// the card's column at its QML place. The column holds, in order: sender, quote, [the voice, media
// and album bubbles], label, text, [link preview], invite button, [poll]. The sender is a line: the name, which only
// the oldest message of a person's run shows, and at its right end the person's tag (an admin's). In a group, an
// incoming message also has a tile before the card, the chat list's square, bottom aligned: the sender's picture over
// their initials on their colour for the run's newest message, an empty spacer of the same width for the others,
// so the run's cards line up. The picture card (a photo
// or video) is a frame instead: a head with the sender and quote, the picture across the card's
// width, and the card with the caption; with no caption the time sits on the picture. Elsewhere the time is
// at the end of the text's last line, under a voice note's waveform, or else beside the column.
class MessageCard : public bb::cascades::CustomControl, public bb::cascades::ListItemListener
{
    Q_OBJECT
public:
    MessageCard(bb::cascades::ListView *list, bool rich, bool picture = false);

    // msg is a "text", "msg" or "picture" row of MessageModel.
    void show(const QVariantMap &msg);
    // Reports this card's last laid-out height to the page when it shows row id (ChatPage's slide; MessageCards::slideRow).
    void reportSlide(const QString &id);

    // ListItemListener: a press tints the card; selection isn't used.
    void select(bool selected);
    void activate(bool activated);
    void reset(bool selected, bool activated);

private slots:
    void paintBackground();
    void onLink(bb::cascades::ActiveTextTriggeredEvent *event);
    void onPopulating();
    void onMenuState(bb::cascades::ContextMenuVisualState::Type state);
    void onReply();
    void onTouch(bb::cascades::TouchEvent *event);
    void onReact();
    void onForward();
    void onDownload();
    void onVotes();
    void onCopy();
    void onTranslate();
    void onEdit();
    void onRevoke();
    void onDeleteMe();
    void onChip();
    void onJoin();
    void onQuote();
    void onTile();
    void onLaidOut(const QRectF &frame);

private:
    QString str(const char *key) const { return m_msg.value(key).toString(); }
    bool flag(const char *key) const { return m_msg.value(key).toBool(); }
    Invites *joiner() const;

    void makeSender();
    void makeTile();
    void fillTile(bool on);
    float tileSpace() const;
    // The name line shows: a group's incoming message that is the oldest of its run (MessageModel's showName).
    bool named() const { return !str("sender").isEmpty() && flag("showName"); }
    void makeForwarded();
    void makeQuote();
    void makeLabel();
    void makeJoin();
    void makeBang();
    void makeChip();
    void growChips(int n);
    void fillChips();
    float chipWidth(int count) const;
    void makeActions();
    void makeBubbles();
    void place(bb::cascades::Control *part, bb::cascades::Control *a = 0, bb::cascades::Control *b = 0,
               bb::cascades::Control *c = 0, bb::cascades::Control *d = 0);
    bb::cascades::ActionItem *action(const char *title, const char *icon, const char *slot, const char *key);
    void setKeys(bool on);

    void restyle(bool out);
    void paintCorners(const bb::cascades::Color &card);
    void fillColumn();
    void fillFoot();
    void makeBars();
    void makeTimer();
    bb::cascades::ImageView *makeTail(const char *source);
    void fillMenu();
    void fillBubbles();
    void placeTime(int place);
    float timeWidth() const;
    QString timeLead() const;

    bb::cascades::ListView *m_list;
    bb::cascades::UIConfig *m_ui;
    bool m_rich;
    QVariantMap m_msg;
    bool m_pressed;
    int m_timePlace; // where m_timeBox is: -1 nowhere yet, 0 beside the column, 1 at the text's end, 2 on the picture, 3 under the voice note, 4 on the sticker, 5 on the album
    float m_timeTop; // m_timeBox's top padding as last set, -1 for none
    float m_colMax; // m_col's maxWidth as last set, -1 for none
    int m_tick; // the picture m_ticks has: -1 none yet, 0 the clock, 1 one tick, 2 two

    // Swipe to reply: where the finger went down, and whether this touch is a swipe (1), not one (-1)
    // or not yet known (0). m_slide holds translationX's implicit animation off while the finger drives it.
    float m_downX;
    float m_downY;
    int m_swipe;
    bb::cascades::ImplicitAnimationController m_slide;

    // What the styling was last set from: restyle() runs again when one of them changes.
    bool m_styled;
    bool m_restyledOut; // the side restyle() last styled: a recycled card changes it
    bool m_round; // and the Shape (Round has other corners)
    float m_scale;
    int m_body;
    int m_small;
    bb::cascades::Color m_plain;

    bb::cascades::Container *m_root;
    // The spacers beside the card: the left one shows for ours, the right one for theirs.
    bb::cascades::Container *m_gapL;
    bb::cascades::Container *m_gapR;
    // What carries the background: the card, or in the picture card the frame around the head, the
    // picture and the card. m_head is there only in the picture card.
    bb::cascades::Container *m_frame;
    bb::cascades::ImageView *m_bg; // the card's colour and rounded corners, behind m_frame
    bb::cascades::Container *m_head;
    bb::cascades::Container *m_card;
    bb::cascades::Container *m_col;
    // The row: the tile, the spacers and the card. The tile is made the first time a group's incoming message needs it.
    bb::cascades::Container *m_row;
    bb::cascades::Container *m_avatar;
    bb::cascades::ImageView *m_photo;
    bb::cascades::ImageView *m_disc; // Round mode's colour of the tile, under the picture
    bb::cascades::Label *m_initials;
    QString m_photoPath; // what m_photo was last given, "" for none; in Round mode the picture's round copy
    // The name line: the name and, docked at its right end, the tag. m_tagRoom is a transparent copy of the tag after
    // the name, which keeps the two apart when the line is as narrow as both.
    bb::cascades::Container *m_nameLine;
    bb::cascades::Container *m_names;
    bb::cascades::Label *m_sender;
    bb::cascades::Label *m_tagRoom;
    bb::cascades::Label *m_tag;
    // "Forwarded", or "Forwarded many times", under the sender: an arrow and a grey italic word.
    bb::cascades::Container *m_fwd;
    bb::cascades::ImageView *m_fwdIcon;
    bb::cascades::Label *m_fwdText;
    bb::cascades::Container *m_quote;
    bb::cascades::ImageView *m_bar;     // the quote's accent bar (images/quote_bar)
    bb::cascades::ImageView *m_quoteBg; // the quote's box (images/quote), tinted with quoteTint
    int m_quoteShape;                   // the shape its images are for: -1 none yet, 0 Square, 1 Round
    bb::cascades::Label *m_quoteName;
    bb::cascades::Label *m_quoteText;
    bb::cascades::ImageView *m_quoteThumb;
    bb::cascades::Label *m_label;
    bb::cascades::Container *m_words; // the text, and the time over its last line's end
    bb::cascades::Label *m_text;
    bb::cascades::Button *m_join;
    // The rich card's bubbles, made as m_sender is (VoiceBubble.cpp and the others).
    VoiceBubble *m_voice;
    MediaBubble *m_media;
    AlbumBubble *m_album;
    LinkPreview *m_link;
    PollBubble *m_poll;
    bb::cascades::Container *m_timeBox;
    bb::cascades::ImageView *m_timer; // a self-destructing message's clock, before the time; 0 until a card first has a timer (makeTimer)
    bb::cascades::Label *m_time;
    bb::cascades::ImageView *m_ticks;
    bb::cascades::Label *m_bang;
    // The foot: a tail either side of the accent bar, one shown; the bar is one of two, as the tails are,
    // so the fill only toggles them (theirs m_barL, ours m_barR; restyle() tints them).
    // Both 0 until a card first shows one (makeBars): Round mode never does.
    bb::cascades::Container *m_foot;
    bb::cascades::ImageView *m_tailL;
    bb::cascades::ImageView *m_barL;
    bb::cascades::ImageView *m_barR;
    bb::cascades::ImageView *m_tailR;
    // The reactions: m_chip is the area under the card, holding rows of chips (a row ends where the column's width
    // does). The chips are a pool, kept and moved between the rows as cards are recycled; the ones a message does not
    // use wait in m_chipSpare, which is hidden. m_chipAt is the row each chip is in, -1 for the spare.
    bb::cascades::Container *m_chip;
    bb::cascades::Container *m_chipSpare;
    QList<bb::cascades::Container *> m_chipRows;
    QList<bb::cascades::Container *> m_chipBoxes;
    QList<bb::cascades::Label *> m_chipLabels;
    QList<bb::cascades::ImageView *> m_chipPills; // Round mode: each chip's colour, a pill under it
    QList<int> m_chipAt;

    QString m_edited; // "edited ", translated
    QString m_starMark; // the star (U+2605) and a space, before the time of a starred message
    QString m_trMark; // U+21C4 and a space, before the time of a message shown translated
    float m_laidHeight;  // the height of the last layout (onLaidOut), 0 before the first: the slide's distance
    bb::cascades::ActionSet *m_menu;
    bb::cascades::ActionItem *m_reply;
    bb::cascades::ActionItem *m_react;
    bb::cascades::ActionItem *m_forward;
    bb::cascades::ActionItem *m_votes;
    bb::cascades::ActionItem *m_download;
    bb::cascades::ActionItem *m_copy;
    bb::cascades::ActionItem *m_translate;
    bb::cascades::ActionItem *m_edit;
    bb::cascades::ActionItem *m_revoke;
    bb::cascades::DeleteActionItem *m_deleteMe;
    QList<bb::cascades::AbstractShortcut *> m_keys; // the menu's letters, live only while it shows
};

#endif
