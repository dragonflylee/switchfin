package("mpv")
    if is_arch("x64") then
        set_urls("https://github.com/dragonflylee/switchfin/releases/download/mingw-packages/mpv-dev-x86_64.7z")
        add_versions("0.36.0", "5fe9ae3cda785f2154f7bcb10d9ed4f90a2d32bac09069cad77c21b892fb0685")
    elseif is_arch("arm64") then
        set_urls("https://github.com/dragonflylee/switchfin/releases/download/mingw-packages/mpv-dev-aarch64.7z")
        add_versions("0.36.0", "d508aecfaae5e58b7b5a10f535d6163244215a95afafd62a27125005fdb3a461")
    end
    add_links("mpv")
    on_install("windows", "mingw", function (package)
        os.cp("include/*", package:installdir("include"))
        os.cp("*.a", package:installdir("lib"))
        os.cp("*.dll", package:installdir("bin"))

        import("detect.sdks.find_vstudio")
        for _, vsinfo in pairs(find_vstudio()) do
            if vsinfo.vcvarsall then
                os.setenv("PATH", vsinfo.vcvarsall[os.arch()]["PATH"])
            end
        end

        os.execv("lib.exe", {
            "/name:libmpv-2.dll", 
            "/def:mpv.def", 
            "/out:"..package:installdir("lib").."/mpv.lib",
            "/MACHINE:"..package:arch(),
        })
    end)