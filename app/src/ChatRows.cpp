#include "ChatRows.h"

#include <bb/cascades/ActionItem>
#include <bb/cascades/ActionSet>
#include <bb/cascades/Container>
#include <bb/cascades/ContextMenuHandler>
#include <bb/cascades/DockLayout>
#include <bb/cascades/FontWeight>
#include <bb/cascades/Header>
#include <bb/cascades/HorizontalAlignment>
#include <bb/cascades/ImageView>
#include <bb/cascades/ImageViewLoadEffect>
#include <bb/cascades/Label>
#include <bb/cascades/ListView>
#include <bb/cascades/ScalingMethod>
#include <bb/cascades/SystemDefaults>
#include <bb/cascades/TextAlign>
#include <bb/cascades/TextFormat>
#include <bb/cascades/TextStyleDefinition>
#include <bb/cascades/UIConfig>
#include <bb/cascades/UIPalette>
#include <bb/cascades/VerticalAlignment>

#include "CardStyle.h"
#include "ChatListModel.h"
#include "Markup.h"
#include "Settings.h"

using namespace bb::cascades;

namespace {
// The QML file's name: the translations of ChatRow.qml's strings (tengram_it.ts) apply.
const char *const kContext = "ChatRow";

// icon 0: no picture.
void setItem(ActionItem *item, const char *title, const char *icon = 0)
{
    item->setTitle(Card::tr(kContext, title));
    if (icon)
        item->setImageSource(Card::url(icon));
}
}

ChatRows::ChatRows(QObject *parent)
    : ListItemProvider(parent)
{
}

VisualNode *ChatRows::createItem(ListView *list, const QString &type)
{
    // A text size, interface or Shape change refills every row: its sizes and tile were set from the old one.
    if (m_list != list) {
        m_list = list;
        if (!connect(list, SIGNAL(textScaleChanged()), this, SIGNAL(itemUpdatesNeeded())))
            qWarning("chat rows: the list has no textScaleChanged");
        if (!connect(list, SIGNAL(compactChanged()), this, SIGNAL(itemUpdatesNeeded())))
            qWarning("chat rows: the list has no compactChanged");
        if (!connect(list, SIGNAL(roundChanged()), this, SIGNAL(itemUpdatesNeeded())))
            qWarning("chat rows: the list has no roundChanged");
    }
    if (type == "header")
        return new Header();
    return new ChatRow(list);
}

void ChatRows::updateItem(ListView *, VisualNode *listItem, const QString &type,
                          const QVariantList &, const QVariant &data)
{
    if (type == "header")
        static_cast<Header *>(listItem)->setTitle(data.toString());
    else
        static_cast<ChatRow *>(listItem)->show(data.toMap());
}

ChatRow::ChatRow(ListView *list)
    : m_list(list)
    , m_ui(list->ui())
    , m_pressed(false)
    , m_styled(false)
    , m_scale(0)
    , m_compact(false)
    , m_topics(list->property("topicList").toBool())
    , m_bottom(0)
    , m_at(0)
    , m_atDisc(0)
    , m_menu(0)
    , m_readItem(0)
    , m_pinItem(0)
    , m_upItem(0)
    , m_downItem(0)
    , m_muteItem(0)
    , m_translateItem(0)
    , m_archiveItem(0)
{
    // The row's own node and its root: a list item is as wide as its content unless it fills.
    setHorizontalAlignment(HorizontalAlignment::Fill);
    m_root = new Container();
    m_root->setHorizontalAlignment(HorizontalAlignment::Fill);

    // The row, its tile and the initials grow with the text size, so a row keeps Normal's
    // proportions: at 140 % the two lines filled the 9.4 du row edge to edge (Q10, 2026-11-01). Their
    // sizes are set by show().
    m_row = new Container();
    m_row->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_row->setLayout(Card::across());

    // The chat's picture (the engine's preview file, chat.photo) over the initials tile. The file's
    // name has the picture's id, so a new picture is a new path and reloads.
    m_tile = new Container();
    m_tile->setVerticalAlignment(VerticalAlignment::Center);
    m_tile->setLayout(new DockLayout());
    // Round mode's colour (the user's ask, 2026-11-19): a Container's background can't be round, so the tile's colour is
    // a white disc tinted by show(), under the picture, shown while there is none.
    m_disc = new ImageView();
    m_disc->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_disc->setVerticalAlignment(VerticalAlignment::Fill);
    m_disc->setScalingMethod(ScalingMethod::AspectFit);
    m_disc->setLoadEffect(ImageViewLoadEffect::None);
    m_disc->setImageSource(Card::url("asset:///images/circle.png"));
    m_photo = new ImageView();
    m_photo->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_photo->setVerticalAlignment(VerticalAlignment::Fill);
    m_photo->setScalingMethod(ScalingMethod::AspectFill);
    m_photo->setLoadEffect(ImageViewLoadEffect::None);
    m_initials = new Label();
    m_initials->setHorizontalAlignment(HorizontalAlignment::Center);
    m_initials->setVerticalAlignment(VerticalAlignment::Center);
    m_tile->add(m_disc);
    m_tile->add(m_photo);
    m_tile->add(m_initials);

    m_col = new Container();
    m_col->setVerticalAlignment(VerticalAlignment::Center);
    Card::quota(m_col, 1);

    Container *top = new Container();
    top->setLayout(Card::across());
    m_name = new Label();
    m_name->setVerticalAlignment(VerticalAlignment::Center);
    Card::quota(m_name, 1);
    // A secret chat's, before its name, in Telegram's secret green; its size is set by show().
    m_secret = new ImageView();
    m_secret->setVerticalAlignment(VerticalAlignment::Center);
    m_secret->setLeftMargin(0);
    m_secret->setRightMargin(m_ui->du(0.5));
    m_secret->setScalingMethod(ScalingMethod::AspectFit);
    m_secret->setImageSource(Card::url("asset:///images/lock.png"));
    // A closed topic's.
    m_lock = new ImageView();
    m_lock->setVerticalAlignment(VerticalAlignment::Center);
    m_lock->setImageSource(Card::url("asset:///images/lock.png"));
    m_pin = new ImageView();
    m_pin->setVerticalAlignment(VerticalAlignment::Center);
    m_pin->setImageSource(Card::url("asset:///images/pin.png"));
    m_mute = new ImageView();
    m_mute->setVerticalAlignment(VerticalAlignment::Center);
    m_mute->setImageSource(Card::url("asset:///images/mute.png"));
    // Grows with the text size, as the name and preview do (MessageCard.cpp's time); its size is
    // set by show().
    m_time = new Label();
    m_time->setVerticalAlignment(VerticalAlignment::Center);
    m_time->textStyle()->setBase(SystemDefaults::TextStyles::smallText());
    m_time->textStyle()->setFontSize(FontSize::PercentageValue);
    top->add(m_secret);
    top->add(m_name);
    top->add(m_lock);
    top->add(m_pin);
    top->add(m_mute);
    top->add(m_time);

    Container *bottom = new Container();
    m_bottom = bottom;
    bottom->setLayout(Card::across());
    // Our last message's state (docs/protocol.md): sending, sent, delivered, read.
    m_ticks = new ImageView();
    m_ticks->setVerticalAlignment(VerticalAlignment::Center);
    m_ticks->setRightMargin(0);
    m_bang = new Label();
    m_bang->setVerticalAlignment(VerticalAlignment::Center);
    m_bang->setText("!");
    m_bang->textStyle()->setBase(SystemDefaults::TextStyles::subtitleText());
    m_bang->textStyle()->setFontWeight(FontWeight::Bold);
    m_bang->textStyle()->setColor(Card::color("#FF3333"));
    m_preview = new Label();
    m_preview->setVerticalAlignment(VerticalAlignment::Center);
    // Closer to the ticks than Cascades' default gap (about 20 px between labels): the user's, Q10,
    // 2026-11-02.
    m_preview->setLeftMargin(m_ui->du(1));
    Card::quota(m_preview, 1);
    // The unread count, grey while muted, as WhatsApp does.
    // Round mode (the user's ask, 2026-11-19): a pill, pill_s.amd stretched under the count and tinted, as the service
    // lines' pill (ServiceCard); the count's paddings are on an inner Container, so the pill covers them too.
    m_badge = new Container();
    m_badge->setVerticalAlignment(VerticalAlignment::Center);
    m_badge->setLayout(new DockLayout());
    m_badgePill = new ImageView();
    m_badgePill->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_badgePill->setVerticalAlignment(VerticalAlignment::Fill);
    m_badgePill->setScalingMethod(ScalingMethod::Fill);
    m_badgePill->setLoadEffect(ImageViewLoadEffect::None);
    m_badgePill->setImageSource(Card::url("asset:///images/pill_s.amd"));
    m_badgePill->setVisible(false);
    // The count fills the badge and centres its text: a Label's box is wider than one digit, and its text sat on the
    // box's left, off the disc's centre (the user, 2026-11-20).
    Container *counted = new Container();
    counted->setHorizontalAlignment(HorizontalAlignment::Fill);
    counted->setVerticalAlignment(VerticalAlignment::Center);
    counted->setLeftPadding(m_ui->du(0.8));
    counted->setRightPadding(m_ui->du(0.8));
    m_count = new Label();
    m_count->setHorizontalAlignment(HorizontalAlignment::Center);
    m_count->textStyle()->setBase(SystemDefaults::TextStyles::smallText());
    m_count->textStyle()->setFontWeight(FontWeight::Bold);
    m_count->textStyle()->setTextAlign(TextAlign::Center);
    counted->add(m_count);
    m_badge->add(m_badgePill);
    m_badge->add(counted);
    // Marked unread (-1): a dot in the badge's place, no number.
    m_dot = new Container();
    m_dot->setVerticalAlignment(VerticalAlignment::Center);
    m_dot->setLeftMargin(m_ui->du(0.8));
    m_dot->setPreferredSize(m_ui->du(1.6), m_ui->du(1.6));
    m_dot->setLayout(new DockLayout());
    m_dotDisc = new ImageView();
    m_dotDisc->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_dotDisc->setVerticalAlignment(VerticalAlignment::Fill);
    m_dotDisc->setScalingMethod(ScalingMethod::AspectFit);
    m_dotDisc->setLoadEffect(ImageViewLoadEffect::None);
    m_dotDisc->setImageSource(Card::url("asset:///images/circle.png"));
    m_dotDisc->setVisible(false);
    m_dot->add(m_dotDisc);
    bottom->add(m_ticks);
    bottom->add(m_bang);
    bottom->add(m_preview);
    bottom->add(m_badge);
    bottom->add(m_dot);

    m_col->add(top);
    m_col->add(bottom);
    m_row->add(m_tile);
    m_row->add(m_col);

    m_sep = new Container();
    m_sep->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_sep->setPreferredHeight(m_ui->px(2));

    m_root->add(m_row);
    m_root->add(m_sep);
    setRoot(m_root);

    // The long-press menu (step 5.3): the work is the list's. The actions are made at the first
    // populating, which comes just before it opens, and their titles set at each: ActionItem has no
    // visible, so each follows the row's state. A topic has none.
    if (m_topics)
        return;
    m_menu = new ActionSet();
    addActionSet(m_menu);
    ContextMenuHandler *handler = new ContextMenuHandler(this);
    setContextMenuHandler(handler);
    connect(handler, SIGNAL(populating(bb::cascades::ContextMenuShowingEvent *)), this,
            SLOT(onPopulating()));
}

// Everything the chat, the text size and the theme's colours decide, set again on each recycle.
void ChatRow::show(const QVariantMap &chat)
{
    m_chat = chat;
    const float scale = Card::textScale(m_list);
    const bool compact = m_list->property("compact").toBool();
    UIPalette *palette = m_ui->palette();
    const Color quiet = palette->secondaryTextOnPlain();
    const Color primary = palette->primary();
    const int unread = m_chat.value("unread").toInt();
    const bool muted = flag("muted");
    const bool out = flag("out");
    const int state = m_chat.value("state").toInt();

    // What only the text size and the interface decide, set again when they change (a recycle
    // keeps it), as MessageCard::restyle does. Normal: a bigger picture and more room around it, the text as
    // Compact's, which is the 9.4 du row from before.
    if (!m_styled || scale != m_scale || compact != m_compact) {
        m_styled = true;
        m_scale = scale;
        m_compact = compact;
        const float side = compact ? 1.5 : 2;
        const float tile = compact ? 7 : 9; // Normal: 8.2 until the user asked for a bigger picture and a taller row, 2026-11-19
        m_row->setLeftPadding(m_ui->du(side));
        m_row->setRightPadding(m_ui->du(side));
        m_col->setLeftMargin(m_ui->du(side));
        m_row->setMinHeight(m_ui->du((compact ? 9.4 : 12.6) * scale));
        m_tile->setPreferredSize(m_ui->du(tile * scale), m_ui->du(tile * scale));
        Card::style(m_initials, m_list, qRound(118 * scale));
        m_initials->textStyle()->setColor(Color::White);
        Card::style(m_name, m_list, qRound(115 * scale));
        m_secret->setPreferredSize(m_ui->du(2.6 * scale), m_ui->du(2.6 * scale));
        m_time->textStyle()->setFontSizeValue(qRound(100 * scale));
        Card::style(m_preview, m_list, qRound(90 * scale));
    }

    paintBackground();

    // The tile: the picture, or the initials on the chat's colour (a topic with none: the accent). Given the
    // picture again only when it changed, as a binding would. Round mode: a disc and the picture's round copy
    // (roundPhoto; the square picture if it can't be made), but a forum's topics keep their square tiles.
    const bool round = !m_topics && Card::isRound(m_list);
    const QString photo = str("photo");
    const Color tint = str("color").isEmpty() ? primary : Card::color(str("color"));
    if (round) {
        m_tile->resetBackground();
        m_disc->setFilterColor(tint);
    } else {
        m_tile->setBackground(tint);
    }
    m_disc->setVisible(round && photo.isEmpty());
    QString shown = photo;
    if (round && !photo.isEmpty()) {
        const QString disc = roundPhoto(photo);
        if (!disc.isEmpty())
            shown = disc;
    }
    if (shown != m_photoPath) {
        m_photoPath = shown;
        m_photo->setImageSource(shown.isEmpty() ? QUrl() : QUrl(QString::fromLatin1("file://") + shown));
    }
    m_photo->setVisible(!photo.isEmpty());
    m_initials->setVisible(photo.isEmpty());
    m_initials->setText(str("initials"));

    m_name->setText(str("name"));
    // A secret chat: the lock and the name in the theme's secret green (the list holds it); else the theme's own text.
    const bool secret = m_chat.value("secret").toInt() > 0;
    m_secret->setVisible(secret);
    if (secret) {
        const Color green = Card::color(Card::secretColor(m_list));
        m_secret->setFilterColor(green);
        m_name->textStyle()->setColor(green);
    } else {
        m_name->textStyle()->resetColor();
    }
    if (m_topics)
        m_name->textStyle()->setFontWeight(unread != 0 ? FontWeight::Bold : FontWeight::Normal);
    m_lock->setVisible(flag("closed"));
    m_lock->setFilterColor(quiet);
    m_pin->setVisible(flag("pinned"));
    m_pin->setFilterColor(quiet);
    m_mute->setVisible(muted);
    m_mute->setFilterColor(quiet);
    m_time->setText(str("time"));
    m_time->textStyle()->setColor(unread != 0 ? primary : quiet);

    const bool ticked = out && state < 4;
    m_ticks->setVisible(ticked);
    if (ticked) {
        m_ticks->setImageSource(Card::url(state == 0 ? "asset:///images/clock.png"
                                    : state == 1 ? "asset:///images/tick.png" : "asset:///images/ticks.png"));
        m_ticks->setFilterColor(state == 3 ? primary : quiet);
    }
    m_bang->setVisible(out && state == 4);

    // Without U+20E3, a keycap's frame, as the message card's body (MessageCard.cpp).
    m_preview->textStyle()->setColor(quiet);
    // WhatsApp's styles show in the preview as in the message (Markup.h); a line without any stays plain.
    const QString line = Card::noKeycaps(str("preview"));
    const QString html = Markup::html(line);
    // A group's sender in the accent colour, as Telegram's list has it (the user's ask, 2026-11-18). #24A1DE is the
    // app's fixed primary (Settings.cpp).
    // ponytail: the first ": " is taken for the sender's; a service line holding ": " would colour its start. An engine sender field fixes it.
    const int colon = m_chat.value("kind").toInt() == 1 ? line.indexOf(": ") : -1;
    if (colon > 0) {
        const QString rest = line.mid(colon + 2);
        const QString restHtml = Markup::html(rest);
        m_preview->setTextFormat(TextFormat::Html);
        m_preview->setText("<span style=\"color:#24A1DE\">" + Markup::escaped(line.left(colon + 1)) + "</span> "
                           + (restHtml.isEmpty() ? Markup::escaped(rest) : restHtml));
    } else {
        m_preview->setTextFormat(html.isEmpty() ? TextFormat::Plain : TextFormat::Html);
        m_preview->setText(html.isEmpty() ? line : html);
    }

    const Color badge = muted ? palette->plain() : primary;
    // Round mode: the colour is the pill's and the disc's; at least as wide as tall, so one digit is a disc.
    const bool roundBadge = Card::isRound(m_list);
    m_badge->setVisible(unread > 0);
    m_badgePill->setVisible(roundBadge);
    m_badgePill->setFilterColor(badge);
    m_dotDisc->setVisible(roundBadge);
    m_dotDisc->setFilterColor(badge);
    if (roundBadge) {
        m_badge->resetBackground();
        m_badge->setMinWidth(m_ui->du(3.2 * m_scale));
        m_badge->setMinHeight(m_ui->du(3.2 * m_scale));
        m_dot->resetBackground();
    } else {
        m_badge->setBackground(badge);
        m_badge->resetMinWidth();
        m_badge->resetMinHeight();
        m_dot->setBackground(badge);
    }
    if (unread > 0) {
        m_count->setText(QString::number(unread));
        m_count->textStyle()->setColor(muted ? palette->textOnPlain() : palette->textOnPrimary());
    }
    m_dot->setVisible(unread < 0);
    // The @ is a disc in Round mode, a square otherwise, as the count; the count's size whatever the shape.
    const bool at = m_chat.value("mentions").toInt() > 0;
    if (at && !m_at)
        makeAt();
    if (m_at) {
        m_at->setVisible(at);
        m_atDisc->setVisible(roundBadge);
        m_atDisc->setFilterColor(primary);
        if (roundBadge)
            m_at->resetBackground();
        else
            m_at->setBackground(primary);
        m_at->setPreferredSize(m_ui->du(3.2 * m_scale), m_ui->du(3.2 * m_scale));
    }

    m_sep->setBackground(palette->plain());
}

// The @ badge, made when the row first has a mention: few chats ever do.
void ChatRow::makeAt()
{
    // An unread mention or reply to us (chats.mentions): "@" on the primary colour before the count, as Telegram's, muted or
    // not, since the Hub notifies it in a muted chat too (the user, 2026-11-20). As big as the count's disc.
    m_at = new Container();
    m_at->setVerticalAlignment(VerticalAlignment::Center);
    m_at->setLeftMargin(m_ui->du(0.8));
    m_at->setLayout(new DockLayout());
    m_atDisc = new ImageView();
    m_atDisc->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_atDisc->setVerticalAlignment(VerticalAlignment::Fill);
    m_atDisc->setScalingMethod(ScalingMethod::AspectFit);
    m_atDisc->setLoadEffect(ImageViewLoadEffect::None);
    m_atDisc->setImageSource(Card::url("asset:///images/circle.png"));
    m_atDisc->setVisible(false);
    Label *atSign = new Label();
    atSign->setHorizontalAlignment(HorizontalAlignment::Center);
    atSign->setVerticalAlignment(VerticalAlignment::Center);
    atSign->setText("@");
    atSign->textStyle()->setBase(SystemDefaults::TextStyles::smallText());
    atSign->textStyle()->setFontWeight(FontWeight::Bold);
    atSign->textStyle()->setTextAlign(TextAlign::Center);
    atSign->textStyle()->setColor(m_ui->palette()->textOnPrimary());
    m_at->add(m_atDisc);
    m_at->add(atSign);
    // Before the count's badge.
    m_bottom->insert(m_bottom->indexOf(m_badge), m_at);
}

// Pressed: the page's pressColor, else none (QML had Color.Transparent).
void ChatRow::paintBackground()
{
    if (m_pressed)
        m_root->setBackground(Card::color(Card::pressColor(m_list)));
    else
        m_root->resetBackground();
}

void ChatRow::select(bool)
{
}

void ChatRow::activate(bool activated)
{
    m_pressed = activated;
    paintBackground();
}

void ChatRow::reset(bool, bool activated)
{
    m_pressed = activated;
    paintBackground();
}

ActionItem *ChatRow::action(const char *slot)
{
    ActionItem *item = new ActionItem();
    connect(item, SIGNAL(triggered()), this, slot);
    m_menu->add(item);
    return item;
}

void ChatRow::makeActions()
{
    m_readItem = action(SLOT(onRead()));
    m_pinItem = action(SLOT(onPin()));
    m_upItem = action(SLOT(onUp()));
    m_downItem = action(SLOT(onDown()));
    m_muteItem = action(SLOT(onMute()));
    m_translateItem = action(SLOT(onTranslate()));
    m_archiveItem = action(SLOT(onArchive()));
}

// Move up and Move down sit under Pin only for a pinned chat: ActionItem has no visible, so they leave the menu and
// come back (the ActionSet keeps the ones it let go of and deletes them with itself).
void ChatRow::showMoves(bool on)
{
    ActionItem *const items[] = {m_upItem, m_downItem};
    for (int i = 0; i < 2; ++i) {
        const bool in = m_menu->indexOf(items[i]) >= 0;
        if (on && !in)
            m_menu->insert(2 + i, items[i]);
        else if (!on && in)
            m_menu->remove(items[i]);
    }
}

// "Mute…" is UTF-8 in the source, as the .ts has it: main.cpp sets UTF-8 as the codec for tr.
void ChatRow::onPopulating()
{
    if (!m_readItem)
        makeActions();
    m_menu->setTitle(str("name"));
    const bool channels = m_list->property("channelList").toBool();
    const bool archived = m_list->property("archivedList").toBool();
    const bool unread = m_chat.value("unread").toInt() != 0;

    setItem(m_readItem, unread ? QT_TRANSLATE_NOOP("ChatRow", "Mark as read") : QT_TRANSLATE_NOOP("ChatRow", "Mark as unread"),
            unread ? "file:///usr/share/icons/bb_action_markread.png"
                   : "file:///usr/share/icons/bb_action_markunread.png");
    setItem(m_pinItem, flag("pinned") ? QT_TRANSLATE_NOOP("ChatRow", "Unpin") : QT_TRANSLATE_NOOP("ChatRow", "Pin"), "asset:///images/pin_action.png");
    m_pinItem->setEnabled(!channels);
    // Move up and down: for a pinned chat of the main list, off at the top and the bottom of the pins.
    const bool movable = flag("pinned") && !archived && !channels;
    showMoves(movable);
    if (movable) {
        ChatListModel *model = qobject_cast<ChatListModel *>(m_list->dataModel());
        const QStringList pins = model ? model->pinnedJids() : QStringList();
        const int at = pins.indexOf(str("jid"));
        setItem(m_upItem, QT_TRANSLATE_NOOP("ChatRow", "Move up"));
        setItem(m_downItem, QT_TRANSLATE_NOOP("ChatRow", "Move down"));
        m_upItem->setEnabled(at > 0);
        m_downItem->setEnabled(at >= 0 && at < pins.size() - 1);
    }
    setItem(m_muteItem, flag("muted") ? QT_TRANSLATE_NOOP("ChatRow", "Unmute") : QT_TRANSLATE_NOOP("ChatRow", "Mute\xE2\x80\xA6"), "asset:///images/mute_action.png");
    m_muteItem->setEnabled(!channels);
    // Off in a secret chat (nothing of it goes to a service) and for a forum's topics, which are not chats.
    setItem(m_translateItem, QT_TRANSLATE_NOOP("ChatRow", "Translate\xE2\x80\xA6"), "asset:///images/translate_action.png");
    m_translateItem->setEnabled(m_chat.value("secret").toInt() == 0 && !m_topics);
    if (channels)
        setItem(m_archiveItem, QT_TRANSLATE_NOOP("ChatRow", "Unfollow"), "file:///usr/share/icons/bb_action_delete.png");
    else
        setItem(m_archiveItem, archived ? QT_TRANSLATE_NOOP("ChatRow", "Unarchive") : QT_TRANSLATE_NOOP("ChatRow", "Archive"),
                "file:///usr/share/icons/bb_action_archive.png");
}

void ChatRow::onRead() { Card::call(m_list, "toggleUnread", m_chat); }
void ChatRow::onPin() { Card::call(m_list, "togglePin", m_chat); }
void ChatRow::onUp() { Card::call(m_list, "movePin", m_chat, -1); }
void ChatRow::onDown() { Card::call(m_list, "movePin", m_chat, 1); }
void ChatRow::onMute() { Card::call(m_list, "toggleMute", m_chat); }
void ChatRow::onTranslate() { Card::call(m_list, "pickTranslate", m_chat); }
void ChatRow::onArchive() { Card::call(m_list, "toggleArchive", m_chat); }
