#include "PollBubble.h"

#include <bb/cascades/Container>
#include <bb/cascades/HorizontalAlignment>
#include <bb/cascades/Label>
#include <bb/cascades/ListView>
#include <bb/cascades/SystemDefaults>
#include <bb/cascades/TapHandler>
#include <bb/cascades/TextAlign>
#include <bb/cascades/UIConfig>
#include <bb/cascades/UIPalette>
#include <bb/cascades/VerticalAlignment>

#include "CardStyle.h"

using namespace bb::cascades;

PollBubble::PollBubble(ListView *list)
    : m_list(list)
    , m_ui(list->ui())
    , m_built(false)
{
    setTopMargin(0);
    setBottomMargin(0);
    m_root = new Container();
    setRoot(m_root);
}

void PollBubble::show(const QVariantMap &msg)
{
    // The rows hold the options, the counts and our vote: they are made again only when one of
    // them changed, else just styled again (the text size, the theme).
    const bool same = m_built && msg.value("options") == m_msg.value("options")
        && msg.value("mine") == m_msg.value("mine") && msg.value("voted") == m_msg.value("voted")
        && msg.value("max") == m_msg.value("max");
    m_msg = msg;
    if (!same)
        rebuild();
    m_built = true;
    restyle();
}

void PollBubble::rebuild()
{
    m_root->removeAll();
    m_rows.clear();
    const QVariantList options = m_msg.value("options").toList();
    for (int i = 0; i < options.size(); i++)
        makeRow(i, options.at(i).toMap());
}

// The mark: ☑ ☐ for several choices, ● ○ for one.
static QString markOf(bool several, bool picked)
{
    return QString(QChar(several ? (picked ? 0x2611 : 0x2610) : (picked ? 0x25CF : 0x25CB)));
}

void PollBubble::makeRow(int index, const QVariantMap &option)
{
    const int count = option.value("count").toInt();
    // How many voted, for the bar.
    const int total = m_msg.value("voted").toInt();
    const bool several = m_msg.value("max").toInt() != 1;

    Row r;
    r.picked = chosen(index);
    r.row = new Container();

    Container *line = new Container();
    line->setLayout(Card::across());

    // The mark and the name vote; the count beside them, not inside, has its own tap:
    // nested, a tap on the count could reach both handlers.
    Container *choice = new Container();
    Card::quota(choice, 1);
    choice->setLayout(Card::across());
    TapHandler *vote = new TapHandler(choice);
    vote->setProperty("index", index);
    choice->addGestureHandler(vote);
    connect(vote, SIGNAL(tapped(bb::cascades::TapEvent *)), this, SLOT(onChoose()));

    // From the phone's fallback font (DejaVu Sans), as ✕ ▲ ▼ are, so not the emoji style:
    // its font has ☑ as a colour emoji.
    r.mark = new Label();
    r.mark->setVerticalAlignment(VerticalAlignment::Top);
    r.mark->setTopMargin(0);
    r.mark->setBottomMargin(0);
    r.mark->setRightMargin(m_ui->du(1));
    r.mark->setText(markOf(several, r.picked));
    r.mark->setTextFormat(TextFormat::Plain);

    // The name in a container with the quota, as the card's text is.
    Container *words = new Container();
    words->setVerticalAlignment(VerticalAlignment::Top);
    Card::quota(words, 1);
    r.name = new Label();
    r.name->setTopMargin(0);
    r.name->setBottomMargin(0);
    r.name->setMultiline(true);
    // Without U+20E3, as the card's text.
    r.name->setText(Card::noKeycaps(option.value("name").toString()));
    words->add(r.name);

    choice->add(r.mark);
    choice->add(words);

    // Its own tap, so it doesn't vote; the width is a thumb's, the number at the right.
    r.count = new Label();
    r.count->setVerticalAlignment(VerticalAlignment::Top);
    r.count->setTopMargin(0);
    r.count->setBottomMargin(0);
    r.count->setLeftMargin(m_ui->du(1));
    r.count->setMinWidth(m_ui->du(5));
    r.count->setText(QString::number(count));
    TapHandler *who = new TapHandler(r.count);
    who->setProperty("index", index);
    r.count->addGestureHandler(who);
    connect(who, SIGNAL(tapped(bb::cascades::TapEvent *)), this, SLOT(onWho()));

    line->add(choice);
    line->add(r.count);

    // The share of the voters who chose it: the primary colour for the count, grey for the
    // rest, all grey while nobody voted.
    Container *bar = new Container();
    bar->setTopMargin(m_ui->du(0.5));
    bar->setBottomMargin(0);
    bar->setHorizontalAlignment(HorizontalAlignment::Fill);
    bar->setPreferredHeight(m_ui->du(0.5));
    bar->setLayout(Card::across());

    // A part with no share is hidden, not given a quota of 0: Cascades sized a 0 like
    // a 1 (Q10, 0.0.1.132: one vote of one filled half the bar, two of two 2/3).
    // A quota of 0 also logs "setSpaceQuota(): 0 is not a valid value", so the quotas
    // are never below 1.
    r.have = new Container();
    r.have->setVisible(count > 0);
    r.have->setVerticalAlignment(VerticalAlignment::Fill);
    Card::quota(r.have, qMax(count, 1));
    r.rest = new Container();
    r.rest->setVisible(total == 0 || count < total);
    r.rest->setVerticalAlignment(VerticalAlignment::Fill);
    r.rest->setOpacity(0.3f);
    Card::quota(r.rest, total > 0 ? qMax(total - count, 1) : 1);
    bar->add(r.have);
    bar->add(r.rest);

    r.row->add(line);
    r.row->add(bar);
    m_root->add(r.row);
    m_rows.append(r);
}

// What the text size and the theme's colours decide.
void PollBubble::restyle()
{
    const float scale = Card::textScale(m_list);
    const int body = Card::bodySize(m_list);
    const int small = Card::smallSize(m_list);
    const UIPalette *palette = m_ui->palette();
    for (int i = 0; i < m_rows.size(); i++) {
        const Row &r = m_rows.at(i);
        r.row->setTopPadding(m_ui->du(1 * scale));

        TextStyleDefinition *mark = r.mark->textStyle();
        mark->setBase(SystemDefaults::TextStyles::bodyText());
        mark->setFontSize(FontSize::PercentageValue);
        mark->setFontSizeValue(qRound(100 * scale));
        mark->setColor(r.picked ? palette->primary() : palette->secondaryTextOnPlain());

        Card::style(r.name, m_list, body);
        r.name->textStyle()->setColor(palette->textOnPlain());

        Card::style(r.count, m_list, small);
        r.count->textStyle()->setTextAlign(TextAlign::Right);
        r.count->textStyle()->setColor(palette->secondaryTextOnPlain());

        r.have->setBackground(palette->primary());
        r.rest->setBackground(palette->secondaryTextOnPlain());
    }
}

// Whether our vote has option i.
bool PollBubble::chosen(int i) const
{
    const QVariantList mine = m_msg.value("mine").toList();
    for (int k = 0; k < mine.size(); k++) {
        if (mine.at(k).toInt() == i)
            return true;
    }
    return false;
}

// The whole new vote: one choice moves or goes, several toggle i and stop at max (0: no limit).
// ponytail: from the vote last drawn, so two taps before the engine answers act on the same
// state; an optimistic copy of mine if that shows.
void PollBubble::choose(int i)
{
    if (m_msg.value("readOnly").toBool())
        return;
    const QVariantList mine = m_msg.value("mine").toList();
    const int max = m_msg.value("max").toInt();
    const bool on = chosen(i);
    QVariantList next;
    if (max == 1) {
        if (!on)
            next << i;
    } else {
        for (int k = 0; k < mine.size(); k++) {
            if (mine.at(k).toInt() != i)
                next << mine.at(k);
        }
        if (!on) {
            if (max > 0 && mine.size() >= max)
                return;
            next << i;
        }
    }
    Card::call(m_list, "vote", m_msg.value("id"), next);
}

void PollBubble::onChoose()
{
    choose(sender()->property("index").toInt());
}

void PollBubble::onWho()
{
    const QVariantList options = m_msg.value("options").toList();
    const int i = sender()->property("index").toInt();
    if (i < 0 || i >= options.size())
        return;
    const QString voters = options.at(i).toMap().value("voters").toString();
    Card::call(m_list, "showVoters", voters.isEmpty() ? Card::tr("PollBubble", "Nobody yet") : voters);
}
