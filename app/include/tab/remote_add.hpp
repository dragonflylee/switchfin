/*
    pleNx — add/edit form for a remote file server
    (WebDAV / FTP / SFTP / HTTP index). Side panel pushed as an activity
    (same pattern as PlayerSetting). Saving is gated on a successful
    connection test: listing of the start path through the matching
    client (app/src/client/client.cpp routes the URL scheme).
*/

#pragma once

#include <borealis.hpp>
#include <utils/config.hpp>

class RemoteAdd : public brls::Box {
public:
    /// editIndex < 0: add; otherwise edits AppConfig::getRemotes()[editIndex]
    RemoteAdd(std::function<void()> onDone, int editIndex = -1);
    ~RemoteAdd() override;

    /// Pushes the form as an activity. `onDone` is called after a
    /// successful save (the caller refreshes its server list).
    static void open(std::function<void()> onDone, int editIndex = -1);

private:
    BRLS_BIND(brls::Header, header, "remote/add/header");
    BRLS_BIND(brls::SelectorCell, cellType, "remote/add/type");
    BRLS_BIND(brls::InputCell, cellName, "remote/add/name");
    BRLS_BIND(brls::InputCell, cellHost, "remote/add/host");
    BRLS_BIND(brls::InputNumericCell, cellPort, "remote/add/port");
    BRLS_BIND(brls::InputCell, cellUser, "remote/add/user");
    BRLS_BIND(brls::InputCell, cellPasswd, "remote/add/passwd");
    BRLS_BIND(brls::InputCell, cellPath, "remote/add/path");
    BRLS_BIND(brls::Button, btnSave, "remote/add/save");
    BRLS_BIND(brls::Button, btnDelete, "remote/delete");
    BRLS_BIND(brls::ProgressSpinner, spinner, "remote/add/spinner");
    BRLS_BIND(brls::Box, cancel, "remote/add/cancel");

    /// Builds an AppRemote from the fields; throws std::runtime_error
    /// (i18n message) if a required field is missing.
    AppRemote build();
    /// Validates, tests the connection (async + spinner) then saves.
    void submit();

    std::function<void()> onDone;
    int editIndex;
    /// current index of the type selector (to only replace the port if it
    /// still holds the previous type's default)
    int typeIndex = 0;
};
