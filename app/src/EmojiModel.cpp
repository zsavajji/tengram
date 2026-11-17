#include "EmojiModel.h"

#include "emojiTable.h"

#include <QDir>
#include <QSettings>
#include <QVariantMap>
#include <QVector>

static const int kRecent = 27; // three rows of the grid's nine

// A QSettings can't be copied, so only the file's name is shared.
static QString settingsFile()
{
    return QDir::homePath() + "/settings.ini";
}

// The PNG of an emoji, named by its first code point in lowercase hex.
static QString image(const QString &emoji)
{
    const QVector<uint> cps = emoji.toUcs4();
    return "asset:///emoji/" + QString::number(cps.value(0), 16) + ".png";
}

EmojiModel::EmojiModel(QObject *parent)
    : DataModel(parent)
{
    m_titles << tr("Recent");
    m_icons << QString::fromUtf8("\xF0\x9F\x95\x98"); // U+1F558
    m_emoji << QStringList();
    for (unsigned i = 0; i < sizeof(kEmojiTable) / sizeof(kEmojiTable[0]); ++i) {
        m_titles << QString::fromUtf8(kEmojiTable[i].title);
        m_icons << QString::fromUtf8(kEmojiTable[i].icon);
        m_emoji << QString::fromUtf8(kEmojiTable[i].emoji).split(' ');
    }
    m_recent = QSettings(settingsFile(), QSettings::IniFormat).value("emojiRecent").toString().split(' ', QString::SkipEmptyParts);
    m_emoji[0] = m_recent;
}

int EmojiModel::childCount(const QVariantList &indexPath)
{
    if (indexPath.isEmpty())
        return m_emoji.size();
    if (indexPath.size() == 1)
        return m_emoji.value(indexPath[0].toInt()).size();
    return 0;
}

bool EmojiModel::hasChildren(const QVariantList &indexPath)
{
    return indexPath.size() < 2 && childCount(indexPath) > 0;
}

QString EmojiModel::itemType(const QVariantList &indexPath)
{
    return indexPath.size() == 1 ? "header" : "item";
}

QVariant EmojiModel::data(const QVariantList &indexPath)
{
    const int g = indexPath.value(0).toInt();
    if (indexPath.size() == 1)
        return m_titles.value(g);
    const QString emoji = m_emoji.value(g).value(indexPath.value(1).toInt());
    QVariantMap row;
    row["emoji"] = emoji;
    row["image"] = image(emoji);
    return row;
}

void EmojiModel::use(const QString &emoji)
{
    m_recent.removeAll(emoji);
    m_recent.prepend(emoji);
    while (m_recent.size() > kRecent)
        m_recent.removeLast();
    QSettings(settingsFile(), QSettings::IniFormat).setValue("emojiRecent", m_recent.join(" "));
}

void EmojiModel::refresh()
{
    m_emoji[0] = m_recent;
    emit itemsChanged(bb::cascades::DataModelChangeType::Init);
}

QVariantList EmojiModel::groups()
{
    QVariantList list;
    for (int g = 0; g < m_titles.size(); ++g) {
        QVariantMap row;
        row["title"] = m_titles[g];
        row["image"] = image(m_icons[g]);
        list << row;
    }
    return list;
}
