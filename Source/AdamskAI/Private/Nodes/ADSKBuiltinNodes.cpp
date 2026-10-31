// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Nodes/ADSKBuiltinNodes.h"

#include "Runtime/ADSKNode.h"
#include "Runtime/ADSKNodeRegistry.h"

namespace ADSKBuiltinNodesPrivate
{
	FADSKNodeDescriptor MakeSequence()
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = TEXT("Sequence");
		Descriptor.Kind = EADSKNodeKind::Sequence;
		Descriptor.MinChildren = 1;
		Descriptor.MaxChildren = ADSK::UnboundedChildren;
		return Descriptor;
	}

	FADSKNodeDescriptor MakeSelector()
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = TEXT("Selector");
		Descriptor.Kind = EADSKNodeKind::Selector;
		Descriptor.MinChildren = 1;
		Descriptor.MaxChildren = ADSK::UnboundedChildren;
		return Descriptor;
	}

	FADSKNodeDescriptor MakeReactiveSelector()
	{
		FADSKNodeDescriptor Descriptor = MakeSelector();
		Descriptor.Name = TEXT("ReactiveSelector");
		Descriptor.MaxChildren = ADSK::MaxReactiveChildren;
		Descriptor.SelectorPolicy = EADSKSelectorPolicy::Reactive;
		Descriptor.MemoryStruct = FADSKReactiveSelectorMemory::StaticStruct();
		return Descriptor;
	}

	FADSKNodeDescriptor MakeUtilitySelector()
	{
		FADSKNodeDescriptor Descriptor = MakeSelector();
		Descriptor.Name = TEXT("UtilitySelector");
		Descriptor.MaxChildren = ADSK::MaxReactiveChildren;
		Descriptor.SelectorPolicy = EADSKSelectorPolicy::Utility;
		Descriptor.MemoryStruct = FADSKUtilitySelectorMemory::StaticStruct();
		Descriptor.ConfigStruct = FADSKUtilityConfig::StaticStruct();
		Descriptor.Fields.Add({TEXT("reevaluate"), TEXT("Reevaluate")});
		Descriptor.Fields.Add({TEXT("margin"), TEXT("Margin")});
		Descriptor.ValidateConfig = [](const void* Config)
		{
			const FADSKUtilityConfig& Utility = *static_cast<const FADSKUtilityConfig*>(Config);
			return FMath::IsFinite(Utility.Reevaluate) && Utility.Reevaluate >= 0.f && FMath::IsFinite(Utility.Margin) && Utility.Margin >= 0.f;
		};
		return Descriptor;
	}

	/** A scope that only scores: it runs its child unchanged and tells a UtilitySelector what the option is worth. */
	FADSKNodeDescriptor MakeScore()
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = TEXT("Score");
		Descriptor.Kind = EADSKNodeKind::Scope;
		Descriptor.MinChildren = 1;
		Descriptor.MaxChildren = 1;
		Descriptor.ConfigStruct = FADSKScoreConfig::StaticStruct();
		Descriptor.Fields.Add({TEXT("weight"), TEXT("Weight")});
		Descriptor.Fields.Add({TEXT("bias"), TEXT("Bias")});
		Descriptor.Ports.Add({TEXT("value"), EADSKValueType::Float, EADSKPortDirection::In});
		Descriptor.ValidateConfig = [](const void* Config)
		{
			const FADSKScoreConfig& Score = *static_cast<const FADSKScoreConfig*>(Config);
			return FMath::IsFinite(Score.Weight) && FMath::IsFinite(Score.Bias);
		};
		Descriptor.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
		Descriptor.OnScore = [](FADSKNodeContext& Context)
		{
			const FADSKScoreConfig& Config = Context.GetConfig<FADSKScoreConfig>();
			float Value = 0.f;
			Context.ReadFloat(0, Value);
			return Config.Weight * Value + Config.Bias;
		};
		return Descriptor;
	}

	FADSKNodeDescriptor MakeSimpleParallel()
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = TEXT("SimpleParallel");
		Descriptor.Kind = EADSKNodeKind::Parallel;
		Descriptor.MinChildren = 2;
		Descriptor.MaxChildren = 2;
		return Descriptor;
	}

	FADSKNodeDescriptor MakeGuard()
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = TEXT("Guard");
		Descriptor.Kind = EADSKNodeKind::Guard;
		Descriptor.ConfigStruct = FADSKGuardConfig::StaticStruct();
		Descriptor.Fields.Add({TEXT("invert"), TEXT("bInvert")});
		Descriptor.MinChildren = 1;
		Descriptor.MaxChildren = 1;
		Descriptor.Ports.Add({TEXT("condition"), EADSKValueType::Bool, EADSKPortDirection::In});
		return Descriptor;
	}

	bool IsWaitTime(const void* Config)
	{
		const FADSKWaitConfig& Wait = *static_cast<const FADSKWaitConfig*>(Config);
		return FMath::IsFinite(Wait.Duration) && Wait.Duration >= 0.f && FMath::IsFinite(Wait.Deviation) && Wait.Deviation >= 0.f;
	}

	float DrawWaitTime(FADSKNodeContext& Context)
	{
		const FADSKWaitConfig& Config = Context.GetConfig<FADSKWaitConfig>();
		// Draw only when asked, so a fixed time never consumes the stream.
		return Config.Deviation > 0.f
			? FMath::Max(0.f, Config.Duration + Context.GetRandom().FRandRange(-Config.Deviation, Config.Deviation))
			: Config.Duration;
	}

	FADSKNodeDescriptor MakeWait()
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = TEXT("Wait");
		Descriptor.bNeverFails = true;
		Descriptor.ConfigStruct = FADSKWaitConfig::StaticStruct();
		Descriptor.MemoryStruct = FADSKWaitMemory::StaticStruct();
		Descriptor.Fields.Add({TEXT("duration"), TEXT("Duration")});
		Descriptor.Fields.Add({TEXT("deviation"), TEXT("Deviation")});
		Descriptor.ValidateConfig = &IsWaitTime;
		Descriptor.OnStart = [](FADSKNodeContext& Context)
		{
			FADSKWaitMemory& Memory = Context.GetMemory<FADSKWaitMemory>();
			Memory.Elapsed = 0.f;
			Memory.Target = DrawWaitTime(Context);
			return Memory.Target <= 0.f ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Running;
		};
		Descriptor.OnUpdate = [](FADSKNodeContext& Context, float Delta)
		{
			FADSKWaitMemory& Memory = Context.GetMemory<FADSKWaitMemory>();
			Memory.Elapsed += Delta;
			return Memory.Elapsed >= Memory.Target ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Running;
		};
		Descriptor.OnNextWake = [](FADSKNodeContext& Context)
		{
			const FADSKWaitMemory& Memory = Context.GetMemory<FADSKWaitMemory>();
			return FMath::Max(0.f, Memory.Target - Memory.Elapsed);
		};
		return Descriptor;
	}

	FADSKNodeDescriptor MakeRoll()
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = TEXT("Roll");
		Descriptor.bNeverFails = true;
		Descriptor.ConfigStruct = FADSKRollConfig::StaticStruct();
		Descriptor.Fields.Add({TEXT("count"), TEXT("Count")});
		Descriptor.Ports.Add({TEXT("value"), EADSKValueType::Int, EADSKPortDirection::Out});
		Descriptor.ValidateConfig = [](const void* Config) { return static_cast<const FADSKRollConfig*>(Config)->Count >= 1; };
		Descriptor.OnStart = [](FADSKNodeContext& Context)
		{
			const int32 Count = Context.GetConfig<FADSKRollConfig>().Count;
			return Context.WriteInt(0, Context.GetRandom().RandRange(0, Count - 1)) ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Failed;
		};
		return Descriptor;
	}

	FADSKNodeDescriptor MakeChance()
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = TEXT("Chance");
		Descriptor.ConfigStruct = FADSKChanceConfig::StaticStruct();
		Descriptor.Fields.Add({TEXT("probability"), TEXT("Probability")});
		Descriptor.ValidateConfig = [](const void* Config)
		{
			const float Probability = static_cast<const FADSKChanceConfig*>(Config)->Probability;
			return FMath::IsFinite(Probability) && Probability >= 0.f && Probability <= 1.f;
		};
		Descriptor.OnStart = [](FADSKNodeContext& Context)
		{
			return Context.GetRandom().FRand() < Context.GetConfig<FADSKChanceConfig>().Probability ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Failed;
		};
		return Descriptor;
	}

	FADSKNodeDescriptor MakeDecorator(const TCHAR* Name)
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Kind = EADSKNodeKind::Composite;
		Descriptor.MinChildren = 1;
		Descriptor.MaxChildren = 1;
		return Descriptor;
	}

	bool IsDuration(const void* Config)
	{
		const float Duration = static_cast<const FADSKDurationConfig*>(Config)->Duration;
		return FMath::IsFinite(Duration) && Duration >= 0.f;
	}

	/** Tries its children in an order drawn once per activation; succeeds on the first success. */
	FADSKNodeDescriptor MakeRandomSelector()
	{
		FADSKNodeDescriptor Descriptor = MakeDecorator(TEXT("RandomSelector"));
		Descriptor.MaxChildren = FADSKRandomSelectorMemory::MaxChildren;
		Descriptor.MemoryStruct = FADSKRandomSelectorMemory::StaticStruct();
		Descriptor.OnCompositeStart = [](FADSKNodeContext& Context)
		{
			FADSKRandomSelectorMemory& Memory = Context.GetMemory<FADSKRandomSelectorMemory>();
			const int32 Count = Context.GetChildCount();
			for (int32 Child = 0; Child < Count; ++Child) { Memory.Order[Child] = static_cast<uint8>(Child); }
			for (int32 Index = Count - 1; Index > 0; --Index) { Swap(Memory.Order[Index], Memory.Order[Context.GetRandom().RandRange(0, Index)]); }
			Memory.Tried = 0;
			return FADSKCompositeStep::Start(Memory.Order[0]);
		};
		Descriptor.OnChildFinished = [](FADSKNodeContext& Context, int32, EADSKNodeStatus Status)
		{
			FADSKRandomSelectorMemory& Memory = Context.GetMemory<FADSKRandomSelectorMemory>();
			if (Status == EADSKNodeStatus::Succeeded) { return FADSKCompositeStep::Succeed(); }
			return ++Memory.Tried < Context.GetChildCount() ? FADSKCompositeStep::Start(Memory.Order[Memory.Tried]) : FADSKCompositeStep::Fail();
		};
		return Descriptor;
	}

	/** Runs its child again after each success; fails with it; succeeds after `count` successes (0: never). */
	FADSKNodeDescriptor MakeRepeat()
	{
		FADSKNodeDescriptor Descriptor = MakeDecorator(TEXT("Repeat"));
		Descriptor.ConfigStruct = FADSKRepeatConfig::StaticStruct();
		Descriptor.MemoryStruct = FADSKRepeatMemory::StaticStruct();
		Descriptor.Fields.Add({TEXT("count"), TEXT("Count")});
		Descriptor.ValidateConfig = [](const void* Config) { return static_cast<const FADSKRepeatConfig*>(Config)->Count >= 0; };
		Descriptor.OnCompositeStart = [](FADSKNodeContext& Context)
		{
			Context.GetMemory<FADSKRepeatMemory>().Done = 0;
			return FADSKCompositeStep::Start(0);
		};
		Descriptor.OnChildFinished = [](FADSKNodeContext& Context, int32, EADSKNodeStatus Status)
		{
			if (Status == EADSKNodeStatus::Failed) { return FADSKCompositeStep::Fail(); }
			const int32 Count = Context.GetConfig<FADSKRepeatConfig>().Count;
			return Count > 0 && ++Context.GetMemory<FADSKRepeatMemory>().Done >= Count ? FADSKCompositeStep::Succeed() : FADSKCompositeStep::Start(0);
		};
		return Descriptor;
	}

	/** Runs its child again after each failure; succeeds with it; fails after `count` failed attempts (0: never). */
	FADSKNodeDescriptor MakeRetry()
	{
		FADSKNodeDescriptor Descriptor = MakeDecorator(TEXT("Retry"));
		Descriptor.ConfigStruct = FADSKRepeatConfig::StaticStruct();
		Descriptor.MemoryStruct = FADSKRepeatMemory::StaticStruct();
		Descriptor.Fields.Add({TEXT("count"), TEXT("Count")});
		Descriptor.ValidateConfig = [](const void* Config) { return static_cast<const FADSKRepeatConfig*>(Config)->Count >= 0; };
		Descriptor.OnCompositeStart = [](FADSKNodeContext& Context)
		{
			Context.GetMemory<FADSKRepeatMemory>().Done = 0;
			return FADSKCompositeStep::Start(0);
		};
		Descriptor.OnChildFinished = [](FADSKNodeContext& Context, int32, EADSKNodeStatus Status)
		{
			if (Status == EADSKNodeStatus::Succeeded) { return FADSKCompositeStep::Succeed(); }
			const int32 Count = Context.GetConfig<FADSKRepeatConfig>().Count;
			return Count > 0 && ++Context.GetMemory<FADSKRepeatMemory>().Done >= Count ? FADSKCompositeStep::Fail() : FADSKCompositeStep::Start(0);
		};
		return Descriptor;
	}

	/** Fails at once until `duration` seconds have passed since its child last ran to an end (cancelled included). */
	FADSKNodeDescriptor MakeCooldown()
	{
		FADSKNodeDescriptor Descriptor = MakeDecorator(TEXT("Cooldown"));
		Descriptor.ConfigStruct = FADSKDurationConfig::StaticStruct();
		Descriptor.MemoryStruct = FADSKCooldownMemory::StaticStruct();
		Descriptor.StateStruct = FADSKCooldownState::StaticStruct();
		Descriptor.Fields.Add({TEXT("duration"), TEXT("Duration")});
		Descriptor.ValidateConfig = &IsDuration;
		Descriptor.OnCompositeStart = [](FADSKNodeContext& Context)
		{
			const bool bReady = Context.GetTime() >= Context.GetState<FADSKCooldownState>().ReadyAt;
			Context.GetMemory<FADSKCooldownMemory>().bRan = bReady;
			return bReady ? FADSKCompositeStep::Start(0) : FADSKCompositeStep::Fail();
		};
		Descriptor.OnChildFinished = [](FADSKNodeContext&, int32, EADSKNodeStatus Status) { return FADSKCompositeStep::Finish(Status); };
		// As an option of a ReactiveSelector, becoming ready interrupts the option running after it.
		Descriptor.OnCheck = [](FADSKNodeContext& Context) { return Context.GetTime() >= Context.GetState<FADSKCooldownState>().ReadyAt; };
		// Ready stays ready, so only a cooldown still running has a time to be checked at.
		Descriptor.OnNextWake = [](FADSKNodeContext& Context)
		{
			const double Left = Context.GetState<FADSKCooldownState>().ReadyAt - Context.GetTime();
			return Left > 0.0 ? static_cast<float>(Left) : ADSK::NeverWake;
		};
		Descriptor.OnEnd = [](FADSKNodeContext& Context, EADSKEndReason)
		{
			if (Context.GetMemory<FADSKCooldownMemory>().bRan)
			{
				Context.GetState<FADSKCooldownState>().ReadyAt = Context.GetTime() + Context.GetConfig<FADSKDurationConfig>().Duration;
			}
		};
		return Descriptor;
	}

	/** Fails, cancelling its child, once the child has run `duration` (± `deviation`) seconds; otherwise ends with the child. */
	FADSKNodeDescriptor MakeTimeLimit()
	{
		FADSKNodeDescriptor Descriptor = MakeDecorator(TEXT("TimeLimit"));
		Descriptor.ConfigStruct = FADSKWaitConfig::StaticStruct();
		Descriptor.MemoryStruct = FADSKTimeLimitMemory::StaticStruct();
		Descriptor.Fields.Add({TEXT("duration"), TEXT("Duration")});
		Descriptor.Fields.Add({TEXT("deviation"), TEXT("Deviation")});
		Descriptor.ValidateConfig = &IsWaitTime;
		Descriptor.OnCompositeStart = [](FADSKNodeContext& Context)
		{
			FADSKTimeLimitMemory& Memory = Context.GetMemory<FADSKTimeLimitMemory>();
			Memory.Elapsed = 0.f;
			Memory.Limit = DrawWaitTime(Context);
			return FADSKCompositeStep::Start(0);
		};
		Descriptor.OnUpdate = [](FADSKNodeContext& Context, float Delta)
		{
			FADSKTimeLimitMemory& Memory = Context.GetMemory<FADSKTimeLimitMemory>();
			Memory.Elapsed += Delta;
			return Memory.Elapsed >= Memory.Limit ? EADSKNodeStatus::Failed : EADSKNodeStatus::Running;
		};
		Descriptor.OnNextWake = [](FADSKNodeContext& Context)
		{
			const FADSKTimeLimitMemory& Memory = Context.GetMemory<FADSKTimeLimitMemory>();
			return FMath::Max(0.f, Memory.Limit - Memory.Elapsed);
		};
		Descriptor.OnChildFinished = [](FADSKNodeContext&, int32, EADSKNodeStatus Status) { return FADSKCompositeStep::Finish(Status); };
		return Descriptor;
	}

	FADSKNodeDescriptor MakeForceSuccess()
	{
		FADSKNodeDescriptor Descriptor = MakeDecorator(TEXT("ForceSuccess"));
		Descriptor.bNeverFails = true;
		Descriptor.OnCompositeStart = [](FADSKNodeContext&) { return FADSKCompositeStep::Start(0); };
		Descriptor.OnChildFinished = [](FADSKNodeContext&, int32, EADSKNodeStatus) { return FADSKCompositeStep::Succeed(); };
		return Descriptor;
	}

	FADSKNodeDescriptor MakeEmit()
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = TEXT("Emit");
		Descriptor.Kind = EADSKNodeKind::Emit;
		return Descriptor;
	}

	/** Every child at once, each in its own lane; intrinsic, the executor steps it. */
	FADSKNodeDescriptor MakeParallel()
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = TEXT("Parallel");
		Descriptor.Kind = EADSKNodeKind::Concurrent;
		Descriptor.MinChildren = 2;
		Descriptor.MaxChildren = ADSK::MaxParallelChildren;
		Descriptor.ConfigStruct = FADSKParallelConfig::StaticStruct();
		Descriptor.MemoryStruct = FADSKParallelMemory::StaticStruct();
		Descriptor.Fields.Add({TEXT("success"), TEXT("Success")});
		Descriptor.Fields.Add({TEXT("failure"), TEXT("Failure")});
		Descriptor.ValidateConfig = [](const void* Value)
		{
			const FADSKParallelConfig& Config = *static_cast<const FADSKParallelConfig*>(Value);
			return ADSK::ParallelThreshold(Config.Success, ADSK::MaxParallelChildren) > 0 && ADSK::ParallelThreshold(Config.Failure, ADSK::MaxParallelChildren) > 0;
		};
		return Descriptor;
	}

	FADSKNodeDescriptor MakeSet()
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = TEXT("Set");
		Descriptor.Kind = EADSKNodeKind::Set;
		return Descriptor;
	}
}

using namespace ADSKBuiltinNodesPrivate;

int32 ADSK::ParallelThreshold(const FString& Policy, int32 Children)
{
	if (Policy.Equals(TEXT("all"), ESearchCase::CaseSensitive)) { return Children; }
	if (Policy.Equals(TEXT("any"), ESearchCase::CaseSensitive)) { return Children > 0 ? 1 : 0; }
	int32 Count = 0;
	return Policy.IsNumeric() && LexTryParseString(Count, *Policy) && Count >= 1 ? FMath::Min(Count, Children) : 0;
}

namespace ADSKBuiltinNodesPrivate
{
	const TCHAR* GuidanceFor(const FString& Name)
	{
		static const TMap<FString, const TCHAR*> Guidance = {
			{TEXT("Sequence"), TEXT("Steps that must all happen, in order: face, approach, strike.\n")
				TEXT("The first failing step fails the whole Sequence and is not retried; wrap a step whose failure does not matter in `ForceSuccess`.\n")
				TEXT("For \"try this, else that\" use `Selector`.")},
			{TEXT("Selector"), TEXT("Fallbacks: the preferred option first, cheaper ones after.\n")
				TEXT("It does not look back: an earlier option that becomes possible again is not retried until the selector restarts. Use `ReactiveSelector` with `Guard` options when it must be.\n")
				TEXT("Choosing between modes (fight or flee) belongs in the state machine, not in a tree (concept.md: state is the mode, tree is the procedure).")},
			{TEXT("Guard"), TEXT("Keep a child running only while a fact holds: target visible, in range, not stunned.\n")
				TEXT("The condition reads a slot the host publishes; never poll the world from a leaf to do this.\n")
				TEXT("It takes one child; put several under a `Sequence`.")},
			{TEXT("Wait"), TEXT("A pause in tick time; `deviation` adds variety from the seeded stream, so a replay matches.\n")
				TEXT("It never fails, so a tree of `Wait`s alone cannot end with tree.failure.\n")
				TEXT("Waiting for something to happen is a `Guard` or an event, not a `Wait` in a loop.")},
			{TEXT("Emit"), TEXT("Hand a decision from a tree to the state machine: the event is queued and dispatches on the next tick, never the same one.\n")
				TEXT("It cannot raise tree.success, tree.failure or a change event.\n")
				TEXT("It fails only when the event queue is full.")},
			{TEXT("Set"), TEXT("Keep a counter, a flag or a mode in a Local: `<Set id=\"mark\" to=\"Alerted\" value=\"true\"/>`, or copy a slot with `from`.\n")
				TEXT("A `change` event on that Local fires on the next tick, so a Set can hand a decision to the state machine like an `Emit`.\n")
				TEXT("Choosing a mode from scores belongs to `select`, not to a chain of Sets.")},
			{TEXT("Roll"), TEXT("Draw a number for a later comparison (`Guard is=`, `On ... below=`); the draw is seeded, so a replay matches.\n")
				TEXT("To pick one of several subtrees, `RandomSelector` or `UtilitySelector` says it directly.")},
			{TEXT("Chance"), TEXT("A seeded coin flip inside a `Sequence` or `Selector`: \"sometimes taunt first\".\n")
				TEXT("At `probability` 0 or 1 it is constant; remove it instead.")},
			{TEXT("SimpleParallel"), TEXT("Do one thing while looping another beside it: move while re-aiming. The first (main) child decides the result.\n")
				TEXT("The second (background) child restarts each time it ends and is cancelled with the main one; its result never counts.")},
			{TEXT("Parallel"), TEXT("Several things that must all happen, or the first of several that works: move, reload and wait at least 1.5 s (`success=\"all\"`), or whichever of two routes arrives first (`success=\"any\"`).\n")
				TEXT("A child that ends stays ended; nothing loops. To loop something beside a main task, `SimpleParallel` says it.\n")
				TEXT("By default one failure fails it and cancels the rest; `failure=\"all\"` keeps going until every child has failed.")},
			{TEXT("RandomSelector"), TEXT("Variety between equivalent options: idle fidgets, which patrol point next.\n")
				TEXT("For options that are not equivalent, order them in a `Selector` or score them in a `UtilitySelector`.")},
			{TEXT("Repeat"), TEXT("Run a child a fixed number of times, or forever with `count` 0, until it fails.\n")
				TEXT("A forever `Repeat` never ends its tree: leave the state through a transition.")},
			{TEXT("Retry"), TEXT("Try a step that can fail for reasons that pass (a path blocked for a moment, a query with no result yet) up to `count` times.\n")
				TEXT("A child that fails at once is retried in the same tick until the node-start budget defers it: put a `Wait` in a `Sequence` under it to space the attempts.\n")
				TEXT("With `count` 0 it never fails; leave the state through a transition, or bound it with a `TimeLimit`.")},
			{TEXT("Cooldown"), TEXT("Rate-limit an action (a special attack at most every 8 s): while cooling down it fails at once, so a `Selector` falls through to the next option.\n")
				TEXT("As an option of a `ReactiveSelector` it interrupts the later option running when it becomes ready: \"attack the moment the cooldown is over\".\n")
				TEXT("The timer lives in the node's state and is forgotten when the instance restarts.")},
			{TEXT("TimeLimit"), TEXT("Bound anything that waits on the world (a move, a query, an animation callback), so a missed callback fails the node instead of holding the state.\n")
				TEXT("When time runs out it cancels the child and fails; handle tree.failure.\n")
				TEXT("To do something that never ends on its own for a while (fire for 1-3 s), wrap it with `deviation` in a `ForceSuccess`.")},
			{TEXT("ForceSuccess"), TEXT("Make an optional step harmless inside a `Sequence`.\n")
				TEXT("It hides the failure: never wrap the step whose failure should change the mode.")},
			{TEXT("ReactiveSelector"), TEXT("Priorities that must interrupt a running lower option (evade interrupts attack): make each earlier option a `Guard`.\n")
				TEXT("Re-checked options: `Guard`s (their condition), `Cooldown`s (ready again) and project nodes with an `OnCheck`; an earlier option of any other kind is not.\n")
				TEXT("The conditions are re-checked every tick; keep them to slot comparisons.")},
			{TEXT("UtilitySelector"), TEXT("Pick the best of several options by score: one `Score` scope per option, fed by Inputs the host publishes.\n")
				TEXT("With `reevaluate` it switches only when another option wins by more than `margin`, so it does not flap.\n")
				TEXT("An option scoring 0 or less is skipped; when every one is, it fails.")},
			{TEXT("Score"), TEXT("Only means something as an option of a `UtilitySelector`; elsewhere it just runs its child.\n")
				TEXT("The score is a line, `weight * in-value + bias`: have the host publish an input already in a useful range.")},
		};
		const TCHAR* const* Found = Guidance.Find(Name);
		return Found ? *Found : TEXT("");
	}

	FADSKNodeDescriptor Described(FADSKNodeDescriptor Descriptor, const TCHAR* Description)
	{
		Descriptor.Description = Description;
		Descriptor.Guidance = GuidanceFor(Descriptor.Name);
		return Descriptor;
	}
}

void FADSKBuiltinNodes::RegisterAll(FADSKNodeRegistry& Registry)
{
	// Every built-in with callbacks touches only its context, so all of them may tick on a worker.
	const auto Safe = [](FADSKNodeDescriptor Descriptor) { Descriptor.bThreadSafe = true; return Descriptor; };
	const EADSKRegisterResult Results[] = {
		Registry.Register(Described(MakeSequence(), TEXT("Runs its children in order. Fails as soon as one fails; succeeds when the last one succeeds."))),
		Registry.Register(Described(MakeSelector(), TEXT("Tries its children in order. Succeeds as soon as one succeeds; fails when the last one fails."))),
		Registry.Register(Described(MakeGuard(), TEXT("Runs its one child while `in-condition` passes (`is`, `below`, `above`, `invert`), re-checked every tick. Fails, cancelling the child, when the condition stops passing; otherwise ends with the child."))),
		Registry.Register(Safe(Described(MakeWait(), TEXT("Waits `duration` seconds of tick time, drawn once per start from `duration +/- deviation` (floored at 0). Always succeeds.")))),
		Registry.Register(Described(MakeEmit(), TEXT("Queues `event` (with its `<Param>` values, read when it runs) for the next tick and succeeds. Fails when the event queue is full."))),
		Registry.Register(Safe(Described(MakeRoll(), TEXT("Writes a uniform Int in `[0, count)` from the instance's seeded stream to `out-value`. Always succeeds.")))),
		Registry.Register(Safe(Described(MakeChance(), TEXT("Succeeds with `probability`, fails otherwise, drawn from the instance's seeded stream.")))),
		Registry.Register(Described(MakeSimpleParallel(), TEXT("Runs its first (main) child and, beside it in its own lane, loops the second (background) child. Ends with the main child's result, cancelling the background."))),
		Registry.Register(Safe(Described(MakeRandomSelector(), TEXT("A Selector whose order is shuffled once per start from the instance's seeded stream. Succeeds at the first success; fails when every child failed.")))),
		Registry.Register(Safe(Described(MakeRepeat(), TEXT("Runs its child again after each success. Fails when the child fails; succeeds after `count` successes (0: never)."))))
		, Registry.Register(Safe(Described(MakeCooldown(), TEXT("Fails at once until `duration` seconds have passed since its child last ended (cancelled included); otherwise runs the child and ends with it. A restart forgets the cooldown.")))),
		Registry.Register(Safe(Described(MakeTimeLimit(), TEXT("Runs its child; fails, cancelling it, once it has run `duration` seconds, drawn once per start from `duration +/- deviation` (floored at 0). Otherwise ends with the child.")))),
		Registry.Register(Safe(Described(MakeForceSuccess(), TEXT("Runs its child and succeeds whatever the child does.")))),
		Registry.Register(Described(MakeReactiveSelector(), TEXT("A Selector that, every tick, re-checks each earlier option that can be checked (a Guard's condition, a Cooldown being ready, a node with OnCheck); one that turns true cancels the running option and starts in its place. Succeeds at the first success; fails when every option failed."))),
		Registry.Register(Described(MakeUtilitySelector(), TEXT("Tries its options highest score first (`Score` scopes; an option scoring 0 or less is skipped, none left fails). With `reevaluate` seconds it scores again and switches when another option wins by more than `margin`."))),
		Registry.Register(Safe(Described(MakeScore(), TEXT("Scores its one child for a UtilitySelector as `weight * in-value + bias`. Otherwise transparent: runs the child and ends with it.")))),
		Registry.Register(Described(MakeParallel(), TEXT("Runs every child at once, each in its own lane (at most 8). Succeeds once `success` children succeeded (`all`, `any` or a count); fails once `failure` failed, or success can no longer be reached. Then cancels the children still running."))),
		Registry.Register(Described(MakeSet(), TEXT("Writes `value` (a literal of the slot's type: an enum value name, `none` for an Object) or the slot named by `from` to the Local in `to`, and succeeds."))),
		Registry.Register(Safe(Described(MakeRetry(), TEXT("Runs its child again after each failure. Succeeds when the child succeeds; fails after `count` failed attempts (0: never)."))))};
	for (const EADSKRegisterResult Result : Results)
	{
		checkf(Result == EADSKRegisterResult::Accepted, TEXT("built-in node registration failed: %s"), LexToString(Result));
	}
}
