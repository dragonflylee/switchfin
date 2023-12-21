package("nanovg")

    set_homepage("https://github.com/memononen/nanovg/")
    set_description("Antialiased 2D vector drawing library on top of OpenGL for UI and visualizations.")
    set_license("zlib")

    add_urls("https://github.com/dragonflylee/nanovg.git")
    add_versions("2023.12.27", "12c63a2e78db3175fbbf136a77ddb0bd8e694fe4")

    on_install("windows", "macosx", "linux", function (package)
        io.writefile("xmake.lua", [[
            add_rules("mode.debug", "mode.release")
            target("nanovg")
                set_kind("$(kind)")
                add_files("src/*.c")
                add_headerfiles("src/*.h")
                add_headerfiles("src/nvg_shader/*.h", {prefixdir="nvg_shader"})
                if is_plat("windows") and is_kind("shared") then
                    add_rules("utils.symbols.export_all")
                end
        ]])
        local configs = {}
        if package:config("shared") then
            configs.kind = "shared"
        end
        import("package.tools.xmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:has_cfuncs("nvgBeginFrame", {includes = "nanovg.h"}))
    end)