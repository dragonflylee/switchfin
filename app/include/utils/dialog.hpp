#pragma once

#include <string>
#include <functional>

class Dialog {
public:
    /// 展示普通对话框
    static void show(const std::string& msg);

    /// 展示带有取消按钮的对话框
    static void cancelable(const std::string& msg, std::function<void(void)> cb);

    /// 退出应用提示
    static void quitApp(bool restart = true);
};