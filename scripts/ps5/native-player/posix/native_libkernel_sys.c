/* The native loader does not load libkernel_sys.sprx. Provide checked failure
 * results for its unavailable filesystem and process APIs, preventing calls
 * through unresolved imports. mpv treats failed fstatfs as a local stream;
 * fontconfig handles failed symbolic-link queries and locking.
 */
#ifndef PS5_NATIVE_GPU
#error "Compile native POSIX support only for PS5_NATIVE_GPU"
#endif

#include <errno.h>
#include <stddef.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <unistd.h>

/* The filesystem a descriptor lives on cannot be asked for here. Reporting
   failure is what makes mpv treat a local file as local. */
int fstatfs(int descriptor, struct statfs *out)
{
    (void)descriptor;
    (void)out;
    errno = ENOSYS;
    return -1;
}

/* Nothing in this sandbox creates symbolic links, and EINVAL is precisely
   "the named file is not a symbolic link", which is true of every path here. */
ssize_t readlink(const char *path, char *buffer, size_t size)
{
    (void)path;
    (void)buffer;
    (void)size;
    errno = EINVAL;
    return -1;
}

int link(const char *existing, const char *created)
{
    (void)existing;
    (void)created;
    errno = EOPNOTSUPP;
    return -1;
}

int symlink(const char *target, const char *created)
{
    (void)target;
    (void)created;
    errno = EOPNOTSUPP;
    return -1;
}

/* This application never intends to start a child process, and the platform
   has no way to. A caller that asks gets the failure it is written to handle. */
pid_t fork(void)
{
    errno = ENOSYS;
    return -1;
}

pid_t setsid(void)
{
    errno = EPERM;
    return -1;
}
