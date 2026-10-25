-- Single source of truth for the library version.
local LEVER_VERSION = "0.1.0"
set_version(LEVER_VERSION)

add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

-- Lever only targets the client; the option exists so the levilamina package config matches consumers.
add_requires("levilamina 26.51.6", {configs = {target_type = "client"}})

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

-- Build the example mod too: xmake f ... --example=y
option("example")
    set_default(false)
    set_showmenu(true)
    set_description("Build the example LeviLamina client mod that embeds Lever")
option_end()

if has_config("example") then
    add_requires("levibuildscript 0.6.1")
end

-- Compiler settings shared by the library and the example (copied from the Distant Horizons rule).
rule("lever.cxx")
    on_load(function (target)
        target:set("languages", "c++20")
        if target:is_plat("windows") then
            target:add("defines", "NOMINMAX", "UNICODE", "_HAS_CXX23=1")
            target:set("exceptions", "none") -- To avoid conflicts with /EHa.
            target:add("cxflags", "/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
            target:add("cxflags",
                "/EHs",
                "-Wno-microsoft-cast",
                "-Wno-invalid-offsetof",
                "-Wno-c++2b-extensions",
                "-Wno-microsoft-include",
                "-Wno-overloaded-virtual",
                "-Wno-ignored-qualifiers",
                "-Wno-missing-field-initializers",
                "-Wno-potentially-evaluated-expression",
                "-Wno-pragma-system-header-outside-header",
                {tools = {"clang_cl"}})
            target:set("toolchains", "clang-cl")
        end
    end)
rule_end()

target("lever")
    add_rules("lever.cxx")
    set_kind("static")
    add_packages("levilamina")
    add_includedirs("include", {public = true})
    add_includedirs("src")
    add_headerfiles("include/(lever/*.h)", "include/(lever/abi/*.h)")
    -- Portable core (also built by tests/xmake.lua on the host) plus the Windows/LeviLamina platform layer.
    add_files("src/Api.cpp", "src/internal/*.cpp", "src/platform/win32/*.cpp")
    add_defines("LEVER_VERSION_STRING=\"" .. LEVER_VERSION .. "\"")
    local major, minor, patch = LEVER_VERSION:match("^(%d+)%.(%d+)%.(%d+)")
    add_defines("LEVER_VERSION_MAJOR=" .. major, "LEVER_VERSION_MINOR=" .. minor, "LEVER_VERSION_PATCH=" .. patch)
    set_symbols("debug")

if has_config("example") then
    target("LeverExample")
        add_rules("lever.cxx")
        add_rules("@levibuildscript/linkrule")
        add_rules("@levibuildscript/modpacker", {modName = "LeverExample", modVersion = LEVER_VERSION, modPlatform = "client"})
        set_kind("shared")
        add_deps("lever")
        add_packages("levilamina")
        add_files("example/*.cpp")
        set_symbols("debug")
end
