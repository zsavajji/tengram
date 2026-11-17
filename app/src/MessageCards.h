#ifndef MESSAGECARDS_H
#define MESSAGECARDS_H

#include <bb/cascades/CustomControl>
#include <bb/cascades/ListItemProvider>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace bb {
namespace cascades {
class Label;
class ListView;
}
}

// The cards of the chat page's list, in C++ (step 8.6): a row is built once, in createItem, and
// filled on each recycle, in updateItem, with setters in place of QML's bindings. The page sets it
// as the list's listItemProvider; the cards reach the page through the list's functions and
// properties (ChatPage.qml). Types: "header", "service", "text" (the light card) and "msg".
class MessageCards : public bb::cascades::ListItemProvider
{
    Q_OBJECT
public:
    MessageCards(QObject *parent = 0);

    bb::cascades::VisualNode *createItem(bb::cascades::ListView *list, const QString &type);
    void updateItem(bb::cascades::ListView *list, bb::cascades::VisualNode *listItem,
                    const QString &type, const QVariantList &indexPath, const QVariant &data);

private:
    QPointer<bb::cascades::ListView> m_list; // the list whose text size change is wired
};

// A service line ("service" rows of MessageModel): a chip, like a button that isn't one (Q10,
// 2026-10-31), mirroring ServiceItem.qml (gone since step 8.6).
class ServiceCard : public bb::cascades::CustomControl
{
    Q_OBJECT
public:
    ServiceCard(bb::cascades::ListView *list);

    void show(const QVariantMap &msg);

private:
    bb::cascades::ListView *m_list;
    bb::cascades::Label *m_text;
};

#endif
