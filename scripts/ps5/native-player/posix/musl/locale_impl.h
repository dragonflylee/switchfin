/* Integration header for the pristine musl fnmatch source only.
 * fnmatch uses no musl-private locale types or functions. Its public mbtowc,
 * wctype and MB_CUR_MAX operations come from the selected platform libc.
 * Do not import musl's private locale/thread structures into the native ABI. */
