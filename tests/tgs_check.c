/* Stickers gate G1 (docs/stickers-plan.md): can the phone play Telegram's animated stickers?
 *
 *   tgs_check <file.tgs> [size] [out.raw] [frame]
 *
 * Plain C, built by the NDK's qcc as the UI is, linked against libtgs.so (rlottie built by clang, its C API
 * only): the route the UI will take. Unzips the .tgs with the phone's zlib, renders every frame at size x size
 * (default 180, the bubble's) into one reused buffer, and prints the time to load, the first frame, and the mean
 * and worst frame. out.raw, if given, gets the middle frame (or frame) (BGRA premultiplied, size x size) to look at.
 * Exit 0 when every frame rendered and the middle frame drew something. */
#include <rlottie_capi.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

/* The JSON inside a .tgs, NUL-terminated; NULL on error. Telegram caps a sticker at 64 KB zipped; the JSON is at
 * most a few MB, so 16 MB is the limit. */
static char *inflate_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    static unsigned char in[1 << 20];
    const size_t n = fread(in, 1, sizeof in, f);
    fclose(f);
    size_t cap = 1 << 20, have = 0;
    char *out = malloc(cap + 1);
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (!out || inflateInit2(&zs, 15 + 16) != Z_OK) { /* 15 + 16: gzip */
        free(out);
        return NULL;
    }
    zs.next_in = in;
    zs.avail_in = (uInt)n;
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        if (have == cap) {
            char *bigger = cap < (16u << 20) ? realloc(out, cap * 2 + 1) : NULL;
            if (!bigger)
                break;
            out = bigger;
            cap *= 2;
        }
        zs.next_out = (unsigned char *)out + have;
        zs.avail_out = (uInt)(cap - have);
        ret = inflate(&zs, Z_NO_FLUSH);
        have = cap - zs.avail_out;
        if (ret != Z_OK && ret != Z_STREAM_END)
            break;
    }
    inflateEnd(&zs);
    if (ret != Z_STREAM_END) {
        free(out);
        return NULL;
    }
    out[have] = 0;
    *len = have;
    return out;
}

int main(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "-h")) {
        puts("usage: tgs_check <file.tgs> [size] [out.raw] [frame]");
        return 0;
    }
    const size_t size = argc > 2 ? (size_t)atoi(argv[2]) : 180;
    if (size < 16 || size > 1024)
        return fprintf(stderr, "bad size\n"), 2;

    double t0 = now_ms();
    size_t json_len = 0;
    char *json = inflate_file(argv[1], &json_len);
    if (!json)
        return fprintf(stderr, "%s: not a .tgs\n", argv[1]), 1;
    const double t_unzip = now_ms() - t0;

    t0 = now_ms();
    Lottie_Animation *a = lottie_animation_from_data(json, argv[1], "");
    const double t_parse = now_ms() - t0;
    free(json);
    if (!a)
        return fprintf(stderr, "%s: rlottie refused it\n", argv[1]), 1;

    size_t w = 0, h = 0;
    lottie_animation_get_size(a, &w, &h);
    const size_t frames = lottie_animation_get_totalframe(a);
    const double fps = lottie_animation_get_framerate(a);

    const size_t shown = argc > 4 ? (size_t)atoi(argv[4]) : frames / 2;
    uint32_t *buf = malloc(size * size * 4);
    double first = 0, total = 0, worst = 0;
    size_t drawn = 0;
    for (size_t i = 0; i < frames; i++) {
        t0 = now_ms();
        lottie_animation_render(a, i, buf, size, size, size * 4);
        const double t = now_ms() - t0;
        if (i == 0)
            first = t;
        else
            total += t;
        if (t > worst)
            worst = t;
        if (i == shown) {
            for (size_t p = 0; p < size * size; p++)
                drawn += (buf[p] >> 24) != 0;
            if (argc > 3) {
                FILE *o = fopen(argv[3], "wb");
                if (o) {
                    fwrite(buf, 4, size * size, o);
                    fclose(o);
                }
            }
        }
    }
    const double mean = frames > 1 ? total / (frames - 1) : first;
    /* At 30 fps, the share of one core: what playing a sticker in a bubble costs (half the frames of a 60 fps one). */
    printf("%s: %ux%u %.0f fps %u frames, %u KB json | unzip %.1f ms, parse %.1f ms | at %u px: first %.1f ms, "
           "mean %.2f ms, worst %.1f ms -> %.0f %% of a core at 30 fps | frame %u: %u%% drawn\n",
           argv[1], (unsigned)w, (unsigned)h, fps, (unsigned)frames, (unsigned)(json_len / 1024), t_unzip, t_parse,
           (unsigned)size, first, mean, worst, mean * 30 / 10, (unsigned)shown, (unsigned)(drawn * 100 / (size * size)));
    free(buf);
    lottie_animation_destroy(a);
    return frames > 0 && drawn > 0 ? 0 : 1;
}
