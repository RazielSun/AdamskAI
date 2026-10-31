# Linux

Everything a Linux machine needs to build, check and test AdamskAI, and the traps a tree coming from Windows hits.
The general deployment steps are in [setup.md](setup.md); which command proves what is in [testing.md](dev/testing.md);
the Win64 counterpart is [windows.md](windows.md).

## Prerequisites

| Need | Why | Verified with |
|---|---|---|
| Unreal Engine 5.8 (source build or the Linux binary release), registered in `~/.config/Epic/UnrealEngine/Install.ini` under the `.uproject`'s `EngineAssociation`, or `UE_ROOT` set to its root | the target engine; every script resolves it that way and never guesses a path | 5.8.4 |
| The engine's bundled clang toolchain | UBT compiles with it | the one 5.8.4 ships |
| `bash`, `timeout`, `mktemp`, `rsync` | the `.sh` scripts; `rsync` copies the plugin in `adsk_host_check.sh` | coreutils, rsync 3 |
| Python 3.8+ as `python3` | `adsk_check` rules, the test verdict and the perf compare are shared Python | 3.12 |
| `xmllint` (package `libxml2-utils`) | `adsk_schema_check.sh` only; it exits `2` without it | libxml 2.9.14 |

On Debian or Ubuntu:

```bash
sudo apt install python3 rsync libxml2-utils
```

## Deploy and verify

From the project root, with no editor of this project running:

```bash
# once: copy the plugin to Plugins/AdamskAI, enable it in the .uproject, add AdamskAI and
# AdamskAIHost to the game module's Build.cs (setup.md steps 1-5), then build <Project>Editor
# Linux Development with the project's build script or UBT
T=Plugins/AdamskAI/Tools
$T/adsk_all.sh --world          # check, validate, schema, simulate, AdamskAI.* and AdamskAIWorld.*
$T/adsk_host_check.sh           # the plugin alone in an empty host, strict includes included
```

## Traps

- **A tree copied from Windows loses its execute bits.** Every script then fails with `Permission denied`; run
  `find Plugins/AdamskAI -name '*.sh' -exec chmod +x {} +`. The plugin's `.gitattributes` keeps `.sh` and `.py` at
  LF; files copied outside git may still carry CRLF, which bash refuses (`$'\r': command not found`).
- **clang is stricter than MSVC.** Code that only ever built on Win64 can fail here. Seen so far: a nested struct
  with default member initializers used as a default argument inside its enclosing class, and
  `-Wrange-loop-construct` (`-Werror`) on a loop variable of the wrong pair type. Run `adsk_host_check.sh` after a
  Win64-only change.
- **`FJsonObject::Values` is keyed by `TSharedString` on some engine builds** (`UE_JSONOBJECT_LEGACY_STRING_KEYS`
  off), `FString` on others. Iterate with `const auto&` and take the key as `FString(*Pair.Key)`.
- **Never build while an editor of this project is running.** The build replaces the binaries it holds; editors of
  other projects of the same engine can turn a build into hot-reload module copies, which a project disables with
  `[BuildConfiguration] bAllowHotReloadFromIDE=False` in `Config/DefaultEngine.ini`.
