set_xmakever("2.8.3")

local project = "mcp-safe-lua"
local version = "0.4.0"
local license = "Apache-2.0"
set_project(project)
set_version(version)
set_license(license)

add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_warnings("all", "extra", "pedantic")
add_cxflags("-Wno-parentheses")

add_requires("glaze 7.0.x", "lua 5.5.x")

target(project)
    set_kind("binary")
    add_files("src/main.cpp", "src/stream.cpp")
    add_packages("glaze", "lua")
    set_configvar("project", project)
    set_configvar("version", version)
    add_configfiles("src/config.hpp.in")
    add_includedirs("$(builddir)")
    local document = "share/doc/" .. project
    add_installfiles("LICENSE", {prefixdir = document})
    add_installfiles("NOTICE", {prefixdir = document})
