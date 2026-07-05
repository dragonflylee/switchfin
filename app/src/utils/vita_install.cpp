/*
    pleNx — PS Vita in-app self-update (issue #14)

    Installs a homebrew VPK from within the running app: extract the ZIP, forge
    a fake-package `sce_sys/package/head.bin`, then promote the directory with
    ScePromoterUtility (the HENkaku-patched promoter accepts self-signed
    packages on every hacked Vita).

    The fake-package technique — the `head.bin` template and the `fpkg_hmac`
    key derivation — is the de-facto interoperability standard shared by every
    Vita homebrew installer. It originates from the HENkaku / molecularShell
    lineage; VitaShell (TheOfficialFloW, GPLv3) is the canonical reference for
    `makeHeadBin`/`promoteApp`. The `head.bin` bytes embedded in
    `vita_head_bin.h` come from VitaShell's `resources/head.bin`; the code below
    is an independent reimplementation. The Vita build already links a GPL
    libmpv, so the distributed `pleNx.vpk` is a GPL combined work and this reuse
    is licence-compatible. See NOTICE and issue #14.
*/

#ifdef __PSV__

#include "utils/vita_install.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>

#include <mbedtls/sha1.h>

#include <borealis/core/logger.hpp>
#include <fmt/format.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// miniz is compiled with MINIZ_NO_STDIO / MINIZ_NO_TIME on Vita: all file I/O
// goes through sceIo via custom read/write callbacks, so we never depend on the
// newlib stdio quirks (fopen64/stat64/utime.h).
#include "miniz.h"

// Fake-package header template (VITA_HEAD_BIN / VITA_HEAD_BIN_SIZE). Included at
// file scope: it pulls <cstddef>/<cstdint> and must not land inside a namespace.
#include "vita_head_bin.h"

namespace {

// ---------------------------------------------------------------------------
// Small sceIo filesystem helpers
// ---------------------------------------------------------------------------

bool fileExists(const std::string& path) {
    SceIoStat st;
    return sceIoGetstat(path.c_str(), &st) >= 0;
}

// mkdir -p, tolerating already-existing components and the device root
// ("ux0:"), which cannot be created.
void mkdirs(const std::string& path) {
    size_t i = 0;
    while (i < path.size()) {
        size_t j = path.find('/', i);
        if (j == std::string::npos) j = path.size();
        std::string cur = path.substr(0, j);
        if (!cur.empty() && cur.back() != ':') sceIoMkdir(cur.c_str(), 0777);
        i = j + 1;
    }
}

std::string parentDir(const std::string& path) {
    size_t p = path.find_last_of('/');
    return p == std::string::npos ? std::string() : path.substr(0, p);
}

// Recursively delete a file or directory tree (best effort).
void removePath(const std::string& path) {
    SceUID d = sceIoDopen(path.c_str());
    if (d >= 0) {
        SceIoDirent ent;
        std::memset(&ent, 0, sizeof(ent));
        while (sceIoDread(d, &ent) > 0) {
            std::string name = ent.d_name;
            if (name != "." && name != "..") {
                std::string child = path + "/" + name;
                if (SCE_S_ISDIR(ent.d_stat.st_mode))
                    removePath(child);
                else
                    sceIoRemove(child.c_str());
            }
            std::memset(&ent, 0, sizeof(ent));
        }
        sceIoDclose(d);
        sceIoRmdir(path.c_str());
    } else {
        sceIoRemove(path.c_str());
    }
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    out.resize(size > 0 ? static_cast<size_t>(size) : 0);
    int r = size > 0 ? sceIoRead(fd, out.data(), static_cast<SceSize>(size)) : 0;
    sceIoClose(fd);
    return r == static_cast<int>(size);
}

bool writeFile(const std::string& path, const void* data, size_t size) {
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return false;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t left = size;
    bool ok = true;
    while (left) {
        int w = sceIoWrite(fd, p, static_cast<SceSize>(left));
        if (w <= 0) { ok = false; break; }
        p += w;
        left -= static_cast<size_t>(w);
    }
    sceIoClose(fd);
    return ok;
}

// ---------------------------------------------------------------------------
// param.sfo string lookup (little-endian format)
// ---------------------------------------------------------------------------

uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

bool sfoGetString(const std::vector<uint8_t>& sfo, const char* key, char* out, size_t outsize) {
    if (sfo.size() < 0x14 || le32(&sfo[0]) != 0x46535000 /* "\0PSF" */) return false;
    uint32_t keyTable = le32(&sfo[0x08]);
    uint32_t dataTable = le32(&sfo[0x0C]);
    uint32_t count = le32(&sfo[0x10]);
    for (uint32_t i = 0; i < count; i++) {
        size_t e = 0x14 + static_cast<size_t>(i) * 0x10;
        if (e + 0x10 > sfo.size()) return false;
        uint16_t koff = le16(&sfo[e]);
        uint32_t plen = le32(&sfo[e + 4]);
        uint32_t doff = le32(&sfo[e + 12]);
        size_t kpos = keyTable + koff;
        if (kpos >= sfo.size()) continue;
        if (std::strcmp(reinterpret_cast<const char*>(&sfo[kpos]), key) != 0) continue;
        size_t dpos = dataTable + doff;
        if (dpos >= sfo.size() || outsize == 0) return false;
        size_t n = plen;
        if (n >= outsize) n = outsize - 1;
        if (dpos + n > sfo.size()) n = sfo.size() - dpos;
        std::memcpy(out, &sfo[dpos], n);
        out[n] = '\0';
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Fake-package head.bin forging
// ---------------------------------------------------------------------------

// Big-endian u32 read at a byte offset inside the head.bin buffer.
uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// The fake-package HMAC: an obfuscated fold of a plain SHA-1 digest. This is a
// fixed functional derivation dictated by the promoter, not a keyed HMAC.
void fpkgHmac(const uint8_t* data, uint32_t len, uint8_t hmac[16]) {
    uint8_t sha1[20];
    uint8_t buf[64];
    mbedtls_sha1(data, len, sha1);
    std::memset(buf, 0, sizeof(buf));
    std::memcpy(&buf[0], &sha1[4], 8);
    std::memcpy(&buf[8], &sha1[4], 8);
    std::memcpy(&buf[16], &sha1[12], 4);
    buf[20] = sha1[16];
    buf[21] = sha1[1];
    buf[22] = sha1[2];
    buf[23] = sha1[3];
    std::memcpy(&buf[24], &buf[16], 8);
    mbedtls_sha1(buf, sizeof(buf), sha1);
    std::memcpy(hmac, sha1, 16);
}

int makeHeadBin(const std::string& pkgDir, std::string& err) {
    std::vector<uint8_t> sfo;
    if (!readFile(pkgDir + "/sce_sys/param.sfo", sfo)) {
        err = "param.sfo introuvable";
        return -1;
    }

    char titleid[12] = {0};
    char contentid[48] = {0};
    if (!sfoGetString(sfo, "TITLE_ID", titleid, sizeof(titleid))) {
        err = "TITLE_ID absent de param.sfo";
        return -1;
    }
    sfoGetString(sfo, "CONTENT_ID", contentid, sizeof(contentid));  // optional

    std::vector<uint8_t> head(VITA_HEAD_BIN, VITA_HEAD_BIN + VITA_HEAD_BIN_SIZE);

    // content id at 0x30 (48 bytes, zero-padded); fall back to a synthetic one
    // when param.sfo carries none, exactly like every homebrew installer.
    char fallback[48];
    std::snprintf(fallback, sizeof(fallback), "EP9000-%s_00-0000000000000000", titleid);
    std::memset(&head[0x30], 0, 48);
    const char* cid = contentid[0] ? contentid : fallback;
    std::strncpy(reinterpret_cast<char*>(&head[0x30]), cid, 48);

    uint8_t hmac[16];
    uint32_t off, len, out;

    // hmac of the pkg header
    len = be32(&head[0xD0]);
    fpkgHmac(&head[0], len, hmac);
    std::memcpy(&head[len], hmac, 16);

    // hmac of the pkg info
    off = be32(&head[0x8]);
    len = be32(&head[0x10]);
    out = be32(&head[0xD4]);
    fpkgHmac(&head[off], len - 64, hmac);
    std::memcpy(&head[out], hmac, 16);

    // hmac of everything
    len = be32(&head[0xE8]);
    fpkgHmac(&head[0], len, hmac);
    std::memcpy(&head[len], hmac, 16);

    mkdirs(pkgDir + "/sce_sys/package");
    if (!writeFile(pkgDir + "/sce_sys/package/head.bin", head.data(), head.size())) {
        err = "écriture de head.bin impossible";
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// VPK (ZIP) extraction via miniz + sceIo callbacks
// ---------------------------------------------------------------------------

struct ZipReadIO {
    SceUID fd;
};

size_t zipReadCb(void* opaque, mz_uint64 ofs, void* buf, size_t n) {
    SceUID fd = static_cast<ZipReadIO*>(opaque)->fd;
    if (sceIoLseek(fd, static_cast<SceOff>(ofs), SCE_SEEK_SET) < 0) return 0;
    int r = sceIoRead(fd, buf, static_cast<SceSize>(n));
    return r < 0 ? 0 : static_cast<size_t>(r);
}

struct ZipWriteIO {
    SceUID fd;
    bool ok;
};

size_t zipWriteCb(void* opaque, mz_uint64 /*ofs*/, const void* buf, size_t n) {
    ZipWriteIO* w = static_cast<ZipWriteIO*>(opaque);
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    size_t left = n;
    while (left) {
        int r = sceIoWrite(w->fd, p, static_cast<SceSize>(left));
        if (r <= 0) {
            w->ok = false;
            return n - left;
        }
        p += r;
        left -= static_cast<size_t>(r);
    }
    return n;
}

int extractVpk(const std::string& vpk, const std::string& dest, std::string& err) {
    SceUID fd = sceIoOpen(vpk.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        err = "ouverture du VPK impossible";
        return -1;
    }
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    ZipReadIO io{fd};
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    zip.m_pRead = zipReadCb;
    zip.m_pIO_opaque = &io;

    if (!mz_zip_reader_init(&zip, static_cast<mz_uint64>(size), 0)) {
        sceIoClose(fd);
        err = "VPK illisible (archive ZIP invalide)";
        return -1;
    }

    int result = 0;
    mz_uint total = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < total; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) {
            err = "lecture d'une entrée du VPK impossible";
            result = -1;
            break;
        }
        std::string rel = st.m_filename;
        // reject path traversal / absolute entries
        if (rel.empty() || rel[0] == '/' || rel[0] == '\\' || rel.find("..") != std::string::npos)
            continue;
        std::string outpath = dest + "/" + rel;

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            mkdirs(outpath);
            continue;
        }

        mkdirs(parentDir(outpath));
        SceUID out = sceIoOpen(outpath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (out < 0) {
            err = fmt::format("écriture impossible : {}", rel);
            result = -1;
            break;
        }
        ZipWriteIO w{out, true};
        mz_bool ok = mz_zip_reader_extract_to_callback(&zip, i, zipWriteCb, &w, 0);
        sceIoClose(out);
        if (!ok || !w.ok) {
            err = fmt::format("extraction impossible : {}", rel);
            result = -1;
            break;
        }
    }

    mz_zip_reader_end(&zip);
    sceIoClose(fd);
    return result;
}

// ---------------------------------------------------------------------------
// ScePromoterUtility promotion
// ---------------------------------------------------------------------------

int loadScePaf() {
    static uint32_t argp[] = {0x180000, 0xFFFFFFFFu, 0xFFFFFFFFu, 1, 0xFFFFFFFFu, 0xFFFFFFFFu};
    int result = -1;
    SceSysmoduleOpt opt;
    std::memset(&opt, 0, sizeof(opt));
    opt.flags = sizeof(opt);
    opt.result = &result;
    opt.unused[0] = -1;
    opt.unused[1] = -1;
    return sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(argp), argp, &opt);
}

int unloadScePaf() {
    SceSysmoduleOpt opt;
    std::memset(&opt, 0, sizeof(opt));
    return sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, 0, nullptr, &opt);
}

int promoteApp(const std::string& path, std::string& err) {
    int res = loadScePaf();
    if (res < 0) {
        err = fmt::format("chargement de ScePaf impossible (0x{:08X})", static_cast<unsigned>(res));
        return res;
    }

    res = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    if (res < 0) {
        err = fmt::format("chargement du promoter impossible (0x{:08X})", static_cast<unsigned>(res));
        unloadScePaf();
        return res;
    }

    res = scePromoterUtilityInit();
    if (res >= 0) {
        int pres = scePromoterUtilityPromotePkgWithRif(path.c_str(), 1);
        if (pres < 0) {
            err = fmt::format("promotion du paquet impossible (0x{:08X})", static_cast<unsigned>(pres));
            res = pres;
        }
        scePromoterUtilityExit();
    } else {
        err = fmt::format("initialisation du promoter impossible (0x{:08X})", static_cast<unsigned>(res));
    }

    sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    unloadScePaf();
    return res;
}

}  // namespace

namespace vita {

int installVpk(const std::string& vpkPath, const std::string& workDir, std::string& err) {
    brls::Logger::info("vita: installing {} via {}", vpkPath, workDir);

    removePath(workDir);
    mkdirs(workDir);

    if (extractVpk(vpkPath, workDir, err) != 0) {
        brls::Logger::error("vita: extract failed: {}", err);
        removePath(workDir);
        return -1;
    }

    // A valid VPK must at least carry the executable and its metadata.
    if (!fileExists(workDir + "/eboot.bin") || !fileExists(workDir + "/sce_sys/param.sfo")) {
        err = "VPK invalide (eboot.bin ou param.sfo manquant)";
        brls::Logger::error("vita: {}", err);
        removePath(workDir);
        return -1;
    }

    if (makeHeadBin(workDir, err) != 0) {
        brls::Logger::error("vita: makeHeadBin failed: {}", err);
        removePath(workDir);
        return -1;
    }

    if (promoteApp(workDir, err) < 0) {
        brls::Logger::error("vita: promote failed: {}", err);
        removePath(workDir);
        return -1;
    }

    // Promotion is synchronous (sync=1): the bubble is updated, the scratch
    // directory can go.
    removePath(workDir);
    brls::Logger::info("vita: install succeeded");
    return 0;
}

}  // namespace vita

#endif  // __PSV__
