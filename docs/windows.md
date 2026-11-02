# Windows (Win64)

Everything a Win64 machine needs to build, check and test AdamskAI, and the traps the first Win64 pass
hit. The general deployment steps are in [setup.md](setup.md); which command proves what is in
[testing.md](dev/testing.md). What is verified and what is left: [roadmap.md](dev/roadmap.md).

## Prerequisites

| Need | Why | Verified with |
|---|---|---|
| Unreal Engine 5.6–5.8 (launcher or source build) | the target engine | 5.8.4 |
| Visual Studio with the C++ game workload and an MSVC the engine accepts | UBT compiles with it | MSVC 14.51 (VS 18 / 2026) |
| Python 3.8+ as `python` or `py -3` | `adsk_check` rules and the test verdict are shared Python | 3.11; the engine's own `Engine\Binaries\ThirdParty\Python3\Win64\python.exe` is the fallback |
| Windows PowerShell 5.1 or PowerShell 7 | the `_win64.ps1` scripts; `adsk_schema_check_win64.ps1` uses .NET's `XmlSchemaSet`, so nothing else to install | 5.1 |

## Deploy and verify

From the project root, with no editor running:

```powershell
# once: copy the plugin to Plugins\AdamskAI, enable it in the .uproject, add AdamskAI and
# AdamskAIHost to the game module's Build.cs (setup.md steps 1-5)
$T = 'Plugins\AdamskAI\Tools'
powershell -ExecutionPolicy Bypass -File $T\adsk_build_win64.ps1      # <Project>Editor Win64 Development
powershell -ExecutionPolicy Bypass -File $T\adsk_check_win64.ps1
powershell -ExecutionPolicy Bypass -File $T\adsk_validate_win64.ps1
powershell -ExecutionPolicy Bypass -File $T\adsk_test_win64.ps1
```

Each script's header comment documents its parameters, examples, traps and exit codes; read it before
passing anything unusual. Before a release, or after a Build.cs or public-header change, also run
`adsk_strict_includes_win64.ps1` and `adsk_buildplugin_win64.ps1 -OutDir <empty dir>`.

The engine is found the way the launcher finds it: `UE_ROOT` first, then the `.uproject`
`EngineAssociation` in `HKCU\Software\Epic Games\Unreal Engine\Builds` (source builds) or
`HKLM\SOFTWARE\EpicGames\Unreal Engine\<version>` (launcher installs). The project is `-Project`, else the
Git root, else the nearest folder above the plugin holding exactly one `.uproject`.

A project can hook in the same way it does on Linux: `Tools\run_tests_win64.ps1` replaces the plugin's
test runner (`adsk_test_win64.ps1 -Standalone` skips it), `Tools\lib\preflight_win64.ps1` can refuse a
run, and `ADSK_PREFLIGHT=<command>|none` overrides both.

## Traps the first Win64 pass hit

Each of these cost a failed run once. The fixes are in the code and scripts; the list is here so nobody
undoes one.

**Build**

- **Expat links statically.** Without `XML_STATIC`, `expat_external.h` declares every function
  `__declspec(dllimport)` and `AdamskAI` fails to link with `LNK2019 __imp_XML_*`. `AdamskAI.Build.cs`
  defines it, as the engine's own Expat users do. Linux never sees this: there the same macro is a
  visibility attribute.
- **C4459 is an error in BuildPlugin.** A namespace-scope name pulled to file scope with `using namespace`
  (the test specs' `F<File>Private` pattern) shadows any engine template parameter of the same name, and
  MSVC reports it *inside the engine header*. Only the `note: see declaration of ...` line that follows
  names the plugin file; the scripts echo those notes. Keep spec constants specific (`GuardFile`, not
  `Source`). Clang has no such warning.
- **No editor may run while you build.** It holds the module DLLs and the link fails. The scripts list the
  PIDs and refuse; never kill by process name.
- **Without a Git repository, Adaptive Unity takes every file out of the unity blob.** The normal build
  then compiles each `.cpp` alone (PCH still on). Harmless, slower; `adsk_strict_includes_win64.ps1` is
  still the real include check.
- **A target named after a template.** A project renamed from a template keeps the template's target names;
  pass `-TargetName` / `-EditorTarget` / `-GameTarget`.

- **ASan against an installed engine.** UBT builds the plugin's ASan modules with `FORCE_ANSI_ALLOCATOR=1`; the
  prebuilt engine frees their objects through mimalloc and `AdamskAIWorld.*` crashes in GC on a map change. Only
  `AdamskAI.*` runs under ASan there; `adsk_test_win64.ps1` refuses a world filter in that setup. World tests under
  ASan need a source engine built entirely with `-EnableASan`.

**Running the editor headless**

- **`-stdout` is required.** `UnrealEditor-Cmd` does not copy its log to stdout on Windows unless asked, and
  the validator and the test fallback read their verdict from it.
- **`-ExecCmds` splits on commas** and swallows the arguments after it; the test runner uses
  `Automation RunTests <filter>, Automation Quit,` with the trailing comma.
- **Arguments are parsed from the raw command line.** A `-Key=Value` with a space must reach the editor as
  `-Key="Value"`; the scripts quote it that way.
- **Crashes exit with an NTSTATUS** (`0xC0000005` reads back negative). The scripts report it as exit `3`.
- **The editor may not exit.** Every editor run has a timeout; on expiry the process tree is killed by PID
  and the result is exit `3`.

**Windows PowerShell 5.1** (each was a real false pass or a script that could not start)

- A function returning a one-element or empty array returns the element or `$null`; count with `@(...)`.
- `$PSScriptRoot` is empty inside `param()` defaults.
- A statement error does not stop a script; each script installs a `trap` that exits `2`.
- `*>` writes UTF-16 and wraps a native tool's stderr in error records; native output goes through
  `cmd /c ... >log 2>&1` instead, so logs are plain bytes `Select-String` and `grep` both read.

**First runs**

- **The first `UnrealEditor-Cmd` in a project takes up to two minutes.** `adsk_validate` measured 115 s cold against
  10 s warm: 53 s of Turnkey asking UBT for the AutoSDK (cached in `Saved/Logs/AutoSDKInfo.json`, and waiting on the
  UBT mutex while another build runs), about 13 s starting ZenServer, 10 s of shader types. Nothing of it is the
  plugin's; a timeout on that first run is not a hang.

**Files**

- **Wildcards are case-insensitive.** `*.xml` also finds `Guard.XML`; behavior file names must end in
  lowercase `.xml`, and the validator reports such a file instead of skipping it.

## Not run on Windows yet

- `adsk_host_check.sh` has no Win64 twin; `adsk_buildplugin_win64.ps1` covers the stand-alone build but
  not the stand-alone validate and test run.
- `adsk_bench.sh` has no Win64 twin. The bench ran once by hand (2026-10-31, 1000 agents, per agent and frame):
  AdamskAI 0.11–0.13 µs ticking itself and 0.10–0.11 µs managed, Behavior Tree 0.12 µs, StateTree 0.31 µs.
- Scripted PIE scenarios through Unreal MCP on a project map: see [roadmap.md](dev/roadmap.md).
