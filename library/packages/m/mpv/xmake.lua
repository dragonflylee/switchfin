package("mpv")
    if is_plat("windows", "mingw") then
        set_urls("https://github.com/dragonflylee/switchfin/releases/download/mingw-packages/mpv-dev-x86_64.7z")
        add_versions("latest", "d6b1d1870815f7a453259905f2198372e15e74a2bf48778bf15d16da9c70a4cb")
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
            "/MACHINE:X64",
        })
    end)