package("nanovg")
    set_homepage("https://github.com/memononen/nanovg/")
    set_description("Antialiased 2D vector drawing library on top of OpenGL for UI and visualizations.")
    set_license("zlib")

    add_urls("https://github.com/zeromake/nanovg.git")
    add_versions("2023.12.25", "f45d73db67eaadc3df98971872add86f660a3ee5")

    on_install("windows", "macosx", "linux", function (package)
        local configs = {}
        if package:config("shared") then
            configs.kind = "shared"
        end
        import("package.tools.xmake").install(package, configs)
    end)