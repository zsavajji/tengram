#ifndef VOICEBUBBLE_H
#define VOICEBUBBLE_H

#include <bb/cascades/CustomControl>
#include <QString>
#include <QVariantMap>

namespace bb {
namespace cascades {
class ActivityIndicator;
class Container;
class ImageView;
class Label;
class ListView;
class UIConfig;
}
}

// A voice note in a card (step 4.3b), mirroring VoiceBubble.qml (gone since step 8.6), as Telega lays it out: the disc on
// the left; the waveform over the length on the right, and at the length's right stamp(), the corner
// where the card puts the time, as Telegram does (the user, 2026-11-14). The waveform (Waveform.cpp) is grey, and over
// it an image of only the bars played, in the primary colour. A clipping container as wide as the
// part played shrank its image instead, which then slid over the grey one (0.0.1.79). list is the
// chat page's list, which holds the player's state (playingId, loadingId, paused, posMs, durMs):
// its change signals re-apply the live parts, as QML's bindings did.
class VoiceBubble : public bb::cascades::CustomControl
{
    Q_OBJECT
public:
    explicit VoiceBubble(bb::cascades::ListView *list);

    // msg is a voice row of MessageModel: id, duration, mediaState, played, wave and waveData.
    void show(const QVariantMap &msg);

    bb::cascades::Container *stamp() const { return m_stamp; }

private slots:
    void refresh();
    void onPlay();

private:
    bb::cascades::ListView *m_list;
    bb::cascades::UIConfig *m_ui;
    QVariantMap m_msg;

    // The played overlay is drawn again only when its message or its bar count changes.
    QString m_playedId;
    int m_bars;

    bb::cascades::ImageView *m_icon;
    bb::cascades::ActivityIndicator *m_spinner;
    bb::cascades::ImageView *m_wave;
    bb::cascades::ImageView *m_played;
    bb::cascades::ImageView *m_mic;
    bb::cascades::Label *m_clock;
    bb::cascades::Container *m_stamp;
};

#endif
