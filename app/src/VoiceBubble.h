#ifndef VOICEBUBBLE_H
#define VOICEBUBBLE_H

#include <bb/cascades/CustomControl>
#include <QRectF>
#include <QElapsedTimer>
#include <QString>
#include <QVariantMap>

namespace bb {
namespace cascades {
class ActivityIndicator;
class Container;
class ImageView;
class Label;
class ListView;
class TouchEvent;
class UIConfig;
}
}

// A voice note in a card (step 4.3b), mirroring VoiceBubble.qml (gone since step 8.6), as Telega lays it out: the disc on
// the left; the waveform over the length on the right, and at the length's right stamp(), the corner
// where the card puts the time, as Telegram does (the user, 2026-11-14). The waveform (Waveform.cpp) is grey, and over
// it an image of only the bars played, in the primary colour. A clipping container as wide as the
// part played shrank its image instead, which then slid over the grey one (0.0.1.79). list is the
// chat page's list, which holds the player's state (playingId, loadingId, paused, posMs, durMs):
// its change signals re-apply the live parts, as QML's bindings did. A touch on the waveform seeks: a tap
// moves the note there, a drag along it follows the finger with the played bars and the clock, and the note
// moves at the let-go; the list's `seekVoice` does it, and starts a note that is not playing at that point.
class VoiceBubble : public bb::cascades::CustomControl
{
    Q_OBJECT
public:
    explicit VoiceBubble(bb::cascades::ListView *list);

    // msg is a voice row of MessageModel: id, duration, mediaState, played, wave and waveData.
    void show(const QVariantMap &msg);

    bb::cascades::Container *stamp() const { return m_stamp; }

    // A finger is on the waveform and has not turned to scroll: the card's swipe to reply leaves it be.
    bool scrubbing() const { return m_scrubbing; }

private slots:
    void refresh();
    void onPlay();
    void onWaveTouch(bb::cascades::TouchEvent *event);
    void onWaveFrame(const QRectF &frame);

private:
    // The note's length in ms: the player's once it plays this note, else its own.
    int totalMs() const;
    // The share of the waveform at a window x, 0 to 1.
    double fractionAt(float windowX) const;

    bb::cascades::ListView *m_list;
    bb::cascades::UIConfig *m_ui;
    QVariantMap m_msg;

    // The played overlay is drawn again only when its message or its bar count changes.
    QString m_playedId;
    int m_bars;

    // The touch on the waveform. m_waveLeft is the waveform's left in window coordinates, taken at the down (the
    // card moves in a swipe); m_waveWidth its width, which a long time beside it can squeeze below 381 px.
    // m_scrubFrac is where the finger is, 0 to 1. After the seek it is shown until the player's position
    // changes from m_settlePos, as VideoViewer's settling, so the bars do not jump back to the old place.
    bool m_scrubbing;
    bool m_scrubMoved;
    bool m_settling;
    int m_settlePos;
    float m_waveLeft;
    float m_waveWidth;
    float m_downX;
    float m_downY;
    double m_scrubFrac;
    QElapsedTimer m_downAt; // the down, to tell a tap from the long press

    bb::cascades::ImageView *m_icon;
    bb::cascades::ActivityIndicator *m_spinner;
    bb::cascades::ImageView *m_wave;
    bb::cascades::ImageView *m_played;
    bb::cascades::ImageView *m_mic;
    bb::cascades::Label *m_clock;
    bb::cascades::Container *m_stamp;
};

#endif
