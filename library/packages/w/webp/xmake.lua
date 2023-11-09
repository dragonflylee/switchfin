package("webp")
    set_homepage("https://chromium.googlesource.com/webm/libwebp")
    set_description("WebP codec is a library to encode and decode images in WebP format.")
    set_license("BSD-3")
    set_urls("https://github.com/webmproject/libwebp/archive/v$(version).tar.gz")
    add_versions("1.3.2", "c2c2f521fa468e3c5949ab698c2da410f5dce1c5e99f5ad9e70e0e8446b86505")

    add_includedirs("include")
    on_install(function (package)
        io.writefile("xmake.lua", [[
            target("webp")
                set_kind("$(kind)")
                add_includedirs(".")
                add_headerfiles("src/webp/*.h", {prefixdir = "webp"})
                add_files("sharpyuv/*.c")
                add_files("src/**.c")
        ]])
        local configs = {}
        import("package.tools.xmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:has_cfuncs("WebPGetDecoderVersion()", {includes = {"webp/decode.h"}}))
    end)
