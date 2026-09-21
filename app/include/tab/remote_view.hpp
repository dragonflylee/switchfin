/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <client/client.hpp>
#include <utils/ums.hpp>
#ifdef PS5_NATIVE_GPU
#include <atomic>
#include <mutex>
#include <unordered_map>
#endif

class RecyclingGrid;

using DirList = std::vector<remote::DirEntry>;

class RemoteView : public AttachedView {
public:
    using Client = std::shared_ptr<remote::Client>;

    RemoteView(Client c);
    ~RemoteView() override;

    brls::View* getDefaultFocus() override;

    void push(const std::string& path);

    void dismiss(std::function<void(void)> cb = [] {}) override;

    static void play(const std::string& path, const std::string& name = "", const std::string& itemId = "");

protected:
    void setContent(RecyclingGrid* view);
    RecyclingGrid* newRecycler();

    std::vector<RecyclingGrid*> stack;
#ifdef PS5_NATIVE_GPU
    RecyclingGrid* recycler = nullptr;
#else
    RecyclingGrid* recycler;
#endif
    Client client;
#ifdef PS5_NATIVE_GPU
    std::unordered_map<RecyclingGrid*, std::shared_ptr<std::atomic_bool>> listingCancellation;
    // Each RemoteView owns a client session whose transport may be mutable.
    // Workers retain this lock and the client independently of the view.
    std::shared_ptr<std::mutex> listingMutex = std::make_shared<std::mutex>();
#endif
};

class UmsView : public RemoteView {
public:
    UmsView();
    ~UmsView() override;

private:
    Ums::DeviceEvent::Subscription deviceSubscribeID;
#ifdef PS5_NATIVE_GPU
};
#else
};
#endif
