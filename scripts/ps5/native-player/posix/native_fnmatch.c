/*
 * Native adapter for unmodified musl 1.2.5 src/regex/fnmatch.c.
 * Upstream commit: 0784374d561435f7c787a555aeab8ede699ed298
 * Source SHA256: b6b0d4f973210b3c825100a3dd684a3147a588e7e61835919096b432334761fe
 * Copyright (c) 2005-2020 Rich Felker, et al.; MIT, see LICENSE.musl.
 * Source URLs and byte hashes are retained in musl-source.json.
 */
#ifndef PS5_NATIVE_GPU
#error "Compile native POSIX support only for PS5_NATIVE_GPU"
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <string.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

/* FreeBSD SDK macros depend on its private rune ABI. Call the actual public
 * libc functions instead. MB_CUR_MAX retains the existing V7 locale adapter. */
#undef iswctype
#undef towupper
#undef towlower
#include "musl/fnmatch.c"
