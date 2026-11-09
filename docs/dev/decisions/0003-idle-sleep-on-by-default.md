# 0003: Idle sleep on by default

Status: Accepted, 2026-10-31
Evidence: [AdamskAI.Host.Sleep], [AdamskAIWorld.Perf.ShuttlingFleetSleeping], [docs/dev/perf.md]
Commits: f4b5c67

## Context

Before brains slept, the bench (1000 agents, Linux, Development editor) put AdamskAI managed at 0.33–0.61 µs per agent
and frame against 0.10–0.16 µs for a Behavior Tree and 0.22–0.32 µs for StateTree ([roadmap.md](../roadmap.md),
performance). Most of that time went to brains that were waiting or walking and had nothing to decide.

## Decision

`UADSKSettings::bSleepWhenIdle` is on by default, as Behavior Trees and StateTree sleep by default. A brain asks the
instance for its next wake (`FADSKInstance::GetNextWake`, a node's `OnNextWake`) and is not ticked until then or until
an Input, Event or completion wakes it.

## Rejected

Not recorded.

## Consequences

- With sleep the same bench measured AdamskAI `bAutoTick` 0.08–0.11 µs and managed 0.09–0.15 µs.
- A brain stays awake while something pre-ticks it every frame: a sensor, the perception bridge or a project
  controller bound to `OnPreTick` (roadmap PERF-7). Facts should be published when they change, not every frame.
- A node with its own timing must report it through `OnNextWake`, or the brain sleeps past it.
