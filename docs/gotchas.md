# Gotchas

- **Commandlet module phase.** `AdamskAIEditor` loads in `Default`. At `PostEngineInit` the engine looks up
  `-run=AdamskAIValidate` first and fails with "could not find the class".
- **Zero files is not a pass.** A validation run that finds no behavior files exits `2`.
- **Warnings fail validation.** `unreferenced-tree` is a warning for the compiler (a definition exists), but the
  validator exits `1`, and a valid fixture may not produce any diagnostic.
- **63 states, not 64.** The state machine counts as a state; the 64th declared state is `limit-exceeded`.
- **Element 1025.** The root `Agent` and every element count toward 1024; the error lands on the last element.
- **One crash looks like silence.** A missing `ADSK_VALIDATE_COMPLETE` marker, a signal or a timeout is exit `3` from
  the scripts. Read `Saved/Logs/adsk_validate_*.log`.
- **Relative commandlet paths.** The commandlet runs from the engine's working directory; pass absolute `-paths=`,
  `-describe=` and `-catalog=` paths when calling it directly. The scripts make a relative `-Paths` / `--paths`
  absolute against the directory they were started from.
- **`-ExecCmds` and `;`.** The portable test fallback separates commands with a comma, like the project tooling; a
  semicolon left the editor running.
- **Providers register early.** Anything that assembles the catalogue (a brain starting, a validation, a test) freezes
  it. A provider that registers afterwards makes the next lookup assemble it again, but definitions already compiled
  keep the old catalogue until their file reloads ([reload.md](reload.md)).
- **Case.** Node names, attributes, ports, ids, event names and provider names are exact-case; `moveto` is not `MoveTo`.
- **`Emit` is never same-tick.** It enters the shared queue and dispatches on the next tick at the earliest.
- **`Selector` does not look back.** An earlier option that becomes valid again is not retried until the selector restarts.
- **Handler order matters.** Conditional `On` handlers for one event are tried in document order; one placed after an
  unconditional handler never runs (`unreachable-transition`).
- **Enum values are names.** Write them with `SetEnumInput(Input, ValueName)`; never store an enum index across a reload.
- **`Run` shares slots and prefixes ids.** Every call site writes the same Locals; copied nodes are `callsite/id`.
  An authored id such as `reload1/aim` (or `Reload1/Aim`) collides with the copy (`duplicate-id`).
- **Node ids are unique ignoring case.** `aim` and `Aim` in one file is `duplicate-id`: at runtime ids are `FName`s.
- **Source errors hide reference errors.** An unresolved goto, tree or `Run` is only reported once the source walk is clean.
- **Slots, not literals.** `in-goal="1,2,3"` is an unresolved slot name, not a vector.
- **A sensor writes an Input and runs every brain tick.** Its `out-` port on a Local is `type-mismatch`; keep
  `LineOfSight` `traces` low on many agents. `adsk_simulate` runs no sensors: a scenario sets their Inputs.
- **Inputs persist across `Stop`; Locals and execution state do not.** Set required inputs before `Start`.
- **A project without providers has no project catalogue.** `adsk_validate.sh --all` then prints
  `note: no project node providers; project catalogue not required` for the `full-catalog` check. Once a provider
  registers nodes, `Content/<BehaviorRoot>/node-catalog.md` must exist and match.
- **The behavior root is a setting.** `UADSKSettings::BehaviorRoot` (default `AI`) is Content-relative; an empty,
  absolute or `..` value falls back to `AI` with one warning. Staging is still the project's
  `+DirectoriesToAlwaysStageAsUFS` line.
- **Recorder off means no traces.** `RecorderCapacity=0`, and every Shipping build, create brains without a recorder:
  `adsk.DumpTrace` reports `brain-not-initialized` and the debug panel shows no records.
- **`.XML` is not `.xml`.** Behavior file names end in lowercase `.xml`. Windows finds `Guard.XML` anyway, so the
  validator reports it as `source-read` instead of silently skipping it; rename the file.
- **A new definition forgets the old inputs.** `InitializeFromSource` / `InitializeDefinition` drop every input
  cached for the previous file; publish the required inputs again before `StartLogic`. Only a reload of the
  same file (`ReplaceDefinition`) replays them.
- **Windows has its own traps** (Expat linkage, MSVC C4459 in BuildPlugin, `-stdout`, PowerShell 5.1):
  [windows.md](windows.md).
- **A tree that finishes unhandled holds.** No `tree.success`/`tree.failure` handler on the active path leaves the brain
  in Hold until an event moves it; the brain logs one `LogAdamskAIHost` warning per Hold. The validator prints a
  `note hold-risk` for every leaf state whose tree can end with an outcome no unconditional handler on its state
  path takes. It reasons from node kinds only (`Wait`, `Roll` and `Emit` never fail, a `Selector` fails only when
  every option can, any provider leaf or scope can fail), so a note can be a hold the file means; notes never
  change the exit code. A provider leaf that only succeeds sets `FADSKNodeDescriptor::bNeverFails`.
