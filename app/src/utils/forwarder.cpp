//
// 安装内置 forwarder：解析 romfs 中的 NSP(PFS0)，将 NCA 写入 NCM 并按 cnmt 注册元数据。
// NCA 类型/顺序/标题 ID 均取自 meta NCA 内的 cnmt（挂载为 ContentMeta 文件系统读取，无需自行解密）。
//
#include "utils/forwarder.hpp"

#ifdef __SWITCH__

#include <switch.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace forwarder {
namespace {

using Bytes = std::vector<u8>;

constexpr const char* NSP_PATH = "romfs:/forwarder.nsp";

#pragma pack(push, 1)
struct Pfs0Header {
    u32 magic, num_files, string_table_size, padding;
};
struct Pfs0Entry {
    u64 data_offset, data_size;
    u32 name_offset, padding;
};
struct PackagedContentMetaHeader {
    u64 title_id;
    u32 version;
    u8 type;
    u8 _0xD;
    NcmContentMetaHeader meta_header;
    u8 install_type;
    u8 _0x17;
    u32 required_sys_version;
    u8 _0x1C[0x4];
};
#pragma pack(pop)
static_assert(sizeof(PackagedContentMetaHeader) == 0x20);

// 文件名（32 位十六进制）→ NcmContentId。
NcmContentId nca_id_from_string(const std::string& str) {
    char lo[17] = {}, hi[17] = {};
    std::memcpy(lo, str.c_str(), 16);
    std::memcpy(hi, str.c_str() + 16, 16);
    NcmContentId id{};
    *(u64*)id.c = __builtin_bswap64(std::strtoul(lo, nullptr, 16));
    *(u64*)(id.c + 8) = __builtin_bswap64(std::strtoul(hi, nullptr, 16));
    return id;
}

struct NspEntry {
    NcmContentId id;
    u64 file_off;
    u64 file_size;
    bool is_meta;
};

bool parse_nsp(std::ifstream& nsp, u64 nsp_size, std::vector<NspEntry>& out) {
    Pfs0Header hdr;
    nsp.seekg(0);
    if (!nsp.read(reinterpret_cast<char*>(&hdr), sizeof(hdr)) || hdr.magic != 0x30534650) return false;

    const u64 table_off = sizeof(hdr);
    const u64 str_tab = table_off + sizeof(Pfs0Entry) * hdr.num_files;
    const u64 data_base = str_tab + hdr.string_table_size;
    if (data_base > nsp_size) return false;

    std::vector<char> strings(hdr.string_table_size);
    nsp.seekg(str_tab);
    nsp.read(strings.data(), strings.size());

    std::vector<Pfs0Entry> table(hdr.num_files);
    nsp.seekg(table_off);
    nsp.read(reinterpret_cast<char*>(table.data()), sizeof(Pfs0Entry) * hdr.num_files);

    for (const auto& e : table) {
        if (e.name_offset >= strings.size()) return false;
        const std::string name = strings.data() + e.name_offset;
        if (name.find('.') != 32) return false;
        NspEntry entry{nca_id_from_string(name), data_base + e.data_offset, e.data_size,
                       name.find(".cnmt.nca") != std::string::npos};
        if (entry.file_off + entry.file_size > nsp_size) return false;
        out.push_back(entry);
    }
    return !out.empty();
}

// 将 NCA 流式写入 placeholder 并注册。
Result write_nca(NcmContentStorage& cs, const NspEntry& e, std::ifstream& nsp) {
    const NcmPlaceHolderId ph = *reinterpret_cast<const NcmPlaceHolderId*>(&e.id);
    ncmContentStorageDeletePlaceHolder(&cs, &ph);
    Result rc = ncmContentStorageCreatePlaceHolder(&cs, &e.id, &ph, e.file_size);

    std::vector<char> buf(0x400000);
    for (u64 off = 0; R_SUCCEEDED(rc) && off < e.file_size;) {
        const u32 chunk = static_cast<u32>(std::min<u64>(buf.size(), e.file_size - off));
        nsp.seekg(e.file_off + off);
        nsp.read(buf.data(), chunk);
        if (static_cast<u32>(nsp.gcount()) != chunk) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        rc = ncmContentStorageWritePlaceHolder(&cs, &ph, off, buf.data(), chunk);
        off += chunk;
    }
    if (R_FAILED(rc)) return rc;

    bool has = false;
    if (R_SUCCEEDED(ncmContentStorageHas(&cs, &has, &e.id)) && has) ncmContentStorageDelete(&cs, &e.id);
    return ncmContentStorageRegister(&cs, &e.id, &ph);
}

// 从已注册的 meta NCA 中读取 cnmt。
bool read_cnmt(NcmContentStorage& cs, const NcmContentId& meta_id, Bytes& cnmt) {
    char path[FS_MAX_PATH];
    if (R_FAILED(ncmContentStorageGetPath(&cs, path, sizeof(path), &meta_id))) return false;

    FsFileSystem fs;
    if (R_FAILED(fsOpenFileSystemWithId(&fs, 0, FsFileSystemType_ContentMeta, path, FsContentAttributes_All)))
        return false;

    // 目录内只有一个 *.cnmt 文件。
    FsDir dir;
    FsDirectoryEntry entry;
    s64 total = 0, size = 0;
    bool ok = R_SUCCEEDED(fsFsOpenDirectory(&fs, "/", FsDirOpenMode_ReadFiles, &dir));
    ok = ok && R_SUCCEEDED(fsDirRead(&dir, &total, 1, &entry));
    fsDirClose(&dir);

    FsFile file;
    if (!ok || R_FAILED(fsFsOpenFile(&fs, ("/" + std::string(entry.name)).c_str(), FsOpenMode_Read, &file))) {
        fsFsClose(&fs);
        return false;
    }
    if (R_SUCCEEDED(fsFileGetSize(&file, &size)) && size > 0) {
        u64 read = 0;
        cnmt.resize(size);
        ok = R_SUCCEEDED(fsFileRead(&file, 0, cnmt.data(), size, FsReadOption_None, &read)) &&
             static_cast<s64>(read) == size;
    } else {
        ok = false;
    }
    fsFileClose(&file);
    fsFsClose(&fs);
    return ok;
}

// 根据 cnmt 构造 NCM content meta 数据。
Bytes build_meta_data(const Bytes& cnmt, const NcmContentId& meta_id, u64 meta_size) {
    PackagedContentMetaHeader hdr;
    std::memcpy(&hdr, cnmt.data(), sizeof(hdr));
    const auto* infos = reinterpret_cast<const NcmPackagedContentInfo*>(
        cnmt.data() + sizeof(hdr) + hdr.meta_header.extended_header_size);

    NcmContentMetaHeader mh = hdr.meta_header;
    mh.storage_id = 0;
    mh.attributes = 0;
    mh.content_count = 1 + hdr.meta_header.content_count;  // 含 meta 自身

    Bytes out(reinterpret_cast<const u8*>(&mh), reinterpret_cast<const u8*>(&mh) + sizeof(mh));
    const u8* ext = cnmt.data() + sizeof(hdr);
    out.insert(out.end(), ext, ext + mh.extended_header_size);

    NcmContentInfo meta_info{};
    meta_info.content_id = meta_id;
    meta_info.content_type = NcmContentType_Meta;
    ncmU64ToContentInfoSize(meta_size, &meta_info);
    const u8* mp = reinterpret_cast<const u8*>(&meta_info);
    out.insert(out.end(), mp, mp + sizeof(meta_info));

    // meta 自身之后追加各内容 info。
    for (u32 i = 0; i < hdr.meta_header.content_count; i++) {
        const u8* p = reinterpret_cast<const u8*>(&infos[i].info);
        out.insert(out.end(), p, p + sizeof(NcmContentInfo));
    }
    return out;
}

// 推送应用记录：cmd 16，随后 cmd 404 刷新控制缓存。
Result push_application_record(Service& am, u64 tid, const NcmContentMetaKey& key, u8 storage_id) {
    struct {
        u8 last_modified_event;
        u8 padding[0x7];
        u64 tid;
    } in = {0x3 /* Installed */, {0}, tid};
    struct {
        NcmContentMetaKey key;
        u8 storage_id;
        u8 padding[0x7];
    } record = {key, storage_id, {}};

    Result rc = serviceDispatchIn(&am, 16, in,
                                  .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
                                  .buffers = {{&record, sizeof(record)}});
    if (R_SUCCEEDED(rc)) serviceDispatchIn(&am, 404, tid);  // 刷新控制缓存
    return rc;
}

}  // namespace

std::string install(int storage_id, u64& out_title_id) {
    std::ifstream nsp(NSP_PATH, std::ios::binary | std::ios::ate);
    if (!nsp) return "Missing forwarder NSP";

    std::vector<NspEntry> entries;
    if (!parse_nsp(nsp, static_cast<u64>(nsp.tellg()), entries)) return "Failed to parse forwarder NSP";

    if (R_FAILED(ncmInitialize())) return "Failed to init ncm";
    if (R_FAILED(nsInitialize())) {
        ncmExit();
        return "Failed to init ns";
    }
    Service am;
    Result am_rc = MAKERESULT(Module_Libnx, LibnxError_NotFound);
    if (hosversionAtLeast(3, 0, 0)) {
        am_rc = nsGetApplicationManagerInterface(&am);
    } else {
        am = *nsGetServiceSession_ApplicationManagerInterface();
        am_rc = 0;
    }
    appletSetMediaPlaybackState(true);  // 安装期间防止 applet 被挂起

    std::string error;
    const auto dest = static_cast<NcmStorageId>(storage_id);
    NcmContentStorage cs;
    NcmContentId meta_id{};
    u64 meta_size = 0;

    if (R_FAILED(ncmOpenContentStorage(&cs, dest))) {
        error = "Failed to open content storage";
    } else {
        // 写入全部 NCA，并记录 meta NCA 的 id/大小。
        for (const auto& e : entries) {
            if (R_FAILED(write_nca(cs, e, nsp))) {
                error = "Failed to write NCA";
                break;
            }
            if (e.is_meta) meta_id = e.id, meta_size = e.file_size;
        }

        // 标题 ID / 类型 / infos 顺序均以 cnmt 为准。
        Bytes cnmt;
        PackagedContentMetaHeader hdr{};
        if (error.empty() && !read_cnmt(cs, meta_id, cnmt)) error = "Failed to read cnmt";
        if (error.empty()) std::memcpy(&hdr, cnmt.data(), sizeof(hdr));
        out_title_id = hdr.title_id;

        NcmContentMetaDatabase db;
        if (error.empty() && R_FAILED(ncmOpenContentMetaDatabase(&db, dest))) {
            error = "Failed to open content meta database";
        } else if (error.empty()) {
            const NcmContentMetaKey key{hdr.title_id, hdr.version, static_cast<NcmContentMetaType>(hdr.type),
                                        NcmContentInstallType_Full, {}};
            const auto meta_data = build_meta_data(cnmt, meta_id, meta_size);

            Result rc = ncmContentMetaDatabaseSet(&db, &key, meta_data.data(), meta_data.size());
            if (R_SUCCEEDED(rc)) rc = ncmContentMetaDatabaseCommit(&db);
            ncmContentMetaDatabaseClose(&db);

            if (R_FAILED(rc)) {
                error = "Failed to commit meta database";
            } else if (R_FAILED(am_rc)) {
                error = "Failed to open application manager";
            } else if (R_FAILED(push_application_record(am, hdr.title_id, key, static_cast<u8>(storage_id)))) {
                // 仅推送应用记录（与 nspmini 一致）。切勿调用 nsDeleteApplicationEntity：
                // 该操作会清除同 title 的存档注册，导致重装后系统提示“应用数据损坏”。
                error = "Failed to push application record";
            }
        }

        ncmContentStorageClose(&cs);
    }

    if (R_SUCCEEDED(am_rc) && hosversionAtLeast(3, 0, 0)) serviceClose(&am);
    appletSetMediaPlaybackState(false);
    nsExit();
    ncmExit();
    return error;
}

}  // namespace forwarder

#endif
