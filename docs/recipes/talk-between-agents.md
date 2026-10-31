# Recipe: make agents talk to each other

How one agent tells others something: a squad call, a message to one actor, a request and its answer. No brain
ever reads another brain ([../concept.md](../concept.md) principle 6); agents talk through **events with
parameters**, which the receiver declares and handles like any other event. Pick the first row that fits:

| Need | Use |
|---|---|
| Tell the rest of my group, maybe after a delay | `Radio` with `<Param>` children (step 1) |
| The host (C++) tells a group or one agent | `UADSKGroupSubsystem::Broadcast`, or `EnqueueEvent` on one brain (step 2) |
| Tell one chosen actor, a role, everyone in a radius, or answer a request | a project node with `bTakesArgs` (step 3) |
| Share a fact every member reads all the time (the group's target) | a host service read by a sensor (`GroupTarget`), not messages |

## Steps

1. **A group call from the file.** The sender wraps what it says in `<Param name from/>` children, each naming one
   of its slots (any type but an enum); the values are read when `Radio` runs:

   ```xml
   <Radio id="alert_call" event="Contact" delay="0.5">
     <Param name="who" from="Target"/>
     <Param name="where" from="TargetLocation"/>
   </Radio>
   ```

   Every receiver's file declares the event with exactly those parameters and binds what it needs:

   ```xml
   <Event name="Contact">
     <Param name="who" type="Object"/>
     <Param name="where" type="Vector"/>
   </Event>
   ...
   <On event="Contact" goto="investigate"><Bind param="where" to="HeardAt"/></On>
   ```

   A receiver whose file does not declare the event, or declares other parameters, refuses it (the trace records
   `input-rejected`); the call still succeeds for the sender. The host puts actors in groups with
   `UADSKGroupSubsystem::Join`.
   Check: `adsk_validate` on both files exits `0`; in PIE, `adsk.Status` shows the receivers' new state and
   `adsk.DumpTrace` the bound Local.

2. **A call from C++.** The same delivery without a node:

   ```cpp
   const FADSKNamedValue Params[] = {{TEXT("who"), FADSKObjectHandles::Make(Seen)}, {TEXT("where"), FADSKValue::MakeVector(Seen->GetActorLocation())}};
   UADSKGroupSubsystem::Get(this)->Broadcast(*GetPawn(), TEXT("Contact"), 0.5f, Params);
   // one agent only:
   if (UADSKBrainComponent* Brain = UADSKGroupSubsystem::FindBrain(Other)) { Brain->EnqueueEvent(TEXT("Contact"), Params); }
   ```

   `EnqueueEvent` returns why it refused (`UnknownName`, `UnexpectedParameter`, `TypeMismatch`, `NotRunning`).
   Check: the receiver's handler runs on its next tick.

3. **A project node for any other routing.** Write a leaf with `bTakesArgs` in the game module
   ([add-a-project-node.md](add-a-project-node.md)); `Context.ReadArgs()` gives the `<Param>` values in the shape
   `EnqueueEvent` takes. The plugin compiles one with its tests, `SampleSendTo` in
   [SampleNodes.cpp](../../Source/AdamskAITests/Specs/Samples/SampleNodes.cpp): it sends `event` to the actor in
   an Object slot, and a file uses it as

   ```xml
   <SampleSendTo id="tell_leader" event="Contact" in-target="Leader">
     <Param name="where" from="TargetLocation"/>
   </SampleSendTo>
   ```

   The same shape covers the other routings; only how the node picks receivers changes: members with a role
   (`UADSKGroupSubsystem::GetMembers` filtered by the project's own role data), actors in a radius (an overlap
   query), the group's leader (`GetLeader`). For a **request and its answer**, the asker passes itself
   (`<Param name="from" from="Self"/>`, with the host publishing `Self` as an Object Input); the receiver binds
   `from` to a Local and answers with its own `SampleSendTo in-target="ThatLocal"`.
   Check: `adsk_validate` shows the node in the project catalogue with "Params: ... from" and the files that use
   it exit `0`.

## Rules that keep it deterministic

- A message lands in the receiver's queue and runs on **its next tick**, never inside the sender's tick.
- Values are copied when sent: a receiver sees the sender's slot as it was then. An Object is a handle; the actor may
  be gone by the time it is read, so check for `none` in the receiver as for any Object.
- Enums do not travel: an enum value is an index into one file's enum. Send an Int, or one event per case.
- Fan-out, roles, radii and queues are the project's choice and live in its node or service, not in the plugin.
