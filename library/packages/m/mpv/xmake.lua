package("mpv")
    if is_plat("windows", "mingw") then
        set_urls("https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/$(version)/mpv-dev-x86_64-$(version)-git-bc96b23.7z")
        add_versions("20230727", "735fac5cdc000c7ad803238d2e4c002771e9a72ae338374cde6b58fc0bb667bc")
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
            "/def:"..package:scriptdir().."/mpv.def", 
            "/out:"..package:installdir("lib").."/mpv.lib", 
            "/MACHINE:X64",
        })
    end)