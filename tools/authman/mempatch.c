// Finds a NUL-terminated string in a running process's writable memory and, with -w, replaces it in place.
//
//   mempatch <pid> '<find>'                 lists every match (address and what follows it)
//   mempatch <pid> '<find>' -w '<replace>'  writes <replace> and its NUL over the one match, then reads it back
//
// For authman's _sys_headless_xlarge class (docs/headless-cap.md): its RLM action keeps only the text
// "rlm RLIMIT_DATA 31457280" (arlm_create, in a 0x200-byte buffer), which the launcher parses at each launch;
// the number itself is never stored. Root writes another process's memory through /proc/<pid>/as. The write
// refuses unless there is exactly one match. The tool cannot see the buffer's size: the caller keeps the
// replacement short (authman's buffer holds 0x200 bytes for a ~25-byte text; this caps it at 0x1c4).
#include <devctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/procfs.h>
#include <unistd.h>

enum { kMaxMaps = 4096, kMaxHits = 16 };

int main(int argc, char **argv) {
  if (argc != 3 && !(argc == 5 && strcmp(argv[3], "-w") == 0)) {
    fprintf(stderr, "usage: mempatch <pid> '<find>' [-w '<replace>']\n");
    return 2;
  }
  const char *find = argv[2];
  const size_t flen = strlen(find) + 1;  // with its NUL: the whole string, not a prefix of a longer one
  const char *repl = argc == 5 ? argv[4] : NULL;
  if (repl && strlen(repl) + 1 > 0x200 - 0x3c) {
    fprintf(stderr, "replacement too long\n");
    return 2;
  }
  char path[64];
  snprintf(path, sizeof path, "/proc/%s/as", argv[1]);
  int fd = open(path, repl ? O_RDWR : O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "%s: %s\n", path, strerror(errno));
    return 1;
  }
  static procfs_mapinfo maps[kMaxMaps];
  int n = 0;
  if (devctl(fd, DCMD_PROC_MAPINFO, maps, sizeof maps, &n) != EOK || n <= 0) {
    fprintf(stderr, "mapinfo: %s\n", strerror(errno));
    return 1;
  }
  if (n > kMaxMaps) {
    n = kMaxMaps;
  }
  uint64_t hits[kMaxHits];
  int nh = 0, scanned = 0;
  for (int i = 0; i < n; ++i) {
    // Every region up to 64 MB: procfs_mapinfo.flags don't say "writable" in the PROT_ bits, so no filter on
    // them; a region that can't be read is skipped below, and the write refuses anything but one match.
    if (maps[i].size == 0 || maps[i].size > (64u << 20)) {
      continue;
    }
    ++scanned;
    char *buf = malloc(maps[i].size);
    if (!buf) {
      continue;
    }
    if (pread(fd, buf, maps[i].size, (off_t)maps[i].vaddr) == (ssize_t)maps[i].size) {
      for (size_t off = 0; off + flen <= maps[i].size; ++off) {
        if (buf[off] == find[0] && memcmp(buf + off, find, flen) == 0) {
          if (nh < kMaxHits) {
            hits[nh] = maps[i].vaddr + off;
          }
          ++nh;
        }
      }
    }
    free(buf);
  }
  printf("%d regions of %d scanned, %d match(es)\n", scanned, n, nh);
  for (int h = 0; h < nh && h < kMaxHits; ++h) {
    printf("  0x%08llx\n", (unsigned long long)hits[h]);
  }
  if (!repl) {
    return nh > 0 ? 0 : 1;
  }
  if (nh != 1) {
    fprintf(stderr, "not writing: %d matches, want exactly 1\n", nh);
    return 1;
  }
  const size_t rlen = strlen(repl) + 1;
  if (pwrite(fd, repl, rlen, (off_t)hits[0]) != (ssize_t)rlen) {
    fprintf(stderr, "write: %s\n", strerror(errno));
    return 1;
  }
  char back[0x200];
  memset(back, 0, sizeof back);
  if (pread(fd, back, rlen, (off_t)hits[0]) != (ssize_t)rlen || memcmp(back, repl, rlen) != 0) {
    fprintf(stderr, "read back differs\n");
    return 1;
  }
  printf("wrote '%s' at 0x%08llx, read back\n", back, (unsigned long long)hits[0]);
  close(fd);
  return 0;
}
