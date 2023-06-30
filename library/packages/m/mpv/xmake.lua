package("mpv")
    if is_plat("windows", "mingw") then
        set_urls("https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/$(version)/mpv-dev-x86_64-$(version)-git-650c53d.7z")
        add_versions("20230618", "86e8a0a841ea457c554d7fe214b8ff38f0662c4342dbf53b766a00c17afc4238")
    end
    add_links("mpv")
    on_install("windows", "mingw", function (package)
        os.cp("include/*", package:installdir("include"))
        os.cp("*.a", package:installdir("lib"))
        os.cp("*.dll", package:installdir("bin"))

        -- 从 dll 里导出函数为 lib 文件，预编译自带 def 文件格式不正确，没法导出 lib
        if os.isfile("mpv.def") then
            local def_context = io.readfile("mpv.def")
            if not def_context:startswith("EXPORTS") then
                io.writefile("mpv.def", format("EXPORTS\n%s", def_context))
            end
        end

        import("detect.sdks.find_vstudio")
        for _, vsinfo in pairs(find_vstudio()) do
            if vsinfo.vcvarsall then
                os.setenv("PATH", vsinfo.vcvarsall[os.arch()]["PATH"])
            end
        end

        os.execv("lib.exe", {"/name:libmpv-2.dll", "/def:mpv.def", "/out:mpv.lib", "/MACHINE:X64"})
        os.cp("*.lib", package:installdir("lib").."/")
        os.cp("*.exp", package:installdir("lib").."/")
    end)