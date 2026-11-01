# Contributing

Thanks for looking. AdamskAI is a plugin for Unreal Engine 5.8, so changes are built and tested inside a host
project: any C++ project with the plugin under `Plugins/AdamskAI/` ([docs/setup.md](docs/setup.md)). Coding agents
follow [AGENTS.md](AGENTS.md), which this file shares its rules with.

## Before you start

- **A bug:** open an issue with the behavior file (or the smallest one that shows it), what you expected, and the
  output of `adsk_validate` or `adsk.DumpTrace`. A run replays from its seed, inputs and events, so a trace is often
  enough to reproduce it.
- **A feature:** check [docs/dev/roadmap.md](docs/dev/roadmap.md) first. Most features there wait for a trigger, a real file
  that needs them; an issue that shows that file is the fastest way to move one. Genre-specific nodes (combat,
  stealth, strategy) belong in a project or a separate plugin, not here
  ([docs/dev/roadmap.md](docs/dev/roadmap.md#where-new-code-goes)).
- **Platforms:** 0.5.0 is verified on Linux, Win64 on 0.4.0 (a re-run is owed), Mac is not ([docs/linux.md](docs/linux.md),
  [docs/windows.md](docs/windows.md)).

## Making a change

1. Build the host's editor target with no editor of that project running.
2. Run the usual loop and keep it green. Linux: `Plugins/AdamskAI/Tools/adsk_all.sh --world`; Windows:
   `powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_all_win64.ps1 -World`. It runs the static
   rules, validation with both catalogues and the schemas, the simulations and the `AdamskAI.*` and
   `AdamskAIWorld.*` tests. Which command proves what: [docs/dev/testing.md](docs/dev/testing.md).
3. For a change to a `Build.cs`, a public header or anything you release, also run the portability checks:
   `adsk_strict_includes` and the plugin alone in an empty host (`adsk_host_check.sh` / `adsk_buildplugin_win64.ps1`).
4. A changed node, field or port: regenerate `docs/node-catalog.md` and `docs/adamskai.xsd`
   ([docs/dev/testing.md](docs/dev/testing.md#regenerate-the-catalogue-and-the-schema)); never edit them by hand.
5. Update the documents your change affects in the same change: the reference a user reads, a C++ break in
   [docs/migration.md](docs/migration.md), a summary line in [CHANGELOG.md](CHANGELOG.md), and delete a roadmap item
   that landed.

## Code rules

`adsk_check` enforces the first four:

- Every `.h`, `.cpp` and `.cs` starts with the MIT header line.
- A header's `.generated.h` include is its last include.
- `check()` only asks questions: it compiles out of Shipping and Test. Use `verify()` or store the result.
- Build.cs dependencies come from an allowlist; the core `AdamskAI` module never depends on or includes Engine; no
  file includes ImGui.
- The plugin never includes project game code; host features reach a pawn through interfaces the project
  implements.
- Match the surrounding code: naming (`FADSK`, `UADSK`, `EADSK`, `IADSK`), comment density, one-sentence doc comments
  on public API.
- Behavior files follow [docs/xml-style.md](docs/xml-style.md).

## Tests

`Bench/AdamskAIBench/AdamskAIBench.uplugin` sits inside this plugin on purpose: UBT and the editor stop searching a
folder at the first `.uplugin`, so a host never builds the bench. `Tools/adsk_bench.sh` copies it into a host of its
own.

- A new rule or node comes with a test under `Source/AdamskAITests/Specs/<Area>/`; the area folders show the
  patterns (`CQTest`, `TEST_CLASS_WITH_FLAGS`).
- An invalid input goes in `Fixtures/` with the exact diagnostic it must produce; check the compiler's diagnostic is
  the right one before you add the expectation.
- A behavior that could loop gets a simulation scenario ([docs/dev/testing.md](docs/dev/testing.md#simulation)).
- A crash, a timeout or a missing completion marker is never a pass.

## Pull requests

One topic per pull request. Say what you changed, which commands you ran and with what result, and what you did
not verify (a platform, PIE, a packaged build). By contributing you agree your work is released under the
[MIT License](LICENSE).
