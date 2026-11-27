// Run with Qt 4 or 5 core (the Mac has none; a debian container with qtbase5-dev does):
//   g++ -fPIC -std=c++11 -I app/src $(pkg-config --cflags Qt5Core) app/tests/markup_test.cpp $(pkg-config --libs Qt5Core) -o /tmp/markup_test && /tmp/markup_test
#include "Markup.h"

#include <assert.h>
#include <stdio.h>

#define MONO "<span style=\"font-family:DejaVu Sans Mono, Andale Mono, monospace\"><span style=\"font-size:85%\">"

static QVariant ent(int at, int length, const char *type, const char *arg = 0)
{
    QVariantList e;
    e << at << length << QString::fromLatin1(type);
    if (arg)
        e << QString::fromUtf8(arg);
    return e;
}

static void same(const QString &got, const char *want)
{
    if (got != QString::fromUtf8(want)) {
        printf("FAIL\n  want %s\n  got  %s\n", want, got.toUtf8().constData());
        assert(false);
    }
}

static QString html(const char *text, const QVariantList &ents, bool revealed = false)
{
    return Markup::html(QString::fromUtf8(text), ents, revealed);
}

static QString part(const QVariantList &parts, int i, const char *key)
{
    return parts.value(i).toMap().value(key).toString();
}

static QVariantList parts(const char *text, const QVariantList &ents, bool revealed = false)
{
    return Markup::parts(QString::fromUtf8(text), ents, revealed);
}

static QString source(const char *text, const QVariantList &ents)
{
    return Markup::source(QString::fromUtf8(text), ents);
}

int main()
{
    typedef QVariantList L;
    // Rich text.
    same(html("bold plain", L() << ent(0, 4, "b")), "<b>bold</b> plain");
    same(html("abcdef", L() << ent(0, 6, "b") << ent(2, 2, "i")), "<b>ab<i>cd</i>ef</b>");
    same(html("ab", L() << ent(0, 2, "b") << ent(0, 2, "i")), "<b><i>ab</i></b>");
    same(html("a<b\nc", L() << ent(0, 1, "i")), "<i>a</i>&lt;b<br/>c");
    same(html("u s c p", L() << ent(0, 1, "u") << ent(2, 1, "s") << ent(4, 1, "c") << ent(6, 1, "p", "cpp")),
         "<u>u</u> <s>s</s> " MONO "<a href=\"hbcopy:4,1\" style=\"text-decoration:none\">c</a></span></span> p");
    same(html("site", L() << ent(0, 4, "a", "https://x.y/?a=1&b=2")), "<a href=\"https://x.y/?a=1&amp;b=2\">site</a>");
    same(html("see example.com", L() << ent(4, 11, "a")), "see <a href=\"http://example.com\">example.com</a>");
    same(html("https://t.me/x", L() << ent(0, 14, "a")), "<a href=\"https://t.me/x\">https://t.me/x</a>");
    same(html("hi Ann and @bob", L() << ent(3, 3, "m", "42") << ent(11, 4, "m")),
         "hi <a href=\"hbmention:42\">Ann</a> and <a href=\"https://t.me/bob\">@bob</a>");
    same(html("a secret", L() << ent(2, 6, "sp")),
         "a <span style=\"color:#808080;background-color:#808080\">secret</span>");
    // Offsets count UTF-16 units (an emoji is two), and a keycap's frame goes without moving them.
    same(html("\xF0\x9F\x98\x80 x", L() << ent(3, 1, "b")), "\xF0\x9F\x98\x80 <b>x</b>");
    same(html("1\xE2\x83\xA3 x", L() << ent(3, 1, "b")), "1 <b>x</b>");
    // A damaged row: past the end, crossing.
    same(html("abc", L() << ent(1, 50, "b")), "a<b>bc</b>");
    same(html("abcd", L() << ent(0, 2, "b") << ent(1, 3, "i")), "<b>a<i>b</i></b>cd");
    // Nothing to show: "" comes back and the Label shows the plain text.
    same(html("a secret", L() << ent(2, 6, "sp"), true), "");
    same(html("#tag /cmd", L() << ent(0, 4, "h") << ent(5, 4, "cmd")), "");
    same(html("abc", L() << ent(9, 2, "b")), "");
    same(html("no entities", L()), "");
    // Code blocks are cut out as parts of their own; the text around them loses the line break beside the block.
    same(QString::number(parts("no code", L() << ent(0, 2, "b")).size()), "0");
    L p = parts("see\nx=1\nbold end", L() << ent(4, 3, "p", "py") << ent(8, 4, "b"));
    same(QString::number(p.size()), "3");
    same(part(p, 0, "code") + "|" + part(p, 0, "text") + "|" + part(p, 0, "html"), "false|see|");
    same(part(p, 1, "code") + "|" + part(p, 1, "text") + "|" + part(p, 1, "lang"), "true|x=1|py");
    same(part(p, 2, "text") + "|" + part(p, 2, "html"), "bold end|<b>bold</b> end");
    // A style over a block is cut to each side of it, and a block alone is one part.
    p = parts("ab\ncd\nef", L() << ent(0, 8, "b") << ent(3, 2, "p"));
    same(part(p, 0, "html") + "|" + part(p, 1, "text") + "|" + part(p, 2, "html"), "<b>ab</b>|cd|<b>ef</b>");
    p = parts("x=1", L() << ent(0, 3, "p"));
    same(QString::number(p.size()) + part(p, 0, "text") + part(p, 0, "lang"), "1x=1");
    // Code in the text after a block names its place in the whole text, which is what a tap copies from.
    p = parts("x=1\nrun ls", L() << ent(0, 3, "p") << ent(8, 2, "c"));
    same(part(p, 1, "html"), "run " MONO "<a href=\"hbcopy:8,2\" style=\"text-decoration:none\">ls</a></span></span>");
    // The Markdown the composer edits.
    same(source("bold it gone hid mono", L() << ent(0, 4, "b") << ent(5, 2, "i") << ent(8, 4, "s") << ent(13, 3, "sp")
                                             << ent(17, 4, "c")),
         "**bold** __it__ ~~gone~~ ||hid|| `mono`");
    same(source("abcdef", L() << ent(0, 6, "b") << ent(2, 2, "i")), "**ab__cd__ef**");
    same(source("x=1", L() << ent(0, 3, "p", "python")), "```python\nx=1```");
    same(source("x=1", L() << ent(0, 3, "p")), "```\nx=1```");
    same(source("site Ann", L() << ent(0, 4, "a", "https://x.y") << ent(5, 3, "m", "42")),
         "[site](https://x.y) [Ann](tg://user?id=42)");
    same(source("u example.com", L() << ent(0, 1, "u") << ent(2, 11, "a")), "u example.com");
    same(source("a < b", L()), "a < b");
    puts("ok");
    return 0;
}
