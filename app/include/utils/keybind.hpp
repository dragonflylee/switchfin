//
// Created by fang on 2025/7/27.
//

#pragma once

#include <borealis/core/actions.hpp>
#include <borealis/core/application.hpp>

#define DECL_KEYBIND(func)                                            \
public:                                                               \
    static void set##func(const std::string& key) {                   \
        key##func = parseKey(key);                                    \
        brls::Application::addToWatchedKeys(key##func);               \
    }                                                                 \
    static brls::BrlsKeyCombination get##func() { return key##func; } \
                                                                      \
private:                                                              \
    inline static brls::BrlsKeyCombination key##func = brls::BRLS_KBD_KEY_UNKNOWN;

class KeyBind {
public:
    /**
     * @brief 解析快捷键字符串
     * @param config 快捷键字符串
     *        支持的修饰键 (可多个同时出现): ctrl,alt,shift,meta
     *        支持的按键 (只能出现一个，必须放置在最后): 0-9,a-z,f1-f12,pgup,pgdn,home,end,up,down,left,right,space,[,]
     *        例: ctrl-shift-v, ctrl-alt-f1, f2, a, meta-1 ...
     * @return 返回 BrlsKeyCombination对象；若解析失败返回的对象键值为 BRLS_KBD_KEY_UNKNOWN
     */
    static brls::BrlsKeyCombination parseKey(const std::string& config);
    // 刷新与切换快捷键
    DECL_KEYBIND(Refresh);
    // 向左切换Tab
    DECL_KEYBIND(Last);
    // 向右切换Tab
    DECL_KEYBIND(Next);
    // 音量增大快捷键
    DECL_KEYBIND(VolumeUp);
    // 音量减小快捷键
    DECL_KEYBIND(VolumeDown);
    // 视频详情快捷键
    DECL_KEYBIND(VideoProfile);
    // 弹幕快捷键
    DECL_KEYBIND(Danmaku);
    // 快进快捷键
    DECL_KEYBIND(Forward);
    // 快退快捷键
    DECL_KEYBIND(Rewind);
    // 设置快捷键
    DECL_KEYBIND(Setting);
    // 视频清晰度菜单快捷键
    DECL_KEYBIND(VideoQuality);
    // 视频倍速菜单快捷键
    DECL_KEYBIND(VideoSpeed);
    // 视频OSD快捷键
    DECL_KEYBIND(VideoOsd);
    // 视频播放暂停
    DECL_KEYBIND(VideoPause);
};