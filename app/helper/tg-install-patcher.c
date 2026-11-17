/* tg-install-patcher command [args...]
 *
 * On BB10 every process carries the application id of the app that started it, children
 * included, and the system ends all of them when that app exits. Started from Tengram, the
 * headless-mem addon install would die with the app if the user closed it midway. This starts the
 * command, the install of that addon, as a new application (root only) and in its own session,
 * without waiting for it. */
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>

extern char **environ;

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: tg-install-patcher command [args...]\n");
        return 2;
    }

    posix_spawnattr_t attr;
    pid_t pid;
    int rc = posix_spawnattr_init(&attr);
    if (rc == 0) {
        /* The xflags setter takes 32 bits: the short one behind setflags would drop NEWAPP. */
        rc = posix_spawnattr_setxflags(&attr, POSIX_SPAWN_NEWAPP | POSIX_SPAWN_SETSID | POSIX_SPAWN_NOZOMBIE);
        if (rc == 0)
            rc = posix_spawnp(&pid, argv[1], NULL, &attr, argv + 1, environ);
        posix_spawnattr_destroy(&attr);
    }
    if (rc != 0) {
        fprintf(stderr, "tg-install-patcher: %s\n", strerror(rc));
        return 1;
    }
    return 0;
}
