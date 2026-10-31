# Reload and the native-restart boundary

## What exists today

Validation is the supported edit loop: edit XML, run
`Plugins/AdamskAI/Tools/adsk_validate.sh`, and start a new world. `UADSKWorldSubsystem` caches one
compiled definition per source path for the life of a Game/PIE world and compiles against the single
frozen assembled registry; a brain created later in that world reuses the cached definition.

## Transactional reload

Per-world, per file:

1. resolve, size-check, read and compile the file against the assembled registry;
2. on any error keep the running version and report diagnostics;
3. validate the candidate against every affected live brain's cached input bindings;
4. queue it (the newest request replaces an older uncommitted candidate); nothing stops during preparation;
5. at that world's next pre-actor-tick, prepare every affected brain (bindings and inputs; one refusal rejects the
   reload and nothing stops), then stop the running ones deepest-first, publish the definition,
   re-apply each brain's cached inputs to the new instance (`OnDefinitionReplaced` fires), and restart
   only brains that were running. A brain that a stop callback cleaned up meanwhile is skipped, never restarted.

An unchanged composite definition hash (source hash + catalogue fingerprint) is a no-op.

**State across a reload.** With `UADSKSettings::bKeepStateOnReload` (the default) each running brain is captured
(`FADSKSnapshot`) as it stops, after its running nodes are cancelled (so what they write when cancelled, such as a
`Cooldown` starting, is kept), and restarted from that snapshot on the new definition: it enters the deepest
state of its old path that the edited file still has, then that state's entry children, and keeps every Local,
node state struct (a `Cooldown`'s ready time), the random stream and the clock whose name and type still match, and
each change baseline whose event still watches the same slot. The leaf state's tree starts from its root: a half-done
move or attack starts over; a brain that was holding in that same leaf holds again instead of rerunning its tree. What the new file
could not take is logged once per brain (`LogAdamskAIHost`, `restored without local X, state y`; `agent z` first when
the file's agent id changed, which restores all the same). Queued events and
pending results are dropped either way. With the setting off, a reload restarts every brain in its initial state with
Locals at their defaults. `adsk.Restart` always starts clean. Hosts must recreate any edge-derived state after
restart; for example, a controller re-announces its unchanged current target.

A host can use the same snapshot itself: `UADSKBrainComponent::CaptureSnapshot` and `StartLogicFrom` (or
`FADSKInstance::CaptureSnapshot` / `Start(const FADSKSnapshot&)`). `CaptureSnapshot` leaves the brain running, so it
misses what its running nodes would write when cancelled; a host that stops the brain anyway (a save on level exit)
uses `StopLogicAndCaptureSnapshot` (`FADSKInstance::StopAndCaptureSnapshot`), which a reload uses too. Object values in it are process-wide ids, so a
snapshot written to disk for a save game needs the host to map them to something stable; the plugin does not
serialize snapshots.
The editor directory watcher
on the behavior root (`Content/<BehaviorRoot>/`, default `Content/AI/`; `.xml`, Added/Modified only, 0.15 s quiet, 1.0 s maximum defer, PIE/Game worlds only)
calls the same reload API and contains no second compiler. Cooked builds have no live reload.
Each watched save logs `AdamskAI reload <file>: queued|unchanged|rejected [reason]` to `LogAdamskAIHost`; a file that
does not compile is followed by one `  <file>:<line>: <code> <message>` line per error.

Candidate object preloads stay pending until the same transactional promotion as the definition. A rejected candidate keeps the active definition and its retained objects; pending refs are released. Binding validators, including perception bindings, can reject a reload without disturbing the active definition.

## Console commands

Every reply goes to the console that ran the command (the `~` console in PIE) and to the log.
A brain is addressed by `all`, the `brain=` (controller) or `pawn=` name that `adsk.Status` prints, or
a full object path. A short name must be unique in the world; otherwise the command refuses.

| Command | Does | Replies |
|---|---|---|
| `adsk.Status` | lists every brain | one `adsk: brain=… pawn=… source=… running=… state=… ticks=… hash=…` line per brain, then a count |
| `adsk.Reload [relative-file\|all]` | the transactional reload above | one result line per file |
| `adsk.Restart <address>` | stops and starts the brain again, stopped or not: on what its behavior file publishes now, or on its own definition when it was given one directly | `adsk: restarted <brain>`, or `adsk: start-failed` |
| `adsk.Event <address> <Event> [Param=value]...` | queues a declared event; a Vector is `x,y,z` | `adsk: <Event> queued for N of M brain(s)` |
| `adsk.DumpTrace <address>` | writes JSONL under `Saved/AdamskAI/<world>/` | the written path |

An injected event only changes what the behavior file makes it change. When a tree is guarded by a
controller-published input (for example `HasTarget`), an event such as `TargetSeen` without a real target
fails that guard at once; an event whose handler binds a Vector parameter and moves there
(`Damaged Location=x,y,z` in a typical enemy file) is the quickest one to see working.

## The native boundary (true now)

- XML changes can reload; **native** changes cannot: a new or edited node descriptor, provider, config or
  memory struct, host node or C++ change alters the catalogue and needs an editor restart.
- A provider registered or unregistered after first assembly (a Game Feature activating) makes the next lookup
  assemble the catalogue again (`LogAdamskAIHost` says so). Definitions already compiled keep the registry they
  were built with; a file compiles against the new one at its next Acquire or reload. The provider's module must
  outlive every definition that uses its nodes.
- The editor watches BehaviorRoot and every behavior mount (`@Mount/…`), and compiles changed files on a worker
  (`RequestReloadAsync`); the candidate is prepared and committed in a later pre-actor tick, and a newer edit
  arriving meanwhile replaces it. `adsk.Reload` stays synchronous.
- Renaming or moving an asset in the editor rewrites every behavior file attribute that named its old path (and
  a Blueprint's old `_C` class path); the validator warns about a path that still resolves through a redirector.
- Rebuild only with the editor closed, as the project's build rules require.
