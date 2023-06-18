package("borealis")
    set_homepage("https://github.com/xfangfang/borealis")
    set_sourcedir(path.join(os.projectdir(), "library/borealis"))

    add_configs("window", {description = "use window lib", default = "glfw", type = "string"})
    add_configs("driver", {description = "use driver lib", default = "opengl", type = "string"})
    add_configs("winrt", {description = "use winrt api", default = false, type = "boolean"})
    add_deps("nanovg", "yoga", "nlohmann_json", "fmt", "tweeny", "stb", "tinyxml2")
    add_includedirs("include")
    if is_plat("windows", "mingw") then
        add_includedirs("include/compat")
        add_syslinks("Wlanapi", "iphlpapi", "Ws2_32")
    end
    on_load(function (package)
        local window = package:config("window")
        local driver = package:config("driver")
        local winrt = package:config("winrt")
        if window == "glfw" then
            package:add("deps", "xfangfang_glfw")
        elseif window == "sdl" then
            package:add("deps", "sdl2")
        end
        if driver == "opengl" then
            package:add("deps", "glad")
        elseif driver == "d3d11" then
            package:add("syslinks", "d3d11")
        end
        if winrt then
            package:add("syslinks", "windowsapp")
        end
    end)
    on_install(function (package)
        local configs = {}
        configs["window"] = package:config("window")
        configs["driver"] = package:config("driver")
        local winrt = package:config("winrt")
        configs["winrt"] = winrt and "y" or "n"
        import("package.tools.xmake").install(package, configs)
        os.cp("library/include/*", package:installdir("include").."/")
        os.rm(package:installdir("include/borealis/extern"))
        os.cp("library/include/borealis/extern/libretro-common", package:installdir("include").."/")
    end)
package_end()