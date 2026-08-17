#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

// SQLite's POSIX amalgamation probes these two optional calls even though
// Vita newlib does not export them. Ownership changes are irrelevant to the
// single-user Vita filesystem. Reporting no symlink target preserves
// SQLite's normal canonical-path fallback without fabricating one.
int fchown(int descriptor, uid_t owner, gid_t group) {
    (void)descriptor;
    (void)owner;
    (void)group;
    return 0;
}

ssize_t readlink(const char* path, char* buffer, size_t size) {
    (void)path;
    (void)buffer;
    (void)size;
    errno = EINVAL;
    return -1;
}
