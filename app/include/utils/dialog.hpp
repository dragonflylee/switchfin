#pragma once

#include <string>
#include <functional>

class Dialog {
public:
    using Callback = brls::VoidEvent::Callback;

    /// 展示普通对话框
    static void show(const std::string& msg, Callback cb = []() {});

    /// 展示带有取消按钮的对话框
    static void cancelable(const std::string& msg, Callback cb);

    /// 退出应用提示 ; `msg` permet d'adapter le texte au contexte
    /// (ex. mise à jour installée), vide = message générique
    static void quitApp(bool restart = true, const std::string& msg = "");
};