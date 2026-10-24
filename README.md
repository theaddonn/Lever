# OreSettings

A build-time static library for [LeviLamina](https://github.com/LiteLDev/LeviLamina) Minecraft Bedrock **client** mods. Embed it to get settings pages and mod info pages inside the vanilla Ore UI settings screen (a "Mods" tab), a pause-menu entry and a world-creation tab, without writing anything into the game folder.

Several mods can embed it at the same time: at runtime exactly one copy per process becomes the host and installs the hooks once, the others attach to it through a versioned C ABI.

## Status

**Pre-alpha: design and scaffold.** The consumer API, the C ABI, host election and the in-process model compile, but the game hooks are stubs, so nothing is displayed in game yet. See [docs/DESIGN.md](docs/DESIGN.md) for the plan and the open questions.

## Compatibility

| Component                     | Version          |
| ----------------------------- | ---------------- |
| Minecraft Bedrock for Windows | 1.26.51.01       |
| LeviLamina                    | 26.51.6 (client) |

## Using it in a mod

```cpp
#include "oresettings/OreSettings.h"

bool MyMod::enable() {
    using namespace ore_settings;
    registerMod({"mymod", "My Mod", {1, 0, 0}, "me", "Does things."});
    addPage("mymod", "main", "My Mod", "creator")
        .showIn(Surface::PauseMenu)
        .addToggle("on", "Enabled", "Master switch", [] { return gOn; }, [](bool v) { gOn = v; })
        .addSlider("range", "Range", "", 0, 10, 1, [] { return gRange; }, [](double v) { gRange = v; });
    return true;
}

bool MyMod::disable() {
    ore_settings::unregisterMod("mymod"); // callbacks point into your DLL
    return true;
}
```

Calls are safe at any time after the mod is loaded; they are buffered until a host exists. A complete mod is in [example/Example.cpp](example/Example.cpp).

## Adding it to your build

### As an xmake package

Put this in your `xmake.lua`, next to your LeviLamina setup:

```lua
package("oresettings")
    set_urls("https://github.com/theaddonn/OreSettings.git")
    add_versions("0.1.0", "<commit sha>")
    add_deps("levilamina")
    on_install("windows", function (package)
        import("package.tools.xmake").install(package, {target_type = "client"})
        os.cp("include", package:installdir())
        os.cp("build/windows/x64/release/oresettings.lib", package:installdir("lib"))
    end)
package_end()

add_requires("oresettings")

target("MyMod")
    -- ... your levibuildscript rules ...
    add_packages("levilamina", "oresettings")
```

The inline package builds the library with xmake, then copies `include/` and the `.lib` into the package. Adjust the build output path if your xmake version lays it out differently.

### As a git submodule

```
git submodule add https://github.com/theaddonn/OreSettings.git third_party/OreSettings
```

```lua
includes("third_party/OreSettings")  -- defines the `oresettings` static library target

target("MyMod")
    add_deps("oresettings")
    add_packages("levilamina")
```

## Building

Cross-build from Linux (clang-cl and a Windows sysroot under `~/.local/share/amtool/`):

```
export PATH=~/.local/share/amtool/bin:~/.local/bin:$PATH
xmake f -p windows -a x64 -m release --toolchain=clang-cl --sdk=~/.local/share/amtool/winsysroot -y
xmake -y
```

Add `--example=y` to also build the example mod. On Windows: `xmake f -a x64 -m release -p windows -y && xmake -y`.

The library lands in `build/windows/x64/release/oresettings.lib`.

## License

[MIT](LICENSE)
