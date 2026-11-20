// Mp4Length against files written here: a plain MP4 (moov after mdat, mvhd version 1), a fragmented one (mvhd 0,
// the length in mvex's mehd), and junk.
//   c++ -I app/src app/tests/mp4length_test.cpp -o /tmp/mp4length_test && /tmp/mp4length_test
// With a path it prints that file's length instead.
#include "Mp4Length.h"

#include <assert.h>
#include <stdlib.h>
#include <string>

static std::string num(unsigned long long v, int n)
{
    std::string s;
    for (int i = n - 1; i >= 0; --i) {
        s += (char)(v >> (8 * i));
    }
    return s;
}

static std::string box(const char *type, const std::string &body)
{
    return num(body.size() + 8, 4) + type + body;
}

static const char *kPath = "/tmp/mp4length_test.mp4";

static void write(const std::string &bytes)
{
    FILE *f = fopen(kPath, "wb");
    assert(f && fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size());
    fclose(f);
}

static long long length(const std::string &bytes)
{
    write(bytes);
    const long long ms = Mp4Length::ms(kPath);
    remove(kPath);
    return ms;
}

static bool fragmented(const std::string &bytes)
{
    write(bytes);
    const bool yes = Mp4Length::fragmented(kPath);
    remove(kPath);
    return yes;
}

int main(int argc, char **argv)
{
    if (argc == 2) {
        printf("%lld ms\n", Mp4Length::ms(argv[1]));
        return 0;
    }
    const std::string ftyp = box("ftyp", "mp41....");
    const std::string rest(80, '\0'); // mvhd's rate, volume, matrix and next track id
    // version 0: flags, two dates, timescale 15360, length
    const std::string mvhd0 = box("mvhd", num(0, 4) + num(0, 8) + num(15360, 4) + num(0, 4) + rest);
    const std::string mvhd1 = box("mvhd", num(1 << 24, 4) + num(0, 16) + num(600, 4) + num(6300, 8) + rest);
    const std::string mehd = box("mehd", num(0, 4) + num(3452416, 4));
    const std::string trak = box("trak", std::string(40, 'x'));

    assert(length(ftyp + box("mdat", std::string(1000, 'm')) + box("moov", mvhd1 + trak)) == 10500);
    assert(length(ftyp + box("moov", mvhd0 + trak + box("mvex", mehd + box("trex", std::string(24, '\0'))))
                  + box("moof", "frag") + box("mdat", "m")) == 224766);
    assert(length(ftyp + box("moov", mvhd0 + trak + box("mvex", box("trex", std::string(24, '\0'))))) == 0); // no mehd
    assert(length("not an mp4 at all, not at all") == 0);
    assert(length(num(4000, 4) + "moov" + mvhd1) == 0); // a box longer than the file
    assert(Mp4Length::ms("/nonexistent") == 0);

    assert(!fragmented(ftyp + box("mdat", std::string(1000, 'm')) + box("moov", mvhd1 + trak)));
    assert(fragmented(ftyp + box("moov", mvhd0 + trak + box("mvex", mehd + box("trex", std::string(24, '\0'))))
                      + box("moof", "frag") + box("mdat", "m")));
    assert(fragmented(ftyp + box("moov", mvhd0 + trak + box("mvex", box("trex", std::string(24, '\0'))))));
    assert(!fragmented("not an mp4 at all, not at all"));
    assert(!Mp4Length::fragmented("/nonexistent"));
    printf("ok\n");
    return 0;
}
