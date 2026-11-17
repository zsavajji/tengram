#include "Albums.h"

static const int kMin = 4;
static const long long kGap = 60;

static bool fits(const Albums::Row &r)
{
    return (r.kind == 1 || r.kind == 2) && !r.deleted && !r.text && !r.quote && !r.spoiler && !r.forwarded;
}

// r may follow p in an album. Same sender: ours are all out, a group's by messages.sender.
static bool follows(const Albums::Row &p, const Albums::Row &r)
{
    return r.out == p.out && r.sender == p.sender && r.day == p.day && r.ts - p.ts <= kGap;
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
