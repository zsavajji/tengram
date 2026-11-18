#include "Albums.h"

static const int kMin = 2;

static bool fits(const Albums::Row &r)
{
    return (r.kind == 1 || r.kind == 2) && !r.deleted && !r.spoiler && !r.album.empty();
}

static bool follows(const Albums::Row &p, const Albums::Row &r)
{
    return r.album == p.album;
}

std::vector<Albums::Run> Albums::find(const std::vector<Row> &rows)
{
    std::vector<Run> runs;
    const int n = int(rows.size());
    int i = 0;
    while (i < n) {
        if (!fits(rows[i])) {
            ++i;
            continue;
        }
        int j = i + 1;
        while (j < n && fits(rows[j]) && follows(rows[j - 1], rows[j]))
            ++j;
        if (j - i >= kMin) {
            Run r = { i, j - i };
            runs.push_back(r);
        }
        i = j;
    }
    return runs;
}
