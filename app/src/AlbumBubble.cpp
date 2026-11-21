#include "AlbumBubble.h"

#include <bb/cascades/ActivityIndicator>
#include <bb/cascades/Container>
#include <bb/cascades/DockLayout>
#include <bb/cascades/FontSize>
#include <bb/cascades/FontWeight>
#include <bb/cascades/HorizontalAlignment>
#include <bb/cascades/Image>
#include <bb/cascades/ImageView>
#include <bb/cascades/ImageViewLoadEffect>
#include <bb/cascades/Label>
#include <bb/cascades/ListView>
#include <bb/cascades/ScalingMethod>
#include <bb/cascades/TapHandler>
#include <bb/cascades/TextFormat>
#include <bb/cascades/TextStyleDefinition>
#include <bb/cascades/UIConfig>
#include <bb/cascades/UIPalette>
#include <bb/cascades/VerticalAlignment>
#include <QObject>
#include <QVariantList>

#include "CardStyle.h"

using namespace bb::cascades;

// One square of an album (step 6.5g), mirroring AlbumTile.qml: the member's thumbnail, aspect fill,
// and a play mark on a video as MediaBubble draws it. The size is not set here: the bubble sets it.
// member is a message map; its thumb is always an Image, empty without a thumbnail, so a recycled
// tile never shows the picture of the row it showed before. more, "+N", darkens the tile under that
// text (the one that opens the page). The bubble owns it and takes its tap, by the tile's number.
// No Q_OBJECT: it has no signals or slots.
class AlbumTile : public QObject
{
public:
    AlbumTile(UIConfig *ui, ListView *list, int index, QObject *bubble)
        : QObject(bubble)
        , m_ui(ui)
        , m_list(list)
    {
        // The page's colour behind a tile with no thumbnail is set by show().
        m_box = new Container();
        m_box->setLayout(new DockLayout());
        TapHandler *tap = new TapHandler(m_box);
        tap->setProperty("tile", index);
        m_box->addGestureHandler(tap);
        connect(tap, SIGNAL(tapped(bb::cascades::TapEvent *)), bubble, SLOT(onTap()));

        m_thumb = new ImageView();
        m_thumb->setHorizontalAlignment(HorizontalAlignment::Fill);
        m_thumb->setVerticalAlignment(VerticalAlignment::Fill);
        m_thumb->setScalingMethod(ScalingMethod::AspectFill);
        m_thumb->setLoadEffect(ImageViewLoadEffect::None);

        m_play = new ImageView();
        m_play->setHorizontalAlignment(HorizontalAlignment::Center);
        m_play->setVerticalAlignment(VerticalAlignment::Center);
        m_play->setPreferredSize(m_ui->du(6), m_ui->du(6));
        m_play->setImageSource(Card::url("asset:///images/play.png"));
        m_play->setOpacity(0.85f);

        // The member being fetched shows a spinner, as in MediaBubble.
        m_spin = new ActivityIndicator();
        m_spin->setHorizontalAlignment(HorizontalAlignment::Center);
        m_spin->setVerticalAlignment(VerticalAlignment::Center);

        m_more = new Container();
        m_more->setHorizontalAlignment(HorizontalAlignment::Fill);
        m_more->setVerticalAlignment(VerticalAlignment::Fill);
        m_more->setBackground(Card::color("#99000000"));
        m_more->setLayout(new DockLayout());
        m_plus = new Label();
        m_plus->setHorizontalAlignment(HorizontalAlignment::Center);
        m_plus->setVerticalAlignment(VerticalAlignment::Center);
        m_plus->setTextFormat(TextFormat::Plain);
        m_plus->textStyle()->setFontSize(FontSize::XXLarge);
        m_plus->textStyle()->setFontWeight(FontWeight::Bold);
        m_plus->textStyle()->setColor(Color::White);
        m_more->add(m_plus);

        m_box->add(m_thumb);
        m_box->add(m_play);
        m_box->add(m_spin);
        m_box->add(m_more);
    }

    Container *box() const { return m_box; }

    void show(const QVariantMap &member, const QString &more)
    {
        m_id = member.value("id").toString();
        m_box->setBackground(Card::color(Card::pageColor(m_list)));
        m_thumb->setImage(member.value("thumb").value<Image>());
        m_play->setVisible(member.value("media").toString() == "video");
        m_more->setVisible(!more.isEmpty());
        if (!more.isEmpty())
            m_plus->setText(more);
    }

    // loadingId is the chat list's mediaLoadingId.
    void load(const QString &loadingId)
    {
        m_spin->setRunning(loadingId == m_id);
    }

private:
    UIConfig *m_ui;
    ListView *m_list;
    QString m_id;
    Container *m_box;
    ImageView *m_thumb;
    ImageView *m_play;
    ActivityIndicator *m_spin;
    Container *m_more;
    Label *m_plus;
};

AlbumBubble::AlbumBubble(ListView *list)
    : m_list(list)
    , m_ui(list->ui())
    , m_size(0)
    , m_second(0)
    , m_wide(false)
{
    // As MediaBubble's picture: no margin above, air above and below.
    setTopMargin(0);
    Container *root = new Container();
    root->setTopPadding(m_ui->du(1));
    root->setBottomPadding(m_ui->du(1));

    // The tiles, and over them the time's corner, 1 du in from the last tile's edges as on a photo.
    Container *dock = new Container();
    dock->setLayout(new DockLayout());
    Container *grid = new Container();
    dock->add(grid);
    m_stamp = new Container();
    m_stamp->setHorizontalAlignment(HorizontalAlignment::Right);
    m_stamp->setVerticalAlignment(VerticalAlignment::Bottom);
    m_stamp->setRightPadding(m_ui->du(1));
    m_stamp->setBottomPadding(m_ui->du(1));
    dock->add(m_stamp);
    root->add(dock);

    // Two rows of two tiles, a gap apart; show() sizes them.
    const float gap = m_ui->du(0.5);
    for (int row = 0; row < 2; ++row) {
        Container *tiles = new Container();
        tiles->setLayout(Card::across());
        if (row == 1)
            tiles->setTopMargin(gap);
        for (int column = 0; column < 2; ++column) {
            AlbumTile *tile = new AlbumTile(m_ui, m_list, row * 2 + column, this);
            if (column == 1)
                tile->box()->setLeftMargin(gap);
            tiles->add(tile->box());
            m_tiles[row * 2 + column] = tile;
        }
        grid->add(tiles);
        if (row == 1)
            m_second = tiles;
    }
    setRoot(root);

    // The fetch, as the QML bound it.
    if (!connect(list, SIGNAL(mediaLoadingIdChanged()), this, SLOT(refresh())))
        qWarning("cards: the list has no mediaLoadingIdChanged");
}

// Never empty: a row that stops being an album meanwhile falls back on its own map.
QVariantMap AlbumBubble::memberAt(int i) const
{
    const QVariantList album = m_msg.value("album").toList();
    return i < album.size() ? album.at(i).toMap() : m_msg;
}

void AlbumBubble::show(const QVariantMap &msg)
{
    m_msg = msg;
    // Across the card inside its side padding (the user, 2026-11-08), which grows with the text size.
    const float gap = m_ui->du(0.5);
    const float size = (Card::width(m_ui) - 2 * Card::side(m_ui, Card::textScale(m_list), Card::isRound(m_list)) - gap) / 2;
    const int count = m_msg.value("albumCount").toInt();
    // Three: the third tile is as wide as the two above it.
    const bool wide = count == 3;
    if (size != m_size || wide != m_wide) {
        m_size = size;
        m_wide = wide;
        for (int i = 0; i < 4; ++i) {
            m_tiles[i]->box()->setPreferredWidth(i == 2 && wide ? 2 * size + gap : size);
            m_tiles[i]->box()->setPreferredHeight(size);
        }
    }
    m_second->setVisible(count > 2);
    m_tiles[3]->box()->setVisible(count > 3);
    for (int i = 0; i < 4; ++i)
        m_tiles[i]->show(memberAt(i), i == 3 && count > 4 ? "+" + QString::number(count - 3) : QString());
    refresh();
}

void AlbumBubble::refresh()
{
    if (m_msg.isEmpty())
        return;
    const QString loading = m_list->property("mediaLoadingId").toString();
    for (int i = 0; i < 4; ++i)
        m_tiles[i]->load(loading);
}

// A tile opens its member; the fourth, the page of all of them, or just its member when there are four.
void AlbumBubble::onTap()
{
    const int i = sender()->property("tile").toInt();
    if (i == 3 && m_msg.value("albumCount").toInt() > 4)
        Card::call(m_list, "showAlbum", m_msg.value("album"));
    else
        Card::call(m_list, "openMedia", memberAt(i));
}
