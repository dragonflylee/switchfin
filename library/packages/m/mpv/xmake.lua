package("mpv")
    if is_plat("windows", "mingw") then
        set_urls("https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/20230607/mpv-dev-x86_64-20230607-git-effc680.7z")
        add_versions("20230607", "91b12131d3919b51b896b26881ca0dd7a9c23c284d3a211b12daf1c45ff44fa2")
    end
    add_links("mpv")
    on_install("windows", "mingw", function (package)
        os.cp("include/*", package:installdir("include").."/")
        os.cp("*.a", package:installdir("lib").."/")
        os.cp("*.dll", package:installdir("bin").."/")

        -- 从 dll 里导出函数为 lib 文件，预编译自带 def 文件格式不正确，没法导出 lib
        if os.isfile("mpv.def") then
            local def_context = io.readfile("mpv.def")
            if not def_context:startswith("EXPORTS") then
                io.writefile("mpv.def", format("EXPORTS\n%s", def_context))
            end
        end
        
        local find_vstudio = import("detect.sdks.find_vstudio")
        for _, vsinfo in pairs(find_vstudio()) do
            if vsinfo.vcvarsall then
                os.setenv("PATH", vsinfo.vcvarsall["x64"]["PATH"])
            end
        end

        os.execv("lib.exe", {"/name:libmpv-2.dll", "/def:mpv.def", "/out:mpv.lib", "/MACHINE:X64"})
        os.cp("*.lib", package:installdir("lib").."/")
        os.cp("*.exp", package:installdir("lib").."/")
    end)