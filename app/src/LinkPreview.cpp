#include "LinkPreview.h"

#include <bb/cascades/Container>
#include <bb/cascades/FontWeight>
#include <bb/cascades/HorizontalAlignment>
#include <bb/cascades/Image>
#include <bb/cascades/ImageView>
#include <bb/cascades/ImageViewLoadEffect>
#include <bb/cascades/Label>
#include <bb/cascades/LabelAutoSizeProperties>
#include <bb/cascades/ListView>
#include <bb/cascades/ScalingMethod>
#include <bb/cascades/TapHandler>
#include <bb/cascades/UIConfig>
#include <bb/cascades/UIPalette>
#include <bb/cascades/VerticalAlignment>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeReplyError>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTargetReply>
#include <QDebug>
#include <QRegExp>

#include "CardStyle.h"
#include "Invites.h"

using namespace bb::cascades;
using namespace bb::system;

LinkPreview::LinkPreview(ListView *list)
    : m_list(list)
    , m_ui(list->ui())
    , m_invoker(0)
{
    setTopMargin(m_ui->du(0.5));
    setBottomMargin(0);
    setHorizontalAlignment(HorizontalAlignment::Fill);

    // Filled too, as the card is: a control is as wide as its content unless it fills.
    Container *root = new Container();
    root->setHorizontalAlignment(HorizontalAlignment::Fill);
    root->setLayout(Card::across());
    TapHandler *tap = new TapHandler(root);
    root->addGestureHandler(tap);
    connect(tap, SIGNAL(tapped(bb::cascades::TapEvent *)), this, SLOT(onTap()));

    m_bar = new Container();
    m_bar->setVerticalAlignment(VerticalAlignment::Fill);
    m_bar->setPreferredWidth(m_ui->du(0.5));

    // The picture is square and grows with the text size, as the lines beside it do.
    m_thumb = new ImageView();
    m_thumb->setVerticalAlignment(VerticalAlignment::Top);
    m_thumb->setLeftMargin(m_ui->du(1));
    m_thumb->setScalingMethod(ScalingMethod::AspectFill);
    m_thumb->setLoadEffect(ImageViewLoadEffect::None);

    Container *words = new Container();
    words->setLeftMargin(m_ui->du(1));
    Card::quota(words, 1);

    m_title = new Label();
    m_title->setBottomMargin(0);
    m_title->setMultiline(true);
    m_title->autoSize()->setMaxLineCount(2);
    m_title->textStyle()->setFontWeight(FontWeight::Bold);

    m_desc = new Label();
    m_desc->setTopMargin(0);
    m_desc->setBottomMargin(0);
    m_desc->setMultiline(true);
    m_desc->autoSize()->setMaxLineCount(2);

    words->add(m_title);
    words->add(m_desc);
    root->add(m_bar);
    root->add(m_thumb);
    root->add(words);
    setRoot(root);
}

void LinkPreview::show(const QVariantMap &msg)
{
    const float scale = Card::textScale(m_list);
    const int small = Card::smallSize(m_list);
    const UIPalette *palette = m_ui->palette();

    // A link typed without its scheme ("www.…") gets one, or the browser has nothing to open.
    const QString url = msg.value("linkUrl").toString();
    static const QRegExp scheme("^[a-z][a-z0-9+.-]*://", Qt::CaseInsensitive);
    m_link = url.isEmpty() ? QString() : scheme.indexIn(url) == 0 ? url : "http://" + url;

    m_bar->setBackground(palette->primary());

    m_thumb->setVisible(msg.value("hasLinkThumb").toBool());
    m_thumb->setPreferredWidth(m_ui->du(10 * scale));
    m_thumb->setPreferredHeight(m_ui->du(10 * scale));
    m_thumb->setImage(msg.value("linkThumb").value<Image>());

    Card::style(m_title, m_list, small);
    m_title->setText(msg.value("linkTitle").toString());

    const QString desc = msg.value("linkDesc").toString();
    m_desc->setVisible(!desc.isEmpty());
    Card::style(m_desc, m_list, small);
    m_desc->textStyle()->setColor(palette->secondaryTextOnPlain());
    m_desc->setText(desc);
}

// A group or channel invite is asked here (Invites), anything else goes to the browser.
void LinkPreview::onTap()
{
    if (m_link.isEmpty())
        return;
    Invites *invites = qobject_cast<Invites *>(qvariant_cast<QObject *>(m_list->property("joiner")));
    if (invites && invites->openLink(m_link))
        return;
    if (!m_invoker)
        m_invoker = new InvokeManager(this);
    InvokeRequest request;
    request.setTarget("sys.browser");
    request.setAction("bb.action.OPEN");
    request.setUri(m_link);
    InvokeTargetReply *reply = m_invoker->invoke(request);
    if (!reply) {
        qWarning() << "cards: no reply from the system to OPEN" << m_link;
        return;
    }
    connect(reply, SIGNAL(finished()), this, SLOT(onOpened()));
}

void LinkPreview::onOpened()
{
    InvokeTargetReply *reply = qobject_cast<InvokeTargetReply *>(sender());
    if (!reply)
        return;
    if (reply->error() != InvokeReplyError::None)
        qWarning() << "cards: OPEN failed, error" << reply->error();
    reply->deleteLater();
}
