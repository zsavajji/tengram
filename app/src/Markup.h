#ifndef MARKUP_H
#define MARKUP_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <climits>

// Telegram's formatting of a text (docs/protocol.md, extra.ent: [offset, length, type(, argument)] in UTF-16 units,
// which is what a QString counts; sorted, one inside another or apart, never crossing) as the rich text a Label shows
// (html) and as the Markdown the composer takes back to edit a message (source), which the engine reads again
// (TDLib's parseMarkdown), and cut at its code blocks for the card's boxes (parts). Only QtCore, so app/tests/markup_test.cpp builds off the phone.
namespace Markup {

// What covers a spoiler: grey on grey, until a tap on its card (MessageCard::onTouch) reads the row again uncovered
// (MessageModel::unspoil). Not a link of ours: its tap never came (1.0.5.192).
const char *const kVeil = "<span style=\"color:#808080;background-color:#808080\">";

// The text as rich text needs it: its own markup escaped, a newline a <br/>.
inline QString escaped(QString s)
{
    s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;");
    return s.replace('\n', "<br/>");
}

// What opens and closes the entity `e` around `covered`, the text it spans: tags (rich) or Markdown marks. False for
// a type that shows nothing: a hashtag, a bot command, a quote; an e-mail address or a phone number, which the Label links
// by itself; in Markdown also what has no mark (underline, a bare link) and, in rich text, a spoiler tapped open.
// `base` is where the rendered text starts in the row's whole text (a part of it, see parts): a code's link names the
// whole text's offsets.
inline bool marks(const QVariantList &e, const QString &covered, bool rich, bool revealed, int base, QString &open,
                  QString &close)
{
    const QString type = e.value(2).toString();
    const QString arg = e.value(3).toString();
    if (!rich) {
        const char *mark = type == "b" ? "**" : type == "i" ? "__" : type == "s" ? "~~" : type == "sp" ? "||"
                           : type == "c" ? "`" : type == "p" ? "```" : 0;
        if (mark) {
            open = close = QString::fromLatin1(mark);
            // As TDLib's getMarkdownText: the language, then the code from a line of its own.
            if (type == "p")
                open += arg + (covered.startsWith('\n') ? "" : "\n");
        } else if ((type == "a" || type == "m") && !arg.isEmpty()) {
            open = "[";
            close = "](" + (type == "m" ? "tg://user?id=" + arg : arg) + ")";
        } else {
            return false;
        }
        return true;
    }
    const char *tag = type == "b" ? "b" : type == "i" ? "i" : type == "u" ? "u" : type == "s" ? "s" : 0;
    QString href;
    if (tag) {
        open = QString("<%1>").arg(QString::fromLatin1(tag));
        close = QString("</%1>").arg(QString::fromLatin1(tag));
        return true;
    } else if (type == "c") {
        // The phone's own monospace fonts by name: a <tt> is drawn in the body's (the user, 1.0.5.194: "monospace does not
        // show", nor a code block), and so was the family in quotes (1.0.5.196's screenshot); the names go bare, in the
        // shape of the body's own "sans-serif, HBEmoji" (main.qml). A link of ours inside it, so a tap copies the code
        // (MessageCard::onLink takes text[offset, length), the row's own numbers), as Telegram's apps do; not underlined
        // as a link is (the user, 1.0.5.196: "i see code underlined"). DejaVu Sans Mono is wider and taller than the body's
        // font at one size (the user, 1.0.5.197: "monospace font is too big"), so it goes smaller; in a span of its own, so
        // a size the Label can't read costs the size alone, not the font. A code block (p) never comes here as rich text:
        // parts() cuts it out, and the card draws it as a box.
        open = "<span style=\"font-family:DejaVu Sans Mono, Andale Mono, monospace\"><span style=\"font-size:85%\">"
               "<a href=\"hbcopy:" + QString::number(base + e.value(0).toInt()) + "," + QString::number(e.value(1).toInt())
               + "\" style=\"text-decoration:none\">";
        close = "</a></span></span>";
        return true;
    } else if (type == "sp") {
        if (revealed)
            return false;
        open = QString::fromLatin1(kVeil);
        close = "</span>";
        return true;
    } else if (type == "a") {
        // The link's address, else the text itself: Telegram also links "example.com", which the Label alone would not.
        href = arg.isEmpty() ? covered : arg;
        if (!href.contains("://"))
            href.prepend("http://");
    } else if (type == "m") {
        // By id: the person's page (MessageCard::onLink). "@name": Telegram's link to it, which Invites opens.
        href = arg.isEmpty() ? "https://t.me/" + covered.mid(1) : "hbmention:" + arg;
    } else {
        return false;
    }
    open = "<a href=\"" + escaped(href) + "\">";
    close = "</a>";
    return true;
}

// text[from, to) as it goes out: escaped and without U+20E3, a keycap's frame, as the Label's plain text drops it
// (Card::noKeycaps), for rich text; as it is for Markdown.
inline QString piece(const QString &text, int from, int to, bool rich)
{
    const QString s = text.mid(from, to - from);
    return rich ? escaped(s).remove(QChar(0x20E3)) : s;
}

// The text with its entities applied. Rich: "" when none of them shows (the Label then takes the plain text).
inline QString render(const QString &text, const QVariantList &ents, bool rich, bool revealed, int base)
{
    QString out;
    QList<int> ends;    // where each open entity closes, the innermost last
    QStringList closes; // and with what
    int pos = 0, next = 0;
    bool any = false;
    while (next < ents.size() || !ends.isEmpty()) {
        const QVariantList e = next < ents.size() ? ents.at(next).toList() : QVariantList();
        const int opens = next < ents.size() ? qBound(pos, e.value(0).toInt(), text.size()) : INT_MAX;
        if (!ends.isEmpty() && ends.last() <= opens) {
            out += piece(text, pos, ends.last(), rich) + closes.takeLast();
            pos = ends.takeLast();
            continue;
        }
        ++next;
        // An entity ends inside the one that holds it: Telegram's never cross, and a damaged row's must not either.
        const int end = qMin(opens + qMin(e.value(1).toInt(), text.size()), ends.isEmpty() ? text.size() : ends.last());
        QString open, close;
        if (end <= opens || !marks(e, text.mid(opens, end - opens), rich, revealed, base, open, close))
            continue;
        any = true;
        out += piece(text, pos, opens, rich) + open;
        pos = opens;
        ends << end;
        closes << close;
    }
    out += piece(text, pos, text.size(), rich);
    return any || !rich ? out : QString();
}

// The text as rich text, "" when it has no formatting to show. revealed: its spoilers are open.
inline QString html(const QString &text, const QVariantList &ents, bool revealed = false, int base = 0)
{
    return ents.isEmpty() ? QString() : render(text, ents, true, revealed, base);
}

// The text as the Markdown that sends it again with the same formatting.
inline QString source(const QString &text, const QVariantList &ents)
{
    return render(text, ents, false, false, 0);
}

// Whether the entities have one of `type`.
inline bool has(const QVariantList &ents, const char *type)
{
    foreach (const QVariant &v, ents) {
        if (v.toList().value(2).toString() == QLatin1String(type))
            return true;
    }
    return false;
}

// The entities over text[a, b) as that piece's own: cut to it, counted from its start. Code blocks are left out.
inline QVariantList slice(const QVariantList &ents, int a, int b)
{
    QVariantList out;
    foreach (const QVariant &v, ents) {
        QVariantList e = v.toList();
        if (e.size() < 3 || e.at(2).toString() == "p")
            continue;
        const int from = qMax(e.at(0).toInt(), a);
        const int to = qMin(e.at(0).toInt() + qMin(e.at(1).toInt(), b), b);
        if (to <= from)
            continue;
        e[0] = from - a;
        e[1] = to - from;
        out << QVariant(e);
    }
    return out;
}

// text[a, b), the text between two code blocks, as a part: {code: false, text, html} ("" html when it has no formatting
// to show). The line break that set it apart from a block goes: the box is a line of its own. Nothing for an empty one.
inline QVariantList between(const QString &text, const QVariantList &ents, int a, int b, bool revealed)
{
    if (a > 0 && a < b && text.at(a) == '\n')
        ++a;
    if (b < text.size() && b > a && text.at(b - 1) == '\n')
        --b;
    QVariantList out;
    if (b > a) {
        QVariantMap part;
        part["code"] = false;
        part["text"] = text.mid(a, b - a);
        part["html"] = html(text.mid(a, b - a), slice(ents, a, b), revealed, a);
        out << QVariant(part);
    }
    return out;
}

// The text cut at its code blocks, in order: each block {code: true, text, lang} and the text around them as between()
// gives it. Empty when it has no block: then the whole text is html()'s. The card draws a block as a box of its own
// (MessageCard::fillBlocks), which a Label's rich text can't (the user, 1.0.5.198: "inside its own box, similar to stock").
inline QVariantList parts(const QString &text, const QVariantList &ents, bool revealed)
{
    QVariantList out;
    int pos = 0;
    foreach (const QVariant &v, ents) {
        const QVariantList e = v.toList();
        const int from = qBound(pos, e.value(0).toInt(), text.size());
        const int to = qMin(from + qMin(e.value(1).toInt(), text.size()), text.size());
        if (e.value(2).toString() != "p" || to <= from)
            continue;
        out += between(text, ents, pos, from, revealed);
        QVariantMap block;
        block["code"] = true;
        block["text"] = text.mid(from, to - from);
        block["lang"] = e.value(3).toString();
        out << QVariant(block);
        pos = to;
    }
    if (!out.isEmpty())
        out += between(text, ents, pos, text.size(), revealed);
    return out;
}

} // namespace Markup

#endif
