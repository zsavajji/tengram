#ifndef MESSAGECARDS_H
#define MESSAGECARDS_H

#include <bb/cascades/CustomControl>
#include <bb/cascades/ListItemProvider>
#include <QList>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace bb {
namespace cascades {
class ActivityIndicator;
class Container;
class ImageView;
class Label;
class ListView;
}
}
class MessageCard;

// The cards of the chat page's list, in C++ (step 8.6): a row is built once, in createItem, and
// filled on each recycle, in updateItem, with setters in place of QML's bindings. The page sets it
// as the list's listItemProvider; the cards reach the page through the list's functions and
// properties (ChatPage.qml). Types: "header", "service", "loading" (the spinner row at the top edge), "text" (the light card) and "msg".
class MessageCards : public bb::cascades::ListItemProvider
{
    Q_OBJECT
public:
    MessageCards(QObject *parent = 0);

    bb::cascades::VisualNode *createItem(bb::cascades::ListView *list, const QString &type);
    void updateItem(bb::cascades::ListView *list, bb::cascades::VisualNode *listItem,
                    const QString &type, const QVariantList &indexPath, const QVariant &data);

    // The new message's slide (ChatPage's added): a card already showing row id reports its height now, since a reused card
    // that keeps its size and place is never laid out again (a one-line message missed its slide, 0.1.0.73).
    Q_INVOKABLE void slideRow(const QString &id);

private:
    QPointer<bb::cascades::ListView> m_list; // the list whose text size change is wired
    QList<QPointer<MessageCard> > m_cards;  // the message cards made, for slideRow
};

// A service line ("service" rows of MessageModel): a chip, like a button that isn't one (Q10,
// 2026-10-31), mirroring ServiceItem.qml (gone since step 8.6). The day headers are its too.
class ServiceCard : public bb::cascades::CustomControl
{
    Q_OBJECT
public:
    ServiceCard(bb::cascades::ListView *list);

    void show(const QVariantMap &msg);
    void showText(const QString &text);

private:
    bb::cascades::ListView *m_list;
    bb::cascades::Container *m_chip;
    bb::cascades::ImageView *m_pill; // Round mode's shape of the chip, under the text
    bb::cascades::Label *m_text;
};

// The "loading" row of MessageModel (edgeWaiting): a spinner on empty space at the list's top edge while an older page is
// awaited, with a little padding above and below (the user, 2026-11-20). It takes no data; it exists only while the list waits,
// so its spinner runs from the start.
class LoadingCard : public bb::cascades::CustomControl
{
    Q_OBJECT
public:
    LoadingCard(bb::cascades::ListView *list);
};

#endif
