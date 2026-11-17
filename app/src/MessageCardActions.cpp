#include "MessageCard.h"

#include <bb/cascades/ActionItem>
#include <bb/cascades/ActionSet>
#include <bb/cascades/ActiveTextTriggeredEvent>
#include <bb/cascades/Container>
#include <bb/cascades/DeleteActionItem>
#include <bb/cascades/ListView>
#include <bb/cascades/TouchEvent>

#include "CardStyle.h"
#include "Invites.h"

using namespace bb::cascades;

namespace {
// The QML file's name: the translations of MessageItem.qml's strings (hyperberry_it.ts) apply.
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
        case 0: state = Card::tr(kContext, "Sending"); break;
        case 1: state = Card::tr(kContext, "Sent"); break;
        case 2: state = Card::tr(kContext, "Delivered"); break;
        case 3: state = Card::tr(kContext, "Read"); break;
        default: state = Card::tr(kContext, "Not sent"); break;
        }
    }
    if (flag("edited"))
        state = state.isEmpty() ? Card::tr(kContext, "Edited") : state + QString::fromUtf8(" \xC2\xB7 ") + Card::tr(kContext, "Edited");
    m_menu->setSubtitle(state);
}

void MessageCard::onPopulating()
{
    if (!m_reply)
        makeActions();
    const bool deleted = flag("deleted");
    const bool readOnly = flag("readOnly");
    m_reply->setEnabled(!deleted && !readOnly);
    m_react->setEnabled(!deleted && !readOnly);
    if (m_votes)
        m_votes->setEnabled(flag("poll") && !deleted);
    m_copy->setEnabled(!deleted && !str("text").isEmpty());
    const bool starred = flag("starred");
    m_star->setTitle(Card::tr(kContext, starred ? "Unstar" : "Star"));
    m_star->setImageSource(Card::url(starred ? "asset:///images/unstar_action.png" : "asset:///images/star_action.png"));
    m_star->setEnabled(!deleted);
    m_edit->setEnabled(flag("editable"));
    m_revoke->setEnabled(flag("revocable"));
    m_deleteMe->setEnabled(!readOnly);
}

ActionItem *MessageCard::action(const char *title, const char *icon, const char *slot)
{
    ActionItem *item = new ActionItem();
    item->setTitle(Card::tr(kContext, title));
    item->setImageSource(Card::url(icon));
    connect(item, SIGNAL(triggered()), this, slot);
    m_menu->add(item);
    return item;
}

void MessageCard::makeActions()
{
    m_reply = action("Reply", "file:///usr/share/icons/bb_action_reply.png", SLOT(onReply()));
    // The icon is the emoji picker's action (step 5.11); the bar opens over the composer.
    m_react = action("React", "asset:///images/emoji.png", SLOT(onReact()));
    // Who chose what in a poll (the user's request, 0.0.1.132), as the counts' tap shows it per option.
    if (m_rich)
        m_votes = action("View votes", "file:///usr/share/icons/ic_accept.png", SLOT(onVotes()));
    // /usr/share/icons has no copy icon: this one is the stock Text Messages app's.
    m_copy = action("Copy", "asset:///images/copy.png", SLOT(onCopy()));
    // A filled star to star, an outline to take it off (gen-icons.py); onPopulating swaps them.
    m_star = action("Star", "asset:///images/star_action.png", SLOT(onStar()));
    m_edit = action("Edit", "file:///usr/share/icons/ic_edit.png", SLOT(onEdit()));
    m_revoke = action("Delete for everyone", "file:///usr/share/icons/bb_action_delete.png",
                      SLOT(onRevoke()));
    // "Delete for me" is a DeleteActionItem, which the system puts last.
    m_deleteMe = new DeleteActionItem();
    m_deleteMe->setTitle(Card::tr(kContext, "Delete for me"));
    connect(m_deleteMe, SIGNAL(triggered()), this, SLOT(onDeleteMe()));
    m_menu->add(m_deleteMe);
}

void MessageCard::onReply() { Card::call(m_list, "reply", m_msg); }

// A tap on a quote that is a story (a reply to one, or a like on one of ours): the page plays it.
void MessageCard::onQuote()
{
    if (flag("quoteStory"))
        Card::call(m_list, "openStory", m_msg);
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
            m_swipe = (dx < 0 && qAbs(dx) > qAbs(dy) && !flag("deleted") && !flag("readOnly")) ? 1 : -1;
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
    const bool reply = event->isUp() && qMin(dx, m_root->translationX()) <= -enough;
    if (!reply)
        qDebug("swipe: no reply, %s at %.0f px", event->isUp() ? "up" : "cancelled", dx);
    m_slide = ImplicitAnimationController(); // the animation back on, for the slide back
    m_root->setTranslationX(0);
    if (reply)
        onReply();
}
void MessageCard::onReact() { Card::call(m_list, "react", m_msg); }
void MessageCard::onVotes() { Card::call(m_list, "showVoters", str("votesText")); }
void MessageCard::onCopy() { Card::call(m_list, "copy", str("text")); }
void MessageCard::onStar() { Card::call(m_list, "star", m_msg); }
void MessageCard::onEdit() { Card::call(m_list, "edit", m_msg); }
void MessageCard::onRevoke() { Card::call(m_list, "deleteForEveryone", str("id")); }
void MessageCard::onDeleteMe() { Card::call(m_list, "deleteForMe", str("id")); }
void MessageCard::onWho() { Card::call(m_list, "whoReacted", str("id")); }

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
        event->abort(); // a group or channel invite: it's asked here, not in the browser
}
