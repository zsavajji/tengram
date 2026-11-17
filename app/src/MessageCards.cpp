#include "MessageCards.h"

#include <bb/cascades/Container>
#include <bb/cascades/Header>
#include <bb/cascades/HorizontalAlignment>
#include <bb/cascades/Label>
#include <bb/cascades/ListView>
#include <bb/cascades/TextAlign>
#include <bb/cascades/UIConfig>
#include <bb/cascades/UIPalette>

#include "CardStyle.h"
#include "MessageCard.h"

using namespace bb::cascades;

MessageCards::MessageCards(QObject *parent)
    : ListItemProvider(parent)
{
}

VisualNode *MessageCards::createItem(ListView *list, const QString &type)
{
    // A text size change refills every card: its paddings and sizes were set from the old one.
    if (m_list != list) {
        m_list = list;
        if (!connect(list, SIGNAL(textScaleChanged()), this, SIGNAL(itemUpdatesNeeded())))
            qWarning("cards: the list has no textScaleChanged");
    }
    if (type == "header")
        return new Header();
    if (type == "service")
        return new ServiceCard(list);
    // "text" is the light card; "picture" the full one with a photo, video or map across it; "msg", and
    // anything else, the full one.
    return new MessageCard(list, type != "text", type == "picture");
}

void MessageCards::updateItem(ListView *, VisualNode *listItem, const QString &type,
                              const QVariantList &, const QVariant &data)
{
    if (type == "header")
        static_cast<Header *>(listItem)->setTitle(data.toString());
    else if (type == "service")
        static_cast<ServiceCard *>(listItem)->show(data.toMap());
    else
        static_cast<MessageCard *>(listItem)->show(data.toMap());
}

// A list item is as wide as its content, so Center alone left it on the left: the spacers'
// spaceQuota stretches the row across the list, as the cards' text column does. Containers have no
// border: the outer one's colour shows 2 px around the inner one.
ServiceCard::ServiceCard(ListView *list)
    : m_list(list)
{
    UIConfig *ui = list->ui();
    setHorizontalAlignment(HorizontalAlignment::Fill);

    Container *root = new Container();
    root->setHorizontalAlignment(HorizontalAlignment::Fill);
    root->setTopPadding(ui->du(1.5));
    root->setBottomPadding(0);
    root->setLayout(Card::across());

    Container *left = new Container();
    Card::quota(left, 1);

    Container *chip = new Container();
    chip->setBackground(Card::color("#5A5A5A"));
    chip->setLeftPadding(ui->px(2));
    chip->setRightPadding(ui->px(2));
    chip->setTopPadding(ui->px(2));
    chip->setBottomPadding(ui->px(2));

    Container *inner = new Container();
    inner->setBackground(ui->palette()->plain());
    inner->setLeftPadding(ui->du(2));
    inner->setRightPadding(ui->du(2));
    inner->setTopPadding(ui->du(1));
    inner->setBottomPadding(ui->du(1));

    m_text = new Label();
    m_text->setHorizontalAlignment(HorizontalAlignment::Center);
    m_text->setMultiline(true);
    m_text->textStyle()->setTextAlign(TextAlign::Center);
    m_text->textStyle()->setColor(ui->palette()->secondaryTextOnPlain());

    Container *right = new Container();
    Card::quota(right, 1);

    inner->add(m_text);
    chip->add(inner);
    root->add(left);
    root->add(chip);
    root->add(right);
    setRoot(root);
}

void ServiceCard::show(const QVariantMap &msg)
{
    Card::style(m_text, m_list, Card::smallSize(m_list));
    m_text->setText(msg.value("text").toString());
}
