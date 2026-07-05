#!/usr/bin/env python3
"""Regenerate app/src/utils/vita_head_bin.h from VitaShell's head.bin template.

The PS Vita in-app self-update (issue #14) forges a self-signed fake-package
`head.bin` so ScePromoterUtility accepts a homebrew VPK. The header template is
a fixed functional artifact the HENkaku-patched promoter requires; it is
identical across every Vita homebrew installer. We embed VitaShell's copy.

Usage:
    python3 scripts/gen-vita-head-bin.py [path/to/head.bin]

With no argument the canonical template is downloaded from VitaShell master. The
download/input is checked against the known sha256 before being embedded.
"""

import hashlib
import os
import sys
import urllib.request

EXPECTED_SHA256 = "cbb88299319048e19115a1fc9c76b04e33745291749de0e08963b9b425623f45"
EXPECTED_SIZE = 1072  # 0x430: header + info + three trailing 16-byte HMAC slots
SOURCE_URL = "https://raw.githubusercontent.com/TheOfficialFloW/VitaShell/master/resources/head.bin"

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "app", "src", "utils", "vita_head_bin.h")


def load(src):
    if src:
        with open(src, "rb") as f:
            return f.read()
    with urllib.request.urlopen(SOURCE_URL) as r:
        return r.read()


def main():
    data = load(sys.argv[1] if len(sys.argv) > 1 else None)

    digest = hashlib.sha256(data).hexdigest()
    if len(data) != EXPECTED_SIZE or digest != EXPECTED_SHA256:
        sys.exit(
            f"refusing to embed unexpected head.bin: size={len(data)} "
            f"(want {EXPECTED_SIZE}), sha256={digest} (want {EXPECTED_SHA256})"
        )

    lines = [
        "// Auto-generated: fake self-signed PKG header template used to forge",
        "// sce_sys/package/head.bin so ScePromoterUtility accepts a homebrew VPK.",
        "// Source: VitaShell resources/head.bin (TheOfficialFloW/VitaShell, GPLv3),",
        f"// sha256 {EXPECTED_SHA256}.",
        "// Regenerate: scripts/gen-vita-head-bin.py (see issue #14). DO NOT EDIT BY HAND.",
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "static const unsigned char VITA_HEAD_BIN[] = {",
    ]
    for i in range(0, len(data), 12):
        chunk = data[i : i + 12]
        lines.append("    " + " ".join("0x%02x," % b for b in chunk))
    lines += [
        "};",
        "",
        "static const std::size_t VITA_HEAD_BIN_SIZE = sizeof(VITA_HEAD_BIN);",
        "",
    ]

    with open(OUT, "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {OUT} ({len(data)} bytes, sha256 {digest})")


if __name__ == "__main__":
    main()
