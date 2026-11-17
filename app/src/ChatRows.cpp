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
#include <bb/cascades/TextFormat>
#include <bb/cascades/TextStyleDefinition>
#include <bb/cascades/UIConfig>
#include <bb/cascades/UIPalette>
#include <bb/cascades/VerticalAlignment>

#include "CardStyle.h"
#include "Markup.h"

using namespace bb::cascades;

namespace {
// The QML file's name: the translations of ChatRow.qml's strings (hyperberry_it.ts) apply.
const char *const kContext = "ChatRow";
// The same for CallRow.qml's, the Calls tab's row.
const char *const kCallContext = "CallRow";

void setItem(ActionItem *item, const char *title, const char *icon, const char *context = kContext)
{
    item->setTitle(Card::tr(context, title));
    item->setImageSource(Card::url(icon));
}
}

ChatRows::ChatRows(QObject *parent)
    : ListItemProvider(parent)
{
}

VisualNode *ChatRows::createItem(ListView *list, const QString &type)
{
    // A text size change refills every row: its sizes were set from the old one.
    if (m_list != list) {
        m_list = list;
        if (!connect(list, SIGNAL(textScaleChanged()), this, SIGNAL(itemUpdatesNeeded())))
            qWarning("chat rows: the list has no textScaleChanged");
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
    , m_calls(list->property("callRows").toBool())
    , m_pressed(false)
    , m_styled(false)
    , m_scale(0)
    , m_callItem(0)
    , m_readItem(0)
    , m_pinItem(0)
    , m_muteItem(0)
    , m_archiveItem(0)
    , m_openItem(0)
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
    m_row->setLeftPadding(m_ui->du(1.5));
    m_row->setRightPadding(m_ui->du(1.5));
    m_row->setLayout(Card::across());

    // The chat's picture (the engine's preview file, chat.photo) over the initials tile. The file's
    // name has the picture's id, so a new picture is a new path and reloads.
    m_tile = new Container();
    m_tile->setVerticalAlignment(VerticalAlignment::Center);
    m_tile->setLayout(new DockLayout());
    m_photo = new ImageView();
    m_photo->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_photo->setVerticalAlignment(VerticalAlignment::Fill);
    m_photo->setScalingMethod(ScalingMethod::AspectFill);
    m_photo->setLoadEffect(ImageViewLoadEffect::None);
    m_initials = new Label();
    m_initials->setHorizontalAlignment(HorizontalAlignment::Center);
    m_initials->setVerticalAlignment(VerticalAlignment::Center);
    m_tile->add(m_photo);
    m_tile->add(m_initials);

    Container *col = new Container();
    col->setVerticalAlignment(VerticalAlignment::Center);
    col->setLeftMargin(m_ui->du(1.5));
    Card::quota(col, 1);

    Container *top = new Container();
    top->setLayout(Card::across());
    m_name = new Label();
    m_name->setVerticalAlignment(VerticalAlignment::Center);
    Card::quota(m_name, 1);
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
    top->add(m_name);
    top->add(m_pin);
    top->add(m_mute);
    top->add(m_time);

    Container *bottom = new Container();
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
    m_badge = new Container();
    m_badge->setVerticalAlignment(VerticalAlignment::Center);
    m_badge->setLeftPadding(m_ui->du(0.8));
    m_badge->setRightPadding(m_ui->du(0.8));
    m_count = new Label();
    m_count->textStyle()->setBase(SystemDefaults::TextStyles::smallText());
    m_count->textStyle()->setFontWeight(FontWeight::Bold);
    m_badge->add(m_count);
    // Marked unread (-1): a dot in the badge's place, no number.
    m_dot = new Container();
    m_dot->setVerticalAlignment(VerticalAlignment::Center);
    m_dot->setLeftMargin(m_ui->du(0.8));
    m_dot->setPreferredSize(m_ui->du(1.6), m_ui->du(1.6));
    bottom->add(m_ticks);
    bottom->add(m_bang);
    bottom->add(m_preview);
    bottom->add(m_badge);
    bottom->add(m_dot);

    col->add(top);
    col->add(bottom);
    m_row->add(m_tile);
    m_row->add(col);

    m_sep = new Container();
    m_sep->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_sep->setPreferredHeight(m_ui->px(2));

    m_root->add(m_row);
    m_root->add(m_sep);
    setRoot(m_root);

    // The long-press menu (step 5.3): the work is the list's. The actions are made at the first
    // populating, which comes just before it opens, and their titles set at each: ActionItem has no
    // visible, so each follows the row's state.
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
    UIPalette *palette = m_ui->palette();
    const Color quiet = palette->secondaryTextOnPlain();
    const Color primary = palette->primary();
    const int unread = m_chat.value("unread").toInt();
    const bool muted = flag("muted");
    const bool out = flag("out");
    const int state = m_chat.value("state").toInt();

    // What only the text size decides, set again when it changed (a recycle keeps it), as MessageCard::restyle does.
    if (!m_styled || scale != m_scale) {
        m_styled = true;
        m_scale = scale;
        m_row->setMinHeight(m_ui->du(9.4 * scale));
        m_tile->setPreferredSize(m_ui->du(7 * scale), m_ui->du(7 * scale));
        Card::style(m_initials, m_list, qRound(118 * scale));
        m_initials->textStyle()->setColor(Color::White);
        Card::style(m_name, m_list, qRound(115 * scale));
        m_time->textStyle()->setFontSizeValue(qRound(100 * scale));
        Card::style(m_preview, m_list, qRound(90 * scale));
    }

    paintBackground();

    // The tile: the picture, or the initials on the chat's colour. Given the picture again only
    // when it changed, as a binding would.
    m_tile->setBackground(Card::color(str("color")));
    const QString photo = str("photo");
    if (photo != m_photoPath) {
        m_photoPath = photo;
        m_photo->setImageSource(photo.isEmpty() ? QUrl() : QUrl(QString::fromLatin1("file://") + photo));
    }
    m_photo->setVisible(!photo.isEmpty());
    m_initials->setVisible(photo.isEmpty());
    m_initials->setText(str("initials"));

    m_name->setText(str("name"));
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

    // Without U+20E3, a keycap's frame, as the message card's body (MessageCard.cpp). A call's row
    // has what the call was there, red when missed.
    m_preview->textStyle()->setColor(m_calls && flag("missed") ? Card::color("#FF3333") : quiet);
    // WhatsApp's styles show in the preview as in the message (Markup.h); a line without any stays plain.
    const QString line = m_calls ? str("detail") : Card::noKeycaps(str("preview"));
    const QString html = m_calls ? QString() : Markup::html(line);
    m_preview->setTextFormat(html.isEmpty() ? TextFormat::Plain : TextFormat::Html);
    m_preview->setText(html.isEmpty() ? line : html);

    const Color badge = muted ? palette->plain() : primary;
    m_badge->setVisible(unread > 0);
    m_badge->setBackground(badge);
    if (unread > 0) {
        m_count->setText(QString::number(unread));
        m_count->textStyle()->setColor(muted ? palette->textOnPlain() : palette->textOnPrimary());
    }
    m_dot->setVisible(unread < 0);
    m_dot->setBackground(badge);

    m_sep->setBackground(palette->plain());
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
    if (m_calls) {
        m_callItem = action(SLOT(onCall()));
        setItem(m_callItem, "Call", "file:///usr/share/icons/ic_phone.png", kCallContext);
        m_openItem = action(SLOT(onOpenChat()));
        setItem(m_openItem, "Open chat", "file:///usr/share/icons/bb_action_sendtextmessage.png", kCallContext);
        return;
    }
    // Call (the user's ask, 2026-11-07): a person's chat only; greyed for the others, since an
    // ActionItem can't hide.
    m_callItem = action(SLOT(onCall()));
    setItem(m_callItem, "Call", "file:///usr/share/icons/ic_phone.png");
    m_readItem = action(SLOT(onRead()));
    m_pinItem = action(SLOT(onPin()));
    m_muteItem = action(SLOT(onMute()));
    m_archiveItem = action(SLOT(onArchive()));
}

// "Mute…" is UTF-8 in the source, as the .ts has it: main.cpp sets UTF-8 as the codec for tr.
void ChatRow::onPopulating()
{
    if (!m_callItem)
        makeActions();
    m_menu->setTitle(str("name"));
    if (m_calls)
        return;
    const bool channels = m_list->property("channelList").toBool();
    const bool archived = m_list->property("archivedList").toBool();
    const bool unread = m_chat.value("unread").toInt() != 0;

    m_callItem->setEnabled(m_chat.value("kind").toInt() == 0 && m_list->property("linked").toBool());
    setItem(m_readItem, unread ? "Mark as read" : "Mark as unread",
            unread ? "file:///usr/share/icons/bb_action_markread.png"
                   : "file:///usr/share/icons/bb_action_markunread.png");
    setItem(m_pinItem, flag("pinned") ? "Unpin" : "Pin", "asset:///images/pin_action.png");
    m_pinItem->setEnabled(!channels);
    setItem(m_muteItem, flag("muted") ? "Unmute" : "Mute\xE2\x80\xA6", "asset:///images/mute_action.png");
    m_muteItem->setEnabled(!channels);
    if (channels)
        setItem(m_archiveItem, "Unfollow", "file:///usr/share/icons/bb_action_delete.png");
    else
        setItem(m_archiveItem, archived ? "Unarchive" : "Archive",
                "file:///usr/share/icons/bb_action_archive.png");
}

void ChatRow::onCall() { Card::call(m_list, m_calls ? "callBack" : "call", m_chat); }
void ChatRow::onRead() { Card::call(m_list, "toggleUnread", m_chat); }
void ChatRow::onPin() { Card::call(m_list, "togglePin", m_chat); }
void ChatRow::onMute() { Card::call(m_list, "toggleMute", m_chat); }
void ChatRow::onArchive() { Card::call(m_list, "toggleArchive", m_chat); }
void ChatRow::onOpenChat() { Card::call(m_list, "openChat", m_chat); }
