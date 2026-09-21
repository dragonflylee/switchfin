// PS5 OpenGL integration: unsupported optional host diagnostics.
// Derived from PS5 OpenGL native-app/runtime_shims.c by BlackBearReloaded.
// SPDX-License-Identifier: GPL-3.0-or-later
// Alteration: retain only unsupported host-tool APIs, keeping the native runtime's
// startup, assertion and TLS implementations. No application service uses them.
#include <errno.h>
#include <stdio.h>

int mkstemps(char *template_name, int suffix_length) {
    (void)template_name;
    (void)suffix_length;
    errno = ENOSYS;
    return -1;
}
void openlog(const char *identifier, int option, int facility) {
    (void)identifier;
    (void)option;
    (void)facility;
    // This build captures Mesa/ACO stderr in its application log instead.
}
FILE *popen(const char *command, const char *mode) {
    (void)command;
    (void)mode;
    errno = ENOSYS;
    return NULL;
}
int pclose(FILE *stream) {
    (void)stream;
    errno = ENOSYS;
    return -1;
}
