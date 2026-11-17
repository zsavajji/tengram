// Run with Qt 4 or 5 core (the Mac has none; a debian container with qtbase5-dev does):
//   g++ -fPIC -std=c++11 -I app/src $(pkg-config --cflags --libs Qt5Core) app/tests/markup_test.cpp -o /tmp/markup_test && /tmp/markup_test
#include "Markup.h"

#include <assert.h>
#include <stdio.h>

static void check(const char *in, const char *want)
{
    const QString got = Markup::html(QString::fromUtf8(in));
    if (got != QString::fromUtf8(want)) {
        printf("FAIL %s\n  want %s\n  got  %s\n", in, want, got.toUtf8().constData());
        assert(false);
    }
}

int main()
{
    check("*bold*", "<b>bold</b>");
    check("a *b* c", "a <b>b</b> c");
    check("_it_ ~gone~ `mono`", "<i>it</i> <s>gone</s> <tt>mono</tt>");
    check("```x\ny```", "<tt>x<br/>y</tt>");
    check("*_both_*", "<b><i>both</i></b>");
    check("(*x*) and *y*.", "(<b>x</b>) and <b>y</b>.");
    check("a < b *x* & c", "a &lt; b <b>x</b> &amp; c");
    check("`*kept*`", "<tt>*kept*</tt>");
    check("*two*\n*lines*", "<b>two</b><br/><b>lines</b>");
    // Not a style: nothing comes back and the Label shows the plain text.
    check("2*3*4", "");
    check("* a *", "");
    check("snake_case_name", "");
    check("*a\nb*", "");
    check("**", "");
    check("no marks at all", "");
    // A mention (a private-use char) is one word to the styles.
    check("*\xEE\x80\x80*", "<b>\xEE\x80\x80</b>");
    puts("ok");
    return 0;
}
