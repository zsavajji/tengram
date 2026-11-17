#ifndef MARKUP_H
#define MARKUP_H

#include <QString>

// WhatsApp's text styles as the rich text a Label shows: *bold*, _italic_, ~strike~, `mono` and
// ```block```. Used by MessageKinds::html. Only QString, so app/tests/markup_test.cpp builds on the Mac.
namespace Markup {

// The text as rich text needs it: its own markup escaped, a newline a <br/>.
inline QString escaped(QString s)
{
    s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;");
    return s.replace('\n', "<br/>");
}

inline bool word(const QString &s, int i)
{
    return i >= 0 && i < s.size() && s.at(i).isLetterOrNumber();
}

// Where the span whose `n`-char mark opens at s[i] closes (the index of its closing mark), or -1.
// WhatsApp's rule: the opening mark follows a non-letter (or starts the text) and a non-space
// follows it; the closing one follows a non-space and a non-letter (or the end) follows it; a span
// stays on its line, a block (n == 3) doesn't have to.
inline int closing(const QString &s, int i, int n, int to)
{
    const QString mark = s.mid(i, n);
    const bool block = n == 3;
    if (word(s, i - 1) || i + n >= to || (!block && (s.at(i + n).isSpace() || s.at(i + n) == mark.at(0))))
        return -1;
    for (int k = i + n + 1; k + n <= to; ++k) {
        if (!block && s.at(k) == '\n')
            return -1;
        if (s.at(k) == mark.at(0) && s.mid(k, n) == mark && (block || !s.at(k - 1).isSpace()) && !word(s, k + n))
            return k;
    }
    return -1;
}

// s[from, to) as rich text; `styled` says whether any span was made.
inline QString span(const QString &s, int from, int to, bool &styled)
{
    QString out;
    for (int i = from; i < to;) {
        const QChar c = s.at(i);
        const int n = s.mid(i, 3) == "```" ? 3 : 1;
        const char *tag = c == '*' ? "b" : c == '_' ? "i" : c == '~' ? "s" : c == '`' ? "tt" : 0;
        const int k = tag ? closing(s, i, n, to) : -1;
        if (k < 0) {
            out += escaped(QString(c));
            ++i;
            continue;
        }
        styled = true;
        // Mono keeps its marks and text as typed; the others nest.
        const QString inner = c == '`' ? escaped(s.mid(i + n, k - i - n)) : span(s, i + n, k, styled);
        out += QString("<%1>%2</%1>").arg(QString::fromLatin1(tag)).arg(inner);
        i = k + n;
    }
    return out;
}

// The text as rich text, "" when it has no style (the Label then shows it as plain text).
inline QString html(const QString &s)
{
    if (s.indexOf('*') < 0 && s.indexOf('_') < 0 && s.indexOf('~') < 0 && s.indexOf('`') < 0)
        return QString();
    bool styled = false;
    const QString out = span(s, 0, s.size(), styled);
    return styled ? out : QString();
}

} // namespace Markup

#endif
