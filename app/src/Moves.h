#ifndef MOVES_H
#define MOVES_H

#include <bb/cascades/DataModel>
#include <QHash>
#include <QList>
#include <QPair>
#include <QStringList>

// Tells the ListView where each row and header it holds went after a reload, so a re-sort
// moves rows instead of resetting the list and its scroll position. It maps old index paths
// to new ones by key: a group's key, an item's key.
class Moves : public bb::cascades::DataModel::IndexMapper
{
public:
    QStringList oldGroups;
    QList<QStringList> oldItems;
    QHash<QString, int> groups;            // new group index, by group key
    QHash<QString, QPair<int, int> > items; // new [g, i], by item key

    bool newIndexPath(QVariantList *out, int *replacement, const QVariantList &old) const
    {
        const int g = old.value(0).toInt();
        if (old.size() == 1) {
            const QString group = oldGroups.value(g);
            if (!groups.contains(group)) {
                *replacement = g;
                return false;
            }
            *out = QVariantList() << groups.value(group);
            return true;
        }
        const int i = old.value(1).toInt();
        const QString item = oldItems.value(g).value(i);
        if (!items.contains(item)) {
            *replacement = i;
            return false;
        }
        const QPair<int, int> at = items.value(item);
        *out = QVariantList() << at.first << at.second;
        return true;
    }
};

#endif
