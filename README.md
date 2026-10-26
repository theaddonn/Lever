# Lever

A build-time static library for [LeviLamina](https://github.com/LiteLDev/LeviLamina) Minecraft Bedrock **client** mods. Embed it to get settings pages and mod info pages inside the vanilla Ore UI settings screen (a "Mods" tab), a pause-menu entry and a world-creation tab, without writing anything into the game folder.

Several mods can embed it at the same time: at runtime exactly one copy per process becomes the host and installs the hooks once, the others attach to it through a versioned C ABI.

The cross-copy ABI is built for long-term backward compatibility: a frozen bootstrap table plus versioned interfaces (`lever.registry.v1`, ...) and size-prefixed descriptors, so a newer host always understands an older embedded copy. See "ABI compatibility" in [docs/DESIGN.md](docs/DESIGN.md).

## Status

**Pre-alpha.** The consumer API, the C ABI, host election and the in-process model are in place. The native hook adds a "Mods" section to the vanilla Ore UI settings screen with one page per registered mod page, rendering toggles, sliders, dropdowns, text fields, buttons and banners with the vanilla widgets. Verified in game: the overview page, every widget kind, persistence of edits, descriptions, late registration and `notifyChanged`. See [docs/DESIGN.md](docs/DESIGN.md) for the plan and the open questions.

## Compatibility

| Component                     | Version          |
| ----------------------------- | ---------------- |
| Minecraft Bedrock for Windows | 1.26.51.01       |
| LeviLamina                    | 26.51.6 (client) |

## Using it in a mod

```cpp
#include "lever/Lever.h"

bool MyMod::enable() {
    using namespace lever;
    registerMod({"mymod", "My Mod", {1, 0, 0}, "me", "Does things."});
    addPage("mymod", "main", "My Mod", "creator")
        .showIn(Surface::PauseMenu)
        .addToggle("on", "Enabled", "Master switch", [] { return gOn; }, [](bool v) { gOn = v; })
        .addSlider("range", "Range", "", 0, 10, 1, [] { return gRange; }, [](double v) { gRange = v; });
    return true;
}

bool MyMod::disable() {
    lever::unregisterMod("mymod"); // callbacks point into your DLL
    return true;
}
```

Calls are safe at any time after the mod is loaded; they are buffered until a host exists. A complete mod is in [example/Example.cpp](example/Example.cpp).

## Adding it to your build

### As an xmake package

Put this in your `xmake.lua`, next to your LeviLamina setup:

```lua
package("lever")
    set_urls("https://github.com/theaddonn/Lever.git")
    add_versions("0.1.0", "<commit sha>")
    add_deps("levilamina")
    on_install("windows", function (package)
        import("package.tools.xmake").install(package, {target_type = "client"})
        os.cp("include", package:installdir())
        os.cp("build/windows/x64/release/lever.lib", package:installdir("lib"))
    end)
package_end()

add_requires("lever")

target("MyMod")
    -- ... your levibuildscript rules ...
    add_packages("levilamina", "lever")
```

The inline package builds the library with xmake, then copies `include/` and the `.lib` into the package. Adjust the build output path if your xmake version lays it out differently.

### As a git submodule

```
git submodule add https://github.com/theaddonn/Lever.git third_party/Lever
```

```lua
includes("third_party/Lever")  -- defines the `lever` static library target

target("MyMod")
    add_deps("lever")
    add_packages("levilamina")
```

## Building

Cross-build from Linux (clang-cl and a Windows sysroot under `~/.local/share/amtool/`):

```
export PATH=~/.local/share/amtool/bin:~/.local/bin:$PATH
xmake f -p windows -a x64 -m release --toolchain=clang-cl --sdk=~/.local/share/amtool/winsysroot -y
xmake -y
```

Host unit tests (Linux, no LeviLamina needed): `cd tests && xmake f -P . -p linux -a x86_64 -m debug --cc=clang-22 --cxx=clang++-22 --ld=clang++-22 -y && xmake -P . -y && xmake run -P . lever_tests`.

Add `--example=y` to also build the example mod. On Windows: `xmake f -a x64 -m release -p windows -y && xmake -y`.

The library lands in `build/windows/x64/release/lever.lib`.

## License

[MIT](LICENSE)
