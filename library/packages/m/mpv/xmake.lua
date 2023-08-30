package("mpv")
    if is_plat("windows", "mingw") then
        set_urls("https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/20230831/mpv-dev-x86_64-20230831-git-edd7189.7z")
        add_versions("20230831", "1d6b964e97faecad291fe5cc128a3ccf04138d3a0b2ff0635290060ebaf98d34")
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