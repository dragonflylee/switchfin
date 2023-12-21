package("nanovg")
    set_homepage("https://github.com/memononen/nanosvg")
    set_description("Simple stupid SVG parser")
    set_license("zlib")

    set_urls("https://github.com/zeromake/nanovg.git")
    add_versions("2023.03.29", "aa6917c02688ceb72d30fc31f34f0bdfc9b4a559")

    on_install(function (package)
        local configs = {}
        import("package.tools.xmake").install(package, configs)
    end)