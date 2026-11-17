#include "VoiceBubble.h"

#include <bb/cascades/ActivityIndicator>
#include <bb/cascades/Container>
#include <bb/cascades/DockLayout>
#include <bb/cascades/HorizontalAlignment>
#include <bb/cascades/Image>
#include <bb/cascades/ImageView>
#include <bb/cascades/Label>
#include <bb/cascades/ListView>
#include <bb/cascades/ScalingMethod>
#include <bb/cascades/SystemDefaults>
#include <bb/cascades/TapHandler>
#include <bb/cascades/TextFormat>
#include <bb/cascades/TextStyleDefinition>
#include <bb/cascades/UIConfig>
#include <bb/cascades/UIPalette>
#include <bb/cascades/VerticalAlignment>
#include <qmath.h>

#include "CardStyle.h"
#include "Waveform.h"

using namespace bb::cascades;

namespace {

// One of the two waveforms, the same size, laid over each other.
ImageView *waveView()
{
    ImageView *view = new ImageView();
    view->setPreferredWidth(381);
    view->setPreferredHeight(45);
    view->setScalingMethod(ScalingMethod::None);
    return view;
}

// m:ss
QString clockText(int seconds)
{
    return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

}

VoiceBubble::VoiceBubble(ListView *list)
    : m_list(list)
    , m_ui(list->ui())
    , m_bars(0)
{
    setTopMargin(0);
    setBottomMargin(0);
    Container *root = new Container();
    root->setLayout(Card::across());

    // The disc: the icon, or a spinner while the note is fetched. A tap plays it.
    Container *disc = new Container();
    disc->setLayout(new DockLayout());
    disc->setPreferredWidth(72);
    disc->setPreferredHeight(72);
    // Its least size too: the disc and the 381 px waveform need 467 px, and beside a 12-hour time
    // ("10:49 PM" and the ticks) the column is 427, which squeezed the disc to 52 px wide (Q10, 0.1.1.316).
    // The waveform gives way instead, and loses its last bars.
    disc->setMinWidth(72);
    disc->setMinHeight(72);
    disc->setVerticalAlignment(VerticalAlignment::Center);
    m_icon = new ImageView();
    m_icon->setHorizontalAlignment(HorizontalAlignment::Fill);
    m_icon->setVerticalAlignment(VerticalAlignment::Fill);
    m_spinner = new ActivityIndicator();
    m_spinner->setHorizontalAlignment(HorizontalAlignment::Center);
    m_spinner->setVerticalAlignment(VerticalAlignment::Center);
    disc->add(m_icon);
    disc->add(m_spinner);
    TapHandler *tap = new TapHandler(disc);
    disc->addGestureHandler(tap);
    connect(tap, SIGNAL(tapped(bb::cascades::TapEvent *)), this, SLOT(onPlay()));

    // The waveform over the length.
    Container *right = new Container();
    right->setLeftMargin(m_ui->du(1.5));
    right->setVerticalAlignment(VerticalAlignment::Center);
    Container *waves = new Container();
    waves->setLayout(new DockLayout());
    m_wave = waveView();
    m_played = waveView();
    m_played->setVisible(false);
    waves->add(m_wave);
    waves->add(m_played);

    // Under it the length at the left and the time's corner at the right, the waveform's width apart.
    Container *under = new Container();
    under->setTopMargin(m_ui->du(0.5));
    under->setHorizontalAlignment(HorizontalAlignment::Fill);
    under->setLayout(new DockLayout());
    Container *length = new Container();
    length->setVerticalAlignment(VerticalAlignment::Center);
    length->setLayout(Card::across());
    m_mic = new ImageView();
    m_mic->setVerticalAlignment(VerticalAlignment::Center);
    m_mic->setImageSource(Card::url("asset:///images/mic.png"));
    m_clock = new Label();
    m_clock->setVerticalAlignment(VerticalAlignment::Center);
    m_clock->setLeftMargin(m_ui->du(0.5));
    m_clock->setTextFormat(TextFormat::Plain);
    m_clock->textStyle()->setBase(SystemDefaults::TextStyles::smallText());
    length->add(m_mic);
    length->add(m_clock);

    m_stamp = new Container();
    m_stamp->setHorizontalAlignment(HorizontalAlignment::Right);
    m_stamp->setVerticalAlignment(VerticalAlignment::Bottom);
    under->add(length);
    under->add(m_stamp);

    right->add(waves);
    right->add(under);
    root->add(disc);
    root->add(right);
    setRoot(root);

    // The player's state, as the QML bound it.
    const char *const live[] = { SIGNAL(playingIdChanged()), SIGNAL(loadingIdChanged()), SIGNAL(pausedChanged()),
                                 SIGNAL(posMsChanged()), SIGNAL(durMsChanged()) };
    for (unsigned i = 0; i < sizeof(live) / sizeof(live[0]); ++i)
        if (!connect(list, live[i], this, SLOT(refresh())))
            qWarning("cards: the list has no %s", live[i] + 1);
}

void VoiceBubble::show(const QVariantMap &msg)
{
    m_msg = msg;
    const Color quiet = m_ui->palette()->secondaryTextOnPlain();
    m_wave->setFilterColor(quiet);
    m_played->setFilterColor(m_ui->palette()->primary());
    m_wave->setImage(m_msg.value("wave").value<Image>());
    m_mic->setFilterColor(m_msg.value("played").toBool() ? m_ui->palette()->primary() : quiet);
    m_clock->textStyle()->setColor(quiet);
    refresh();
}

// What the player's state decides: the disc, the played bars and the clock.
void VoiceBubble::refresh()
{
    if (m_msg.isEmpty())
        return;
    const QString id = m_msg.value("id").toString();
    const bool playing = m_list->property("playingId").toString() == id;
    const bool loading = m_list->property("loadingId").toString() == id;
    const bool paused = m_list->property("paused").toBool();
    const int posMs = m_list->property("posMs").toInt();
    const int durMs = m_list->property("durMs").toInt();
    const int duration = m_msg.value("duration").toInt();

    // The note's own length until the player knows it.
    const int total = playing && durMs > 0 ? durMs : duration * 1000;
    const double progress = playing && total > 0 ? qMin(1.0, double(posMs) / total) : 0;
    // An int, so the played image is drawn again only when a bar changes: at most 64 times.
    const int bars = qCeil(progress * 64);

    m_icon->setVisible(!loading);
    m_icon->setImageSource(Card::url(m_msg.value("mediaState").toInt() == 3 && !playing ? "asset:///images/retry.png"
                                     : playing && !paused ? "asset:///images/pause.png"
                                     : "asset:///images/play.png"));
    m_spinner->setRunning(loading);

    m_played->setVisible(bars > 0);
    if (bars > 0 && (id != m_playedId || bars != m_bars)) {
        // As ChatController::playedWave: the bars' values as bytes.
        QByteArray bytes;
        foreach (const QVariant &v, m_msg.value("waveData").toList())
            bytes.append(char(v.toInt()));
        m_played->setImage(Waveform::image(bytes, bars).value<Image>());
        m_playedId = id;
        m_bars = bars;
    }

    m_clock->setText(clockText(playing ? qMax(0, qCeil((total - posMs) / 1000.0)) : duration));
}

void VoiceBubble::onPlay()
{
    Card::call(m_list, "playVoice", m_msg);
}
