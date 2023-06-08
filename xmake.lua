option("window")
    set_default("glfw")
    set_showmenu(true)
option_end()

option("driver")
    set_default("opengl")
    set_showmenu(true)
option_end()

option("sw")
    set_default(false)
    set_showmenu(true)
option_end()

if is_plat("windows") then
    add_cxflags("/utf-8")
    set_languages("c++20")
    if is_mode("release") then
        set_optimize("faster")
    end
else
    set_languages("c++17")
end

add_repositories("local-repo library")
add_requires("borealis")
add_requires("lunasvg")
add_requires("libcurl")
add_requires("mpv", {configs={shared=true}})

set_version("0.1.0", {build = "$(shell git rev-list --count --all)"})

target("Switchfin")
    add_includedirs("app/include")
    add_files("app/src/**.cpp")
    add_defines("BRLS_RESOURCES=\"./resources/\"")
    local driver = get_config("driver")
    if driver == "opengl" then
        add_defines("BOREALIS_USE_OPENGL")
    elseif driver == "d3d11" then
        add_defines("BOREALIS_USE_D3D11")
    elseif driver == "metal" then
        add_defines("BOREALIS_USE_METAL")
    end
    if get_config("window") == 'sdl' then
        add_defines("__SDL2__=1")
        add_packages("sdl2")
    else
        add_defines("__GLFW__=1")
    end
    if get_config("sw") then
        add_defines("MPV_SW_RENDER=1")
    end
    add_packages("borealis", "lunasvg", "libcurl", "mpv")
    if is_plat("windows", "mingw") then
        set_configvar("CMAKE_SOURCE_DIR", "$(projectdir)");
        add_configfiles("app/app_win32.rc.in")
        add_files("$(buildir)/app_win32.rc")
        add_defines("BOREALIS_USE_STD_THREAD")
        after_build(function (target)
            for _, pkg in pairs(target:pkgs()) do
                if pkg:has_shared() then
                    for _, f in ipairs(pkg:libraryfiles()) do
                        if f:endswith(".dll") then
                            os.cp(f, target:targetdir().."/")
                        end
                    end
                end
            end
            os.cp("resources", target:targetdir().."/")
        end)
    end
    if is_mode("release") then
        if is_plat("mingw") then
            add_cxflags("-Wl,--subsystem,windows", {force = true})
            add_ldflags("-Wl,--subsystem,windows", {force = true})
        elseif is_plat("windows") then
            add_ldflags("/MANIFEST:EMBED", "/MANIFESTINPUT:app/app.manifest", {force = true})
            add_ldflags("/SUBSYSTEM:WINDOWS", "/ENTRY:mainCRTStartup", {force = true})
        end
    end
    on_config(function (target)
        target:add("defines", "BUILD_PACKAGE_NAME="..target:name())
        target:add("defines", "BUILD_TAG_VERSION="..target:get("version"))
        target:add("defines", "BUILD_TAG_SHORT=$(shell git rev-parse --short HEAD)")
    end)