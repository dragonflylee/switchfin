package("webp")
    set_homepage("https://chromium.googlesource.com/webm/libwebp")
    set_description("WebP codec is a library to encode and decode images in WebP format.")
    set_license("BSD-3")
    set_urls("https://github.com/webmproject/libwebp/archive/v$(version).tar.gz")
    add_versions("1.3.1", "1c45f135a20c629c31cebcba62e2b399bae5d6e79851aa82ec6686acedcf6f65")

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
