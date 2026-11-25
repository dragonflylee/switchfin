//
// Created by fang on 2023/1/6.
//

#include <borealis/views/dialog.hpp>
#include "utils/dialog.hpp"

#ifdef __PSV__
#include <psp2/appmgr.h>
#include <psp2/kernel/processmgr.h>
#endif

using namespace brls::literals;

void Dialog::show(const std::string& msg, Callback cb) {
    auto dialog = new brls::Dialog(msg);
    dialog->addButton("hints/ok"_i18n, cb);
    dialog->open();
}

void Dialog::cancelable(const std::string& msg, Callback cb) {
    auto dialog = new brls::Dialog(msg);
    dialog->addButton("hints/cancel"_i18n, []() {});
    dialog->addButton("hints/ok"_i18n, cb);
    dialog->open();
}

/// 退出应用提示
void Dialog::quitApp(bool restart) {
    auto dialog = new brls::Dialog("main/setting/quit_hint"_i18n);
    dialog->addButton("hints/ok"_i18n, [restart]() {
#ifdef __PSV__
        // Settings are saved as soon as they change and the system frees
        // everything the app holds when it exits, so skip the slow teardown
        // (it waits on mpv and background tasks and can hang).
        if (restart) sceAppMgrLoadExec("app0:eboot.bin", nullptr, nullptr);
        sceKernelExitProcess(0);
#endif
        brls::Box* container = new brls::Box();
        container->setJustifyContent(brls::JustifyContent::CENTER);
        container->setAlignItems(brls::AlignItems::CENTER);
        brls::Label* hint = new brls::Label();
        hint->setFocusable(true);
        hint->setHideHighlight(true);
        hint->setFontSize(32);
        hint->setText("hints/quit"_i18n);
        container->addView(hint);
        container->setBackgroundColor(brls::Application::getTheme().getColor("brls/background"));
        brls::Application::pushActivity(new brls::Activity(container), brls::TransitionAnimation::NONE);
        brls::Application::getPlatform()->exitToHomeMode(!restart);
        brls::Application::quit();
    });
    dialog->setCancelable(false);
    dialog->open();
}