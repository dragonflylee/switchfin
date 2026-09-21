/* The native runtime does not provide getcwd or load libkernel_sys.sprx.
 * Identify the fixed working directory by device and inode among the sandbox
 * roots. The application never calls chdir and uses absolute file paths; an
 * unrecognized directory therefore falls back to "/".
 */
#ifndef PS5_NATIVE_GPU
#error "Compile native POSIX support only for PS5_NATIVE_GPU"
#endif

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include <sys/stat.h>

static int native_directory(char *buffer, size_t size)
{
    static const char *const roots[] = {
        "/", "/app0", "/download0", "/system_tmp", "/av_contents", "/dev",
    };
    struct stat here;
    const char *answer = "/";
    size_t index, length;

    if (stat(".", &here) == 0) {
        for (index = 0; index < sizeof roots / sizeof roots[0]; ++index) {
            struct stat root;
            if (stat(roots[index], &root) == 0 &&
                root.st_dev == here.st_dev && root.st_ino == here.st_ino) {
                answer = roots[index];
                break;
            }
        }
    }
    length = strlen(answer);
    /* The caller turns this into the ERANGE its own callers retry on. */
    if (length + 1 > size) { errno = ENOMEM; return -1; }
    memcpy(buffer, answer, length + 1);
    return 0;
}

char *getcwd(char *buffer, size_t size)
{
    char *target = buffer;
    size_t capacity = size;
    int error;

    /* POSIX: a caller-supplied buffer with no room is a programming error, not
       a short answer. */
    if (buffer != NULL && size == 0) {
        errno = EINVAL;
        return NULL;
    }
    if (buffer == NULL) {
        /* The allocating form. Callers that use it free the result. */
        capacity = size != 0 ? size : (size_t)PATH_MAX;
        target = malloc(capacity);
        if (target == NULL) {
            errno = ENOMEM;
            return NULL;
        }
    }

    errno = 0;
    /* Convert the helper's short-buffer error to the POSIX getcwd result. */
    if (native_directory(target, capacity) < 0) {
        error = errno;
        /* mpv retries a short buffer only when getcwd reports ERANGE. */
        if (error == ENOMEM) error = ERANGE;
        if (error == 0) error = EIO;
        if (buffer == NULL) free(target);
        errno = error;
        return NULL;
    }

    /* An answer that is not a terminated absolute path is not one this function
       may pass on: every caller joins it onto a relative path. */
    if (memchr(target, '\0', capacity) == NULL || target[0] != '/') {
        if (buffer == NULL) free(target);
        errno = EIO;
        return NULL;
    }
    return target;
}
