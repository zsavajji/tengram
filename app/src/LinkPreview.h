#ifndef LINKPREVIEW_H
#define LINKPREVIEW_H

#include <bb/cascades/CustomControl>
#include <QString>
#include <QVariantMap>

namespace bb {
namespace cascades {
class Container;
class ImageView;
class Label;
class ListView;
class UIConfig;
}
namespace system {
class InvokeManager;
}
}

// A text's link preview (step 6.5f), under the text and drawn as the quote above it is: a
// primary-colour bar at the left, then the picture (when there is one), the title and the
// description. A tap opens the link in the browser. Mirrors LinkPreview.qml (gone since step 8.6).
class LinkPreview : public bb::cascades::CustomControl
{
    Q_OBJECT
public:
    explicit LinkPreview(bb::cascades::ListView *list);

    // msg is a "msg" row of MessageModel (linkTitle, linkDesc, linkUrl, linkThumb, hasLinkThumb).
    void show(const QVariantMap &msg);

private slots:
    void onTap();
    void onOpened();

private:
    bb::cascades::ListView *m_list;
    bb::cascades::UIConfig *m_ui;
    bb::cascades::Container *m_bar;
    bb::cascades::ImageView *m_thumb;
    bb::cascades::Label *m_title;
    bb::cascades::Label *m_desc;
    bb::system::InvokeManager *m_invoker; // made at the first tap
    QString m_link;
};

#endif
