#include "MessageCard.h"

#include <bb/cascades/Button>
#include <bb/cascades/Container>
#include <bb/cascades/Label>

#include "AlbumBubble.h"
#include "LinkPreview.h"
#include "MediaBubble.h"
#include "PollBubble.h"
#include "VoiceBubble.h"

using namespace bb::cascades;

// The rich card's bubbles: where each goes in the column, and when it shows.

// Puts a part made late in its place in the column: before the first of the parts that follow it
// there, or last.
void MessageCard::place(Control *part, Control *a, Control *b, Control *c, Control *d)
{
    Control *const next[] = { a, b, c, d };
    for (int i = 0; i < 4; i++) {
        if (next[i] && m_col->indexOf(next[i]) >= 0) {
            m_col->insert(m_col->indexOf(next[i]), part);
            return;
        }
    }
    m_col->add(part);
}

// The bubbles this message needs and the card hasn't made yet, each built only for the rows that
// need it, as MessageItem.qml's ControlDelegates were (step 8.6 removed it): a bubble in every card would weigh down every
// row (4.1). In the column: voice, media, album, [label, text], link preview, [invite], poll. An
// album's media is "", so MediaBubble isn't made for it.
void MessageCard::makeBubbles()
{
    const bool deleted = flag("deleted");
    if (flag("voice") && !m_voice) {
        m_voice = new VoiceBubble(m_list);
        place(m_voice, m_media, m_album, m_label, m_words);
    }
    if (!str("media").isEmpty() && !m_media) {
        // In the picture card it spans the card, between the head and the caption.
        m_media = new MediaBubble(m_list, m_head != 0);
        if (m_head)
            m_frame->insert(1, m_media);
        else
            place(m_media, m_album, m_label, m_words);
    }
    if (!m_msg.value("album").toList().isEmpty() && !m_album) {
        m_album = new AlbumBubble(m_list);
        place(m_album, m_label, m_words);
    }
    if (!str("linkTitle").isEmpty() && !deleted && !m_link) {
        m_link = new LinkPreview(m_list);
        place(m_link, m_join, m_poll);
    }
    if (flag("poll") && !deleted && !m_poll) {
        m_poll = new PollBubble(m_list);
        place(m_poll);
    }
}

// Each bubble shows this message, or hides when it has none of its kind.
void MessageCard::fillBubbles()
{
    const bool deleted = flag("deleted");
    const bool on[] = { flag("voice"), !str("media").isEmpty(), !m_msg.value("album").toList().isEmpty(),
                        !str("linkTitle").isEmpty() && !deleted, flag("poll") && !deleted };
    if (m_voice) {
        m_voice->setVisible(on[0]);
        if (on[0])
            m_voice->show(m_msg);
    }
    if (m_media) {
        m_media->setVisible(on[1]);
        if (on[1])
            m_media->show(m_msg);
    }
    if (m_album) {
        m_album->setVisible(on[2]);
        if (on[2])
            m_album->show(m_msg);
    }
    if (m_link) {
        m_link->setVisible(on[3]);
        if (on[3])
            m_link->show(m_msg);
    }
    if (m_poll) {
        m_poll->setVisible(on[4]);
        if (on[4])
            m_poll->show(m_msg);
    }
}
