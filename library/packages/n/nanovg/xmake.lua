package("nanovg")
    set_homepage("https://github.com/memononen/nanovg")
    set_description("Antialiased 2D vector drawing library on top of OpenGL for UI and visualizations.")
    set_license("zlib")
    -- 如果在引用项目的上层有开发中的引用本地的
    local p = path.join(os.projectdir(), "../nanovg")
    if os.exists(p) and os.isdir(p) then
        set_sourcedir(p)
    else
        set_urls("https://github.com/zeromake/nanovg/archive/aa6917c02688ceb72d30fc31f34f0bdfc9b4a559.zip")
        add_versions("latest", "d05da01345d86cf5b1f3990be87ea7b4279827acbad3ebb50b75308f8c258dc6")
    end
    on_install(function (package)
        local configs = {}
        import("package.tools.xmake").install(package, configs)
    end)