# AGENTS.md — AdamskAI

Guidance for a coding agent deploying AdamskAI into a project or authoring its behavior files. Links are
relative to this plugin. A host project's own `AGENTS.md` / `CLAUDE.md` rules apply on top of these.

## What this is

A deterministic, XML-authored agent behavior plugin for Unreal Engine 5.8: a strict XML v1 compiler, a
registry of native nodes, a hierarchical state machine with procedure trees, a bounded recorder, an
Unreal host (`AdamskAIHost`) and a headless validator. Product: [README.md](README.md); runtime contract
(ownership, bounds, tick order, modules): [docs/contract.md](docs/contract.md).
Version 0.5.0 is verified on Linux; Win64 was verified on 0.4.0 and owes a re-run; Mac is not built. Status and
everything still left: [docs/dev/roadmap.md](docs/dev/roadmap.md).

## Rules that prevent most mistakes

- **Catalogue first.** Only nodes in a catalogue exist. Read [docs/node-catalog.md](docs/node-catalog.md)
  (core and host nodes, and sensors) before writing a tree; a project that adds nodes has its own full
  catalogue (`Content/<BehaviorRoot>/node-catalog.md`, default `Content/AI/`; see [docs/setup.md](docs/setup.md)).
  Never invent an element, attribute or port.
- **XML v1 is strict.** Exact-case names, `version="1"`, no DTD, CDATA, processing instructions,
  stray text or unknown syntax. Spec: [docs/xml-reference.md](docs/xml-reference.md).
- **Follow the file layout.** Header comment, banner per section, one-line intent comment per
  state, tree-prefixed node ids, one-line leaves: [docs/xml-style.md](docs/xml-style.md).
- **Values are `Bool`, `Float`, `Int`, `Vector`, `Object` and definition-scoped enums (Inputs, Locals, event and tree parameters).** Ports bind slots (`in-<port>` / `out-<port>`
  name an Input or Local), never literals. Configuration is explicit descriptor attributes.
- **Inputs are written by the host; outputs and `Bind` write Locals only.** An event with `change="Slot"` is raised
  by the core when that slot changes; publish the fact as an Input instead of raising an edge event from C++. A fact a
  catalogue sensor measures is declared in the file's `Sensors`, whose out ports write Inputs, not published from C++.
- **Register providers before first assembly.** A provider registered later (a Game Feature) makes the catalogue
  assemble again, but files already compiled keep the old one until their next reload. See
  [docs/authoring-nodes.md](docs/authoring-nodes.md) and [docs/reload.md](docs/reload.md).
- **Project behavior files live under the behavior root** (`UADSKSettings::BehaviorRoot`, default
  `Content/AI/`) and nowhere else. Paths are relative to it, end in exactly lowercase `.xml`, and never contain `..`.
- **XML reloads; native code restarts.** A changed or new native node, descriptor or provider needs
  an editor restart. See [docs/reload.md](docs/reload.md).
- **The plugin never includes project game code.** Project nodes and pawn decisions live in the
  game module, which depends on `AdamskAIHost`. The controller pattern is in [docs/setup.md](docs/setup.md#minimal-controller).

## Which command, on which platform

Every check is headless (no renderer, no client). A Linux script and its Win64 twin share the modes, logs, report
files and exit codes; the table pairs them, and a `—` is a script that exists on one platform only. On Windows run a
script as `powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\<script>` from the project root.

| Task | Linux | Windows |
|---|---|---|
| The whole usual loop in one run, with a summary (build is Windows only) | `adsk_all.sh [--world]` | `adsk_all_win64.ps1 [-NoBuild] [-World]` |
| Build the editor target (do this first, editor closed) | the host project's build script, or UBT | `adsk_build_win64.ps1` |
| Static plugin rules (no engine needed) | `adsk_check.sh` | `adsk_check_win64.ps1` |
| Examples, project AI, fixtures, both catalogues and schemas | `adsk_validate.sh` | `adsk_validate_win64.ps1` |
| One file or directory | `adsk_validate.sh --paths <p>[+<p>...]` | `adsk_validate_win64.ps1 -Paths <p>[+<p>...]` |
| What a file decides over time: loops, holds, states reached (no PIE) | `adsk_simulate.sh [<scenario>[+...]]` | `adsk_simulate_win64.ps1 [<scenario>[+...]]` |
| A file's state machine as a Mermaid or DOT diagram (no engine) | `adsk_graph.sh <file> [mermaid\|dot]` | `adsk_graph_win64.ps1 <file> [mermaid\|dot]` |
| Every valid file against the generated XML Schemas (after a language change) | `adsk_schema_check.sh` | `adsk_schema_check_win64.ps1` |
| `AdamskAI.*` automation tests | `adsk_test.sh [filter]` | `adsk_test_win64.ps1 [filter]` |
| Every module without unity and PCH | `adsk_strict_includes.sh` | `adsk_strict_includes_win64.ps1` |
| The plugin alone, outside any project | `adsk_host_check.sh` | `adsk_buildplugin_win64.ps1` |
| Benchmarks against this machine's baseline and the ceilings | `adsk_perf_compare.sh` | `adsk_perf_compare_win64.ps1` |
| What AdamskAI adds to and keeps out of the cook | — (the Linux project checks its own cook) | `adsk_cook_check_win64.ps1` |
| The scripts' own failure paths and exit codes | — (owed) | `adsk_selftest_win64.ps1` |

The usual loop after a change: build, check, validate, test (`adsk_all` runs them in order); after changing a behavior file, validate it and run its
scenarios (write one when a state machine change could loop: [docs/dev/testing.md](docs/dev/testing.md#simulation)). Strict
includes and the plugin-alone build are portability checks for Build.cs or header changes and releases. Platform prerequisites and traps: [docs/linux.md](docs/linux.md), [docs/windows.md](docs/windows.md).

Exit codes: `0` clean, `1` diagnostics, failed tests or a stale catalogue, `2` usage/setup/write failure or
a running editor, `3` timeout, crash or missing completion marker (never a pass). Diagnostics print as
`path:line: severity code [node]: message`; the machine report is `Saved/AdamskAI/validate.json`.
Logs are `Saved/Logs/adsk_validate_*.log`, `Saved/Logs/adsk_test.log` and (Windows builds)
`Saved/Logs/adsk_build_*.log`; the runtime log category is `LogAdamskAIHost`. Details: [docs/dev/testing.md](docs/dev/testing.md).

Console commands `adsk.Status`, `adsk.Reload`, `adsk.Restart`, `adsk.Event` and `adsk.DumpTrace`, and the
editor directory watcher on the behavior root, are described in [docs/reload.md](docs/reload.md). Live
snapshots, labels and the optional panel are in [docs/debugging.md](docs/debugging.md).

## Where the answers are

### Using the plugin (deploying it, writing behavior, adding nodes)

| Question | Answer |
|---|---|
| How do I deploy it into a project? | [docs/setup.md](docs/setup.md) |
| How do I do a whole task (new NPC, new fact or event, project node, agents talking, StateTree / BT port, "it does not move")? | [docs/recipes/](docs/recipes/README.md) |
| What does Linux need, and what breaks there? | [docs/linux.md](docs/linux.md) |
| What is different on Windows? | [docs/windows.md](docs/windows.md) |
| What is legal XML? | [docs/xml-reference.md](docs/xml-reference.md) |
| In what order does a tick do things? How do I test a semantic rule? | [docs/semantics.md](docs/semantics.md) (rules checked by `Fixtures/conformance/`) |
| How should a file be laid out? | [docs/xml-style.md](docs/xml-style.md) |
| Which nodes exist, with fields and ports? | [docs/node-catalog.md](docs/node-catalog.md) (generated; as an XML Schema for IDE completion: [docs/adamskai.xsd](docs/adamskai.xsd)) |
| How do I add a native node? | [docs/authoring-nodes.md](docs/authoring-nodes.md) |
| What reloads and what needs a restart? | [docs/reload.md](docs/reload.md) |
| How do I observe live brains? | [docs/debugging.md](docs/debugging.md) |
| How does it behave in a networked game? | [docs/networking.md](docs/networking.md) |
| What does it cost? | [docs/dev/perf.md](docs/dev/perf.md) |
| What changed in the C++ API since 0.3? | [docs/migration.md](docs/migration.md); in short, [CHANGELOG.md](CHANGELOG.md) |
| Why is it built this way? Where does a new fact, choice or action go? | [docs/concept.md](docs/concept.md) |
| What does a term mean, and what is it called in a Behavior Tree or StateTree? | [docs/glossary.md](docs/glossary.md) |
| Where do the ideas come from? | [docs/references.md](docs/references.md) |
| How does it compare with BT, StateTree, BehaviorTree.CPP, HFSM2, SCXML? | [docs/comparison.md](docs/comparison.md) |
| What can it not do? | [docs/limitations.md](docs/limitations.md) |
| Why did that surprise me? | [docs/gotchas.md](docs/gotchas.md) |
| Valid starting points | [Examples/](Examples/README.md); running scenes: [docs/demo.md](docs/demo.md) |

### Developing the plugin (verification, what is left)

| Question | Answer |
|---|---|
| Which command proves what? How do the world tests work? | [docs/dev/testing.md](docs/dev/testing.md) |
| What is verified, what is left, what is wanted but not built? | [docs/dev/roadmap.md](docs/dev/roadmap.md), the one list; delete an item there when it lands |
| How is performance measured, and against which ceilings? | [docs/dev/perf.md](docs/dev/perf.md) |
| Invalid inputs and their exact diagnostics | [Fixtures/](Fixtures/) |
| How do I propose a change? | [CONTRIBUTING.md](CONTRIBUTING.md) |

## Do not

- Edit [docs/node-catalog.md](docs/node-catalog.md) or [docs/adamskai.xsd](docs/adamskai.xsd) by hand; regenerate them
  (see [docs/dev/testing.md](docs/dev/testing.md)). Never name the schema inside a behavior file: `xsi:` attributes do not compile.
- Add an expectation to a fixture without checking the compiler's diagnostic is the right one.
- Treat a crash, timeout or missing `ADSK_VALIDATE_COMPLETE` marker as a pass.
- Build while an editor for the project is running, or kill editors by process name to get around it;
  the Windows scripts list the PIDs and refuse.
- Call a function with side effects inside `check()`: it compiles out of Shipping and Test. Use `verify()`
  or store the result; `adsk_check` enforces it.
