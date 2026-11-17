#ifndef POLLBUBBLE_H
#define POLLBUBBLE_H

#include <bb/cascades/CustomControl>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace bb {
namespace cascades {
class Container;
class Label;
class ListView;
class UIConfig;
}
}

// The options of a poll in a card (step 6.5e), under its question: per option a mark (● ○ for one
// choice, ☑ ☐ for several), the name, the count, and a bar of the share of the voters who chose it.
// A tap on an option votes (the page's vote), a tap on its count says who chose it (showVoters).
// Mirrors PollBubble.qml (gone since step 8.6). Cascades has no Repeater: the rows are made again whenever the vote changes,
// since list items are recycled, and removeAll() deletes the old rows (container.h: it "frees up
// their memory").
class PollBubble : public bb::cascades::CustomControl
{
    Q_OBJECT
public:
    explicit PollBubble(bb::cascades::ListView *list);

    // msg is a "msg" row of MessageModel (max, mine, voted, options, readOnly, id: see Polls.h).
    void show(const QVariantMap &msg);

private slots:
    void onChoose();
    void onWho();

private:
    struct Row {
        bool picked;
        bb::cascades::Container *row;
        bb::cascades::Label *mark;
        bb::cascades::Label *name;
        bb::cascades::Label *count;
        bb::cascades::Container *have; // the bar's part for the ones who chose it
        bb::cascades::Container *rest; // and for the rest
    };

    void rebuild();
    void makeRow(int index, const QVariantMap &option);
    void restyle();
    bool chosen(int i) const;
    void choose(int i);

    bb::cascades::ListView *m_list;
    bb::cascades::UIConfig *m_ui;
    bb::cascades::Container *m_root;
    QVariantMap m_msg;
    // False until the first show, which makes the rows whatever msg holds.
    bool m_built;
    QList<Row> m_rows;
};

#endif
