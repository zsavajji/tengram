// Sends text commands to authman (/dev/authman) as the launcher does, and prints what each returns.
//
//   authman-send [-u user] '<command>' ...      e.g. -u carrier_mgr 'regprefix hmem' 'load hmem /path/hmem'
//
// The wire format is the launcher's (disassembled, docs/headless-cap.md): an _IO_MSG with authman's manager id
// 0xf001 and a 16-byte header, then the command with its NUL; the reply comes back after the same header.
// -u: authman checks its client whitelist by user name (authman.cfg: carrier_mgr may regprefix, load and
// delcap) and reads load's files with the client's uid, so a root caller becomes that user first.
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/neutrino.h>
#include <unistd.h>

struct header {
  uint16_t type;         // _IO_MSG
  uint16_t combine_len;  // 16
  uint16_t mgrid;        // authman's: 0xf001
  uint16_t subtype;      // 0
  uint32_t slen;         // the command's length with its NUL
  uint32_t rlen;         // the reply buffer's size
};

int main(int argc, char **argv) {
  int i = 1;
  if (argc > 2 && strcmp(argv[1], "-u") == 0) {
    struct passwd *pw = getpwnam(argv[2]);
    if (!pw) {
      fprintf(stderr, "no user %s\n", argv[2]);
      return 2;
    }
    if (initgroups(pw->pw_name, pw->pw_gid) != 0 || setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0) {
      fprintf(stderr, "cannot become %s: %s\n", argv[2], strerror(errno));
      return 2;
    }
    i = 3;
  }
  if (i >= argc) {
    fprintf(stderr, "usage: authman-send [-u user] '<command>' ...\n");
    return 2;
  }
  int fd = open("/dev/authman", O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "/dev/authman: %s\n", strerror(errno));
    return 1;
  }
  int failed = 0;
  for (; i < argc; ++i) {
    struct header h = {0x113, 16, 0xf001, 0, (uint32_t)strlen(argv[i]) + 1, 0};
    char reply[1024];
    struct header rh;
    h.rlen = sizeof reply - 1;
    iov_t s[2], r[2];
    SETIOV(&s[0], &h, sizeof h);
    SETIOV(&s[1], argv[i], h.slen);
    SETIOV(&r[0], &rh, sizeof rh);
    SETIOV(&r[1], reply, sizeof reply - 1);
    memset(reply, 0, sizeof reply);
    int st = MsgSendv(fd, s, 2, r, 2);
    if (st < 0) {
      printf("%s: error %d (%s)\n", argv[i], errno, strerror(errno));
      failed = 1;
    } else {
      printf("%s: ok, status %d, reply '%s'\n", argv[i], st, reply);
    }
  }
  close(fd);
  return failed;
}
