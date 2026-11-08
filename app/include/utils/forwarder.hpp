#pragma once

// 将 romfs 中预生成的 forwarder NSP 拆分出 NCA 并注册到 NCM，实现桌面图标安装。
// 非 Switch 平台返回错误信息。

#include <cstdint>
#include <string>

namespace forwarder {

// 安装内置 forwarder 到指定 NCM 存储（NcmStorageId：0=NAND, 2=SD）。
// 成功返回空字符串，否则返回错误信息；成功时 title_id 为已安装标题的 ID。
std::string install(int storage_id, uint64_t& title_id);

}  // namespace forwarder
