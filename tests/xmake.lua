-- Host (Linux) test project for Lever's platform-independent core: model, registry, ABI marshalling, interface
-- dispatch, election logic and client forwarding. Separate xmake project so its configuration never collides with
-- the Windows cross-build in the repo root. Nothing here needs LeviLamina or Windows headers.
--
--   cd tests
--   xmake f -P . -p linux -a x86_64 -m debug --cc=clang-22 --cxx=clang++-22 --ld=clang++-22 -y
--   xmake -P . -y && xmake run -P . lever_tests

add_rules("mode.debug", "mode.release")

target("lever_tests")
    set_kind("binary")
    set_languages("c++20")
    add_includedirs("../include", "../src", "third_party", ".")
    -- Everything of the library except src/platform/ (Windows + LeviLamina); tests/stubs replaces Platform.
    add_files("../src/Api.cpp", "../src/internal/*.cpp")
    add_files("main.cpp", "stubs/*.cpp", "unit/*.cpp")
    add_defines("LEVER_VERSION_STRING=\"0.2.0\"", "LEVER_VERSION_MAJOR=0", "LEVER_VERSION_MINOR=2", "LEVER_VERSION_PATCH=0")
    add_defines("DOCTEST_CONFIG_SUPER_FAST_ASSERTS")
    add_cxflags("-Wall", "-Wextra")
    if is_mode("debug") then
        add_cxflags("-fsanitize=address,undefined", "-fno-omit-frame-pointer")
        add_ldflags("-fsanitize=address,undefined", {force = true})
    end
    set_rundir("$(projectdir)")
