package("nanovg")
    set_homepage("https://github.com/memononen/nanovg")
    set_description("Antialiased 2D vector drawing library on top of OpenGL for UI and visualizations.")
    set_license("zlib")
    -- 如果在引用项目的上层有开发中的引用本地的
    local p = path.join(path.directory(os.projectfile()), "../nanovg")
    if os.exists(p) and os.isdir(p) then
        set_sourcedir(p)
    else
        set_urls("https://github.com/zeromake/nanovg/archive/$(version).tar.gz", {
            version = function(version)
                local versions = {
                    ["2023.03.29"] = "aa6917c02688ceb72d30fc31f34f0bdfc9b4a559",
                }
                return versions[tostring(version)]
            end
        })
        add_versions("2023.03.29", "852bfeaf9095c6b4550fbde48357c71b323c6c508f97f1025248583d85c8e1dc")
    end
    on_install(function (package)
        local configs = {}
        import("package.tools.xmake").install(package, configs)
    end)