#include "StickerGrid.h"

#include <bb/cascades/ActivityIndicator>
#include <bb/cascades/Button>
#include <bb/cascades/Container>
#include <bb/cascades/DataModel>
#include <bb/cascades/DockLayout>
#include <bb/cascades/GridListLayout>
#include <bb/cascades/Header>
#include <bb/cascades/HorizontalAlignment>
#include <bb/cascades/Image>
#include <bb/cascades/ImageView>
#include <bb/cascades/ImageViewLoadEffect>
#include <bb/cascades/Label>
#include <bb/cascades/ListHeaderMode>
#include <bb/cascades/ListScrollStateHandler>
#include <bb/cascades/ListView>
#include <bb/cascades/LongPressHandler>
#include <bb/cascades/ScalingMethod>
#include <bb/cascades/ScrollAnimation>
#include <bb/cascades/ScrollMode>
#include <bb/cascades/ScrollView>
#include <bb/cascades/ScrollViewProperties>
#include <bb/cascades/TapHandler>
#include <bb/cascades/TextAlign>
#include <bb/cascades/TextFormat>
#include <bb/cascades/TextStyleDefinition>
#include <bb/cascades/UIConfig>
#include <bb/cascades/UIPalette>
#include <bb/cascades/VerticalAlignment>

#include "CardStyle.h"
#include "EngineLink.h"
#include "StickerPicker.h"

using namespace bb::cascades;

// The strings are StickersPage.qml's (tengram_it.ts and the rest): translate() takes the context its qsTr had.

StickerGrid::StickerGrid(Container *parent)
    : CustomControl(parent)
    , m_marked(0)
    , m_previewing(false)
{
    UIConfig *ui = this->ui();
    setHorizontalAlignment(HorizontalAlignment::Fill);
    setVerticalAlignment(VerticalAlignment::Fill);

    m_root = new Container();
    m_root->setLayout(new DockLayout());
    m_root->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_root->setVerticalAlignment(VerticalAlignment::Fill);

    // The column: the strip, the spinner while the first groups come, the grid.
    Container *column = new Container();
    column->setHorizontalAlignment(HorizontalAlignment::Fill);
    column->setVerticalAlignment(VerticalAlignment::Fill);

    ScrollView *scroller = new ScrollView();
    scroller->scrollViewProperties()->setScrollMode(ScrollMode::Horizontal);
    m_strip = new Container();
    m_strip->setLayout(Card::across());
    scroller->setContent(m_strip);

    m_spin = new ActivityIndicator();
    m_spin->setHorizontalAlignment(HorizontalAlignment::Center);
    m_spin->setTopMargin(ui->du(4));
    m_spin->setPreferredSize(ui->du(8), ui->du(8));
    m_spin->setVisible(false);

    // headerMode must be set: a grid's default (None) shows only the groups, as cells, and never their items (EmojiPicker.qml).
    m_list = new ListView();
    GridListLayout *cells = new GridListLayout();
    cells->setColumnCount(4);
    cells->setCellAspectRatio(1.0f);
    cells->setHeaderMode(ListHeaderMode::Standard);
    m_list->setLayout(cells);
    m_list->setListItemProvider(new StickerCells(this)); // the list owns it
    Card::quota(m_list, 1);
    connect(m_list, SIGNAL(triggered(QVariantList)), this, SLOT(onTriggered(QVariantList)));
    ListScrollStateHandler *scroll = new ListScrollStateHandler(m_list);
    scroll->setParent(m_list);
    connect(scroll, SIGNAL(firstVisibleItemChanged(QVariantList)), this, SLOT(onFirstVisible(QVariantList)));

    column->add(scroller);
    column->add(m_spin);
    column->add(m_list);

    m_empty = new Label();
    m_empty->setHorizontalAlignment(HorizontalAlignment::Center);
    m_empty->setVerticalAlignment(VerticalAlignment::Center);
    m_empty->setLeftMargin(ui->du(4));
    m_empty->setRightMargin(ui->du(4));
    m_empty->setMultiline(true);
    m_empty->textStyle()->setTextAlign(TextAlign::Center);
    m_empty->textStyle()->setColor(ui->palette()->secondaryTextOnPlain());
    m_empty->setText(QCoreApplication::translate("StickersPage",
                                                 "No stickers yet. Add sticker sets in Telegram on another device."));
    m_empty->setVisible(false);

    // The preview: over the page, dimmed. A tap on the dim closes it; the dim is a sibling of the column of buttons, so their
    // taps never reach it.
    m_overlay = new Container();
    m_overlay->setLayout(new DockLayout());
    m_overlay->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_overlay->setVerticalAlignment(VerticalAlignment::Fill);
    m_overlay->setVisible(false);
    Container *dim = new Container();
    dim->setHorizontalAlignment(HorizontalAlignment::Fill);
    dim->setVerticalAlignment(VerticalAlignment::Fill);
    dim->setBackground(Color::fromARGB(0xB3000000));
    TapHandler *tap = new TapHandler(dim);
    dim->addGestureHandler(tap);
    connect(tap, SIGNAL(tapped(bb::cascades::TapEvent *)), this, SLOT(closePreview()));

    Container *box = new Container();
    box->setHorizontalAlignment(HorizontalAlignment::Fill);
    box->setVerticalAlignment(VerticalAlignment::Center);
    box->setLeftPadding(ui->du(2));
    box->setRightPadding(ui->du(2));
    m_previewView = new ImageView();
    m_previewView->setHorizontalAlignment(HorizontalAlignment::Center);
    m_previewView->setPreferredSize(ui->du(40), ui->du(40));
    m_previewView->setScalingMethod(ScalingMethod::AspectFit);
    m_previewView->setLoadEffect(ImageViewLoadEffect::None);
    Container *buttons = new Container();
    buttons->setHorizontalAlignment(HorizontalAlignment::Fill);
    buttons->setTopMargin(ui->du(3));
    buttons->setLayout(Card::across());
    Button *send = new Button();
    Card::quota(send, 1);
    send->setText(QCoreApplication::translate("StickersPage", "Send"));
    connect(send, SIGNAL(clicked()), this, SLOT(onSend()));
    m_faveButton = new Button();
    Card::quota(m_faveButton, 1);
    connect(m_faveButton, SIGNAL(clicked()), this, SLOT(onFave()));
    buttons->add(send);
    buttons->add(m_faveButton);
    box->add(m_previewView);
    box->add(buttons);
    m_overlay->add(dim);
    m_overlay->add(box);

    m_root->add(column);
    m_root->add(m_empty);
    m_root->add(m_overlay);
    setRoot(m_root);
}

// The page closed: the preview ends, and the picker, which stays for the next page, is told nobody is looking.
StickerGrid::~StickerGrid()
{
    if (m_picker) {
        m_picker->endPreview(m_previewView);
        m_picker->release();
    }
}

// A QPointer, as ChatSearch's link: at app exit the page may outlive the EngineLink. The data is the app's (shared): what an
// earlier page loaded is in the model and the strip already.
void StickerGrid::setLink(QObject *link)
{
    EngineLink *engine = qobject_cast<EngineLink *>(link);
    if (!engine || m_picker)
        return;
    m_link = link;
    m_picker = StickerPicker::shared(engine);
    connect(m_picker, SIGNAL(stripChanged()), this, SLOT(syncStrip()));
    connect(m_picker, SIGNAL(loadingChanged()), this, SLOT(syncState()));
    m_list->setDataModel(m_picker->model()); // the picker's: the list doesn't take it
    syncStrip();
    m_picker->load();
}

// "#RRGGBB": main.qml's nav.pageColor.
void StickerGrid::setPageColor(const QString &color)
{
    m_pageColor = color;
    m_root->setBackground(Card::color(color));
}

// The groups come and their covers arrive one by one: a cell is made for each new group, and a cell's picture is set only
// when it changed.
void StickerGrid::syncStrip()
{
    const QVariantList groups = m_picker->strip();
    while (m_cells.size() > groups.size()) {
        StripCell *extra = m_cells.takeLast();
        m_strip->remove(extra);
        delete extra;
    }
    for (int i = 0; i < groups.size(); ++i) {
        if (i >= m_cells.size()) {
            StripCell *cell = new StripCell(i, ui());
            connect(cell, SIGNAL(tapped(int)), this, SLOT(go(int)));
            m_strip->add(cell);
            m_cells << cell;
        }
        m_cells.at(i)->show(groups.at(i).toMap());
    }
    mark(m_marked);
    syncState();
}

// The spinner while the first groups come; the empty state when none came.
void StickerGrid::syncState()
{
    const bool none = m_cells.isEmpty();
    const bool loading = m_picker->loading();
    m_spin->setVisible(loading && none);
    m_spin->setRunning(loading && none);
    m_empty->setVisible(!loading && none);
}

void StickerGrid::go(int group)
{
    m_list->scrollToItem(QVariantList() << group, ScrollAnimation::None);
}

void StickerGrid::mark(int group)
{
    m_marked = group;
    for (int i = 0; i < m_cells.size(); ++i)
        m_cells.at(i)->mark(i == group);
}

void StickerGrid::onFirstVisible(const QVariantList &path)
{
    if (!path.isEmpty())
        mark(path.first().toInt());
}

// A long press that opened the preview ends in a release, which may count as a tap.
void StickerGrid::onTriggered(const QVariantList &path)
{
    if (m_previewing || path.size() != 2 || !m_picker)
        return;
    emit picked(m_picker->model()->data(path).toMap().value("file").toString());
}

// The grid's picture is in the view at once; the sticker's own file, sharper, replaces it when it arrives.
void StickerGrid::openPreview(const QVariantMap &sticker)
{
    if (m_previewing || !m_picker)
        return;
    m_preview = sticker;
    m_previewView->setImage(sticker.value("image").value<Image>());
    m_faveButton->setText(sticker.value("fave").toBool()
                              ? QCoreApplication::translate("StickersPage", "Remove from favourites")
                              : QCoreApplication::translate("StickersPage", "Add to favourites"));
    m_overlay->setVisible(true);
    m_previewing = true;
    m_picker->preview(m_previewView, sticker.value("file").toString(), sticker.value("format").toString());
}

void StickerGrid::closePreview()
{
    if (m_picker)
        m_picker->endPreview(m_previewView);
    m_previewing = false;
    m_overlay->setVisible(false);
}

void StickerGrid::onSend()
{
    const QString file = m_preview.value("file").toString();
    closePreview();
    emit picked(file);
}

void StickerGrid::onFave()
{
    if (m_picker)
        m_picker->fave(m_preview.value("file").toString(), !m_preview.value("fave").toBool());
    closePreview();
}

StickerCells::StickerCells(StickerGrid *grid)
    : ListItemProvider(0)
    , m_grid(grid)
{
}

VisualNode *StickerCells::createItem(ListView *, const QString &type)
{
    if (type == "header")
        return new Header();
    return new StickerCell(m_grid);
}

void StickerCells::updateItem(ListView *, VisualNode *listItem, const QString &type, const QVariantList &,
                              const QVariant &data)
{
    if (type == "header")
        static_cast<Header *>(listItem)->setTitle(data.toString());
    else
        static_cast<StickerCell *>(listItem)->show(data.toMap());
}

StickerCell::StickerCell(StickerGrid *grid)
    : m_grid(grid)
{
    UIConfig *ui = grid->ui();
    setHorizontalAlignment(HorizontalAlignment::Fill);
    setVerticalAlignment(VerticalAlignment::Fill);

    m_root = new Container();
    m_root->setLayout(new DockLayout());
    m_root->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_root->setVerticalAlignment(VerticalAlignment::Fill);
    // 90 % of the cell (180 px at 720).
    m_root->setLeftPadding(ui->px(9));
    m_root->setRightPadding(ui->px(9));
    m_root->setTopPadding(ui->px(9));
    m_root->setBottomPadding(ui->px(9));
    LongPressHandler *press = new LongPressHandler(m_root);
    m_root->addGestureHandler(press);
    connect(press, SIGNAL(longPressed(bb::cascades::LongPressEvent *)), this, SLOT(onLongPress()));

    m_image = new ImageView();
    m_image->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_image->setVerticalAlignment(VerticalAlignment::Fill);
    m_image->setScalingMethod(ScalingMethod::AspectFit);
    m_image->setLoadEffect(ImageViewLoadEffect::None);

    // A sticker with no picture (a WebM with no thumbnail).
    m_emoji = new Label();
    m_emoji->setHorizontalAlignment(HorizontalAlignment::Center);
    m_emoji->setVerticalAlignment(VerticalAlignment::Center);
    m_emoji->setTextFormat(TextFormat::Plain);
    m_emoji->setVisible(false);

    m_root->add(m_image);
    m_root->add(m_emoji);
    setRoot(m_root);
}

void StickerCell::show(const QVariantMap &sticker)
{
    m_sticker = sticker;
    const bool has = sticker.value("hasImage").toBool();
    if (has)
        m_image->setImage(sticker.value("image").value<Image>());
    m_image->setVisible(has);
    m_emoji->setVisible(!has);
    if (!has) {
        m_emoji->textStyle()->setBase(m_grid->emojiTextStyle());
        m_emoji->setText(sticker.value("emoji").toString());
    }
}

void StickerCell::select(bool)
{
}

void StickerCell::activate(bool activated)
{
    if (activated)
        m_root->setBackground(Card::color(m_grid->pressColor()));
    else
        m_root->resetBackground();
}

void StickerCell::reset(bool, bool activated)
{
    activate(activated);
}

void StickerCell::onLongPress()
{
    m_grid->openPreview(m_sticker);
}

// 72 px, as EmojiPicker's were (the strip scrolls here): the picture 54 px, the group under the grid's first item underlined.
StripCell::StripCell(int index, UIConfig *ui)
    : m_index(index)
{
    Container *root = new Container();
    root->setLayout(new DockLayout());
    root->setPreferredSize(ui->px(72), ui->px(72));
    TapHandler *tap = new TapHandler(root);
    root->addGestureHandler(tap);
    connect(tap, SIGNAL(tapped(bb::cascades::TapEvent *)), this, SLOT(onTap()));

    m_icon = new ImageView();
    m_icon->setHorizontalAlignment(HorizontalAlignment::Center);
    m_icon->setVerticalAlignment(VerticalAlignment::Center);
    m_icon->setPreferredSize(ui->px(54), ui->px(54));
    m_icon->setScalingMethod(ScalingMethod::AspectFit);
    m_icon->setLoadEffect(ImageViewLoadEffect::None);

    m_underline = new Container();
    m_underline->setVisible(false);
    m_underline->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_underline->setVerticalAlignment(VerticalAlignment::Bottom);
    m_underline->setPreferredHeight(ui->px(5));
    m_underline->setBackground(ui->palette()->primary());

    root->add(m_icon);
    root->add(m_underline);
    setRoot(root);
}

// An asset's path, or an Image (a set's cover).
void StripCell::show(const QVariantMap &row)
{
    const QString key = row.value("key").toString();
    if (key == m_key)
        return;
    m_key = key;
    const QVariant image = row.value("image");
    if (image.type() == QVariant::String)
        m_icon->setImageSource(QUrl(image.toString()));
    else
        m_icon->setImage(image.value<Image>());
}

void StripCell::mark(bool on)
{
    m_underline->setVisible(on);
}

void StripCell::onTap()
{
    emit tapped(m_index);
}
