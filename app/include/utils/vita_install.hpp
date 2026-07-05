/*
    pleNx — PS Vita in-app self-update (issue #14)
*/

#pragma once

#ifdef __PSV__
#include <string>

namespace vita {

/// Install a homebrew VPK already downloaded to disk, in place, by forging its
/// fake-package `head.bin` and handing the extracted directory to
/// ScePromoterUtility. This is the technique every Vita homebrew installer uses
/// (VitaShell, VitaDB Downloader, …); it relies on the HENkaku-patched promoter
/// present on every hacked Vita.
///
/// - `vpkPath` : the downloaded `.vpk` (a plain ZIP) to install.
/// - `workDir` : a writable scratch directory under `ux0:` used to extract the
///               package before promotion; wiped and recreated by this call.
/// - `err`     : filled with a human-readable reason on failure.
///
/// Returns 0 on success. The bubble is replaced in place, so the caller must
/// quit the app afterwards for the user to relaunch the new version.
int installVpk(const std::string& vpkPath, const std::string& workDir, std::string& err);

}  // namespace vita
#endif
