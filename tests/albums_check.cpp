// Host check of Albums::find, Telegram's rule: c++ -std=c++11 -I app/src tests/albums_check.cpp app/src/Albums.cpp && ./a.out
#include "Albums.h"
#include <cassert>
#include <cstdio>
static Albums::Row row(int kind, const char *album, bool deleted = false, bool spoiler = false)
{
    Albums::Row r;
    r.kind = kind;
    r.deleted = deleted;
    r.spoiler = spoiler;
    r.album = album;
    return r;
}
int main()
{
    std::vector<Albums::Row> rows;
    rows.push_back(row(1, "7"));   // 0: album 7
    rows.push_back(row(2, "7"));   // 1: album 7 (a video)
    rows.push_back(row(1, "7"));   // 2: album 7
    rows.push_back(row(1, ""));    // 3: no album
    rows.push_back(row(1, "8"));   // 4: a lone member: no run
    rows.push_back(row(1, "9"));   // 5: album 9
    rows.push_back(row(1, "9", true)); // 6: deleted breaks it
    rows.push_back(row(1, "9"));   // 7
    rows.push_back(row(5, "10"));  // 8: documents never group
    rows.push_back(row(5, "10"));  // 9
    std::vector<Albums::Run> runs = Albums::find(rows);
    assert(runs.size() == 1);
    assert(runs[0].start == 0 && runs[0].count == 3);
    rows.clear();
    rows.push_back(row(1, "3"));
    rows.push_back(row(1, "3"));
    runs = Albums::find(rows);
    assert(runs.size() == 1 && runs[0].count == 2);
    std::puts("albums ok");
    return 0;
}
