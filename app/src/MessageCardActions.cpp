#include "MessageCard.h"

#include <bb/cascades/ActionItem>
#include <bb/cascades/ActionSet>
#include <bb/cascades/ActiveTextTriggeredEvent>
#include <bb/cascades/Container>
#include <bb/cascades/DeleteActionItem>
#include <bb/cascades/ListView>
#include <bb/cascades/Shortcut>
#include <bb/cascades/TouchEvent>

#include "CardStyle.h"
#include "VoiceBubble.h"
#include "Invites.h"

using namespace bb::cascades;

namespace {
// The QML file's name: the translations of MessageItem.qml's strings (tengram_it.ts) apply.
const char *const kContext = "MessageItem";
}

// What a card does when touched: its long-press menu, the join button and a tapped link.

// The menu's title is when the message was sent, the day in full; ours have their state under it,
// in words, since the title cannot hold the ticks' pictures (the user asked for both, 2026-11-08).
void MessageCard::fillMenu()
{
    m_menu->setTitle(str("stamp"));
    QString state;
    if (flag("out")) {
        switch (m_msg.value("state").toInt()) {
        case 0: state = Card::tr(kContext, QT_TRANSLATE_NOOP("MessageItem", "Sending")); break;
        case 1: state = Card::tr(kContext, QT_TRANSLATE_NOOP("MessageItem", "Sent")); break;
        case 2: state = Card::tr(kContext, QT_TRANSLATE_NOOP("MessageItem", "Delivered")); break;
        case 3: state = Card::tr(kContext, QT_TRANSLATE_NOOP("MessageItem", "Read")); break;
        default: state = Card::tr(kContext, QT_TRANSLATE_NOOP("MessageItem", "Not sent")); break;
        }
    }
    if (flag("edited"))
        state = state.isEmpty() ? Card::tr(kContext, QT_TRANSLATE_NOOP("MessageItem", "Edited")) : state + QString::fromUtf8(" \xC2\xB7 ") + Card::tr(kContext, QT_TRANSLATE_NOOP("MessageItem", "Edited"));
    m_menu->setSubtitle(state);
}

void MessageCard::onPopulating()
{
    if (!m_reply)
        makeActions();
    setKeys(true);
    const bool deleted = flag("deleted");
    const bool readOnly = flag("readOnly");
    m_reply->setEnabled(!deleted && !readOnly);
    m_react->setEnabled(!deleted && !flag("secret")); // a channel's post too: anyone may react; not in a secret chat
    m_forward->setEnabled(flag("forwardable"));
    m_commentsItem->setEnabled(m_msg.value("comments", -1).toInt() >= 0 && !deleted); // a channel post with a discussion group
    if (m_votes)
        m_votes->setEnabled(flag("poll") && !deleted);
    if (m_download)
        m_download->setEnabled(!deleted && !flag("secret") && (str("media") == "doc" || str("media") == "audio"));
    m_copy->setEnabled(!deleted && !str("text").isEmpty());
    m_translate->setEnabled(flag("canTranslate"));
    m_translate->setTitle(Card::tr(kContext, flag("translated") ? QT_TRANSLATE_NOOP("MessageItem", "Show original") : QT_TRANSLATE_NOOP("MessageItem", "Translate")));
    m_edit->setEnabled(flag("editable"));
    m_revoke->setEnabled(flag("revocable"));
    m_deleteMe->setEnabled(!readOnly);
}

// The menu's letters (the user's ask, 2026-11-17), as the Hub's: on from populating, which the menu
// reads them at, until it hides. Left on, they ate the letters typed into the page (bb10-pitfalls).
void MessageCard::setKeys(bool on)
{
    foreach (AbstractShortcut *key, m_keys)
        key->setEnabled(on);
}

void MessageCard::onMenuState(ContextMenuVisualState::Type state)
{
    if (state == ContextMenuVisualState::Hidden)
        setKeys(false);
}

ActionItem *MessageCard::action(const char *title, const char *icon, const char *slot, const char *key)
{
    ActionItem *item = new ActionItem();
    item->setTitle(Card::tr(kContext, title));
    item->setImageSource(Card::url(icon));
    connect(item, SIGNAL(triggered()), this, slot);
    Shortcut *shortcut = Shortcut::create().key(key);
    shortcut->setEnabled(false);
    item->addShortcut(shortcut);
    m_keys << shortcut;
    m_menu->add(item);
    return item;
}

void MessageCard::makeActions()
{
    m_reply = action(QT_TRANSLATE_NOOP("MessageItem", "Reply"), "file:///usr/share/icons/bb_action_reply.png", SLOT(onReply()), "r");
    // The icon is the emoji picker's action (step 5.11); the bar opens over the composer.
    m_react = action(QT_TRANSLATE_NOOP("MessageItem", "React"), "asset:///images/emoji.png", SLOT(onReact()), "a");
    m_forward = action(QT_TRANSLATE_NOOP("MessageItem", "Forward"), "file:///usr/share/icons/bb_action_forward.png", SLOT(onForward()), "f");
    // The post's comments, where the line under the card is out of the trackpad's reach (onPopulating).
    m_commentsItem = action(QT_TRANSLATE_NOOP("MessageItem", "Comments"), "file:///usr/share/icons/bb_action_sendtextmessage.png", SLOT(onComments()), "m");
    if (m_rich) {
        // Who chose what in a poll (the user's request, 0.0.1.132), as the counts' tap shows it per option.
        m_votes = action(QT_TRANSLATE_NOOP("MessageItem", "View votes"), "file:///usr/share/icons/ic_accept.png", SLOT(onVotes()), "v");
        // A file into the phone's Downloads, where File Manager shows it (the user's ask, 2026-11-18).
        m_download = action(QT_TRANSLATE_NOOP("MessageItem", "Download"), "asset:///images/save_action.png", SLOT(onDownload()), "s");
    }
    // /usr/share/icons has no copy icon: this one is the stock Text Messages app's.
    m_copy = action(QT_TRANSLATE_NOOP("MessageItem", "Copy"), "asset:///images/copy.png", SLOT(onCopy()), "c");
    // Opens the translation dialog, or the original of a message already shown translated (onPopulating).
    m_translate = action(QT_TRANSLATE_NOOP("MessageItem", "Translate"), "asset:///images/translate_action.png", SLOT(onTranslate()), "t");
    m_edit = action(QT_TRANSLATE_NOOP("MessageItem", "Edit"), "file:///usr/share/icons/ic_edit.png", SLOT(onEdit()), "e");
    m_revoke = action(QT_TRANSLATE_NOOP("MessageItem", "Delete for everyone"), "file:///usr/share/icons/bb_action_delete.png",
                      SLOT(onRevoke()), "x");
    // "Delete for me" is a DeleteActionItem, which the system puts last.
    m_deleteMe = new DeleteActionItem();
    m_deleteMe->setTitle(Card::tr(kContext, QT_TRANSLATE_NOOP("MessageItem", "Delete for me")));
    connect(m_deleteMe, SIGNAL(triggered()), this, SLOT(onDeleteMe()));
    Shortcut *key = Shortcut::create().key("d");
    key->setEnabled(false);
    m_deleteMe->addShortcut(key);
    m_keys << key;
    m_menu->add(m_deleteMe);
}

void MessageCard::onReply() { Card::call(m_list, "reply", m_msg); }

// A tap on a quote: the page goes to the message it quotes (the user's ask, 2026-11-17).
void MessageCard::onQuote()
{
    if (!str("quoteId").isEmpty())
        Card::call(m_list, "openQuote", m_msg);
}

// Swipe left to reply (the user, 2026-11-08): the card follows the finger up to 56 px left; let go
// past 36 px and it replies, as the menu's Reply does. Either way it slides back. A touch that moves
// up or down first is the list's scroll, and the card stays put. The release counts where the finger
// left, not the card's last move: a quick flick's last move is short of where it ends (the user,
// 2026-11-16: the card slid and no reply came).
void MessageCard::onTouch(TouchEvent *event)
{
    const float full = 56;
    const float enough = 36;
    if (event->isDown()) {
        m_downX = event->windowX(); // the window's: the card's own coordinates move with it
        m_downY = event->windowY();
        m_swipe = 0;
        return;
    }
    const float dx = event->windowX() - m_downX;
    const float dy = event->windowY() - m_downY;
    if (event->isMove()) {
        if (m_swipe == 0 && qMax(qAbs(dx), qAbs(dy)) > m_ui->du(2)) {
            // A drag along a voice note's waveform is its seek, which has taken the touch already.
            m_swipe = (dx < 0 && qAbs(dx) > qAbs(dy) && !flag("deleted") && !flag("readOnly")
                       && !(m_voice && m_voice->scrubbing())) ? 1 : -1;
            if (m_swipe == 1)
                m_slide = ImplicitAnimationController::create(m_root, "translationX").enabled(false);
        }
        if (m_swipe == 1)
            m_root->setTranslationX(qBound(-full, dx, 0.0f));
        return;
    }
    if (m_swipe != 1) // up or cancel
        return;
    m_swipe = 0;
    // A cancel counts as a let-go: it is the list taking the touch once a long swipe drifts up or down (the user,
    // 2026-11-17: two swipes in three missed, every one cancelled 270-720 px out, none let go short).
    const bool reply = qMin(dx, m_root->translationX()) <= -enough;
    if (!reply)
        qDebug("swipe: no reply, %s at %.0f px", event->isUp() ? "up" : "cancelled", dx);
    m_slide = ImplicitAnimationController(); // the animation back on, for the slide back
    m_root->setTranslationX(0);
    if (reply)
        onReply();
}
void MessageCard::onReact() { Card::call(m_list, "react", m_msg); }
void MessageCard::onForward() { Card::call(m_list, "forward", m_msg); }
void MessageCard::onComments() { Card::call(m_list, "openComments", str("id")); }
void MessageCard::onDownload() { Card::call(m_list, "download", m_msg); }
void MessageCard::onVotes() { Card::call(m_list, "showVoters", str("votesText")); }
void MessageCard::onCopy() { Card::call(m_list, "copy", str("text")); }
void MessageCard::onTranslate() { Card::call(m_list, "translate", m_msg); }
void MessageCard::onEdit() { Card::call(m_list, "edit", m_msg); }
void MessageCard::onRevoke() { Card::call(m_list, "deleteForEveryone", str("id")); }
void MessageCard::onDeleteMe() { Card::call(m_list, "deleteForMe", str("id")); }
// A tap on a chip adds our reaction of its emoji, or takes it back when it is ours (the page's toggleReaction).
void MessageCard::onChip()
{
    QObject *box = sender() ? sender()->parent() : 0;
    if (box)
        Card::call(m_list, "toggleReaction", str("id"), box->property("emoji"), box->property("mine"));
}

// A tap on the sender's tile in a group: their page (the page's openPerson). Only the run's tile with the picture or
// initials, not the empty spacer beside the run's other messages, and only a person: a negative id is a channel or a
// group writing as itself.
void MessageCard::onTile()
{
    const QString id = str("senderId");
    if (flag("showTile") && !id.isEmpty() && !id.startsWith('-'))
        Card::call(m_list, "openPerson", id, str("sender"));
}

// The page's invites (main.cpp's Invites), which the list holds as `joiner`.
Invites *MessageCard::joiner() const
{
    return qobject_cast<Invites *>(qvariant_cast<QObject *>(m_list->property("joiner")));
}

// Join group: asks at once, by the first line of the card's text.
void MessageCard::onJoin()
{
    Invites *invites = joiner();
    if (invites)
        invites->openCard(str("invite"), str("id"), str("text").split("\n").first());
}

void MessageCard::onLink(ActiveTextTriggeredEvent *event)
{
    const QString href = event->href().toString();
    if (href.startsWith("hbmention:")) {
        event->abort(); // ours: the system can't open it
        Card::call(m_list, "openPerson", href.mid(10), event->text());
        return;
    }
    Invites *invites = joiner();
    if (invites && invites->openLink(href))
        event->abort(); // a Telegram link: it's asked here (Invites opens the browser itself when it's not ours)
}
