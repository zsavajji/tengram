#ifndef EMOJIMODEL_H
#define EMOJIMODEL_H

#include <bb/cascades/DataModel>
#include <QList>
#include <QStringList>
#include <QVariantList>

// The emoji picker's data (PLAN.md §7, step 4.2): the groups of emojiTable.h behind a "Recent"
// group, as the top level ([g]); an emoji is its child ([g, i]): a map of emoji and image, the
// PNG cell scripts/build-emoji-font.py exports, so a cell is never text.
class EmojiModel : public bb::cascades::DataModel
{
    Q_OBJECT
public:
    explicit EmojiModel(QObject *parent = 0);

    int childCount(const QVariantList &indexPath);
    bool hasChildren(const QVariantList &indexPath);
    QString itemType(const QVariantList &indexPath);
    QVariant data(const QVariantList &indexPath);

    // Puts the emoji first in the saved recents. The model stays as it is, so the grid doesn't
    // move under the finger; refresh() shows them.
    Q_INVOKABLE void use(const QString &emoji);
    Q_INVOKABLE void refresh();
    // The category strip: a map of title and image per group, Recent first.
    Q_INVOKABLE QVariantList groups();

private:
    QStringList m_titles;
    QStringList m_icons;
    QList<QStringList> m_emoji; // by group; [0] is Recent as of the last refresh()
    QStringList m_recent;       // as saved
};

#endif
