package("tweeny")
    set_kind("library", {headeronly = true})
    set_homepage("https://github.com/mobius3/tweeny")
    set_description("A modern C++ tweening library")
    set_license("MIT")
    set_urls("https://github.com/mobius3/tweeny/releases/download/v$(version)/tweeny-$(version).h")

    add_versions("3.2.0", "bdb8ad985a1d7ee30a3f441d67030f55de47d1961b71a0e2c3062df3c1d80fa3")
    on_install(function (package)
        local content = io.readfile("../tweeny-"..package:version()..".h"):gsub("namespace tweeny", "#include <string>\n\nnamespace tweeny")
        io.writefile(path.join(package:installdir("include"), "tweeny.h"), content);
    end)
