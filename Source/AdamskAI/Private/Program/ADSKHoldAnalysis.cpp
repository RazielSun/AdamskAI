// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Program/ADSKHoldAnalysis.h"

#include "Nodes/ADSKBuiltinNodes.h"

#include "Program/ADSKDefinition.h"

namespace ADSKHoldAnalysisPrivate
{
	struct FOutcomes
	{
		bool bSuccess = false;
		bool bFailure = false;
	};

	FOutcomes Outcomes(const FADSKDefinition& Definition, int32 NodeIndex)
	{
		const FADSKNodeDefinition& Node = Definition.GetNodes()[NodeIndex];
		const auto Child = [&Definition, &Node](int32 Index) { return Outcomes(Definition, Definition.GetNodeChildren()[Node.FirstChild + Index]); };
		switch (Node.Descriptor->Kind)
		{
		case EADSKNodeKind::Leaf:
			return {true, !Node.Descriptor->bNeverFails};
		// Only a full event queue fails it: not worth a note on every Emit.
		case EADSKNodeKind::Emit:
		case EADSKNodeKind::Set:
			return {true, false};
		case EADSKNodeKind::Sequence:
		{
			FOutcomes Result{true, false};
			for (int32 Index = 0; Index < Node.ChildCount; ++Index)
			{
				const FOutcomes Each = Child(Index);
				Result.bSuccess &= Each.bSuccess;
				Result.bFailure |= Each.bFailure;
			}
			return Result;
		}
		case EADSKNodeKind::Selector:
		{
			FOutcomes Result{false, true};
			for (int32 Index = 0; Index < Node.ChildCount; ++Index)
			{
				const FOutcomes Each = Child(Index);
				Result.bSuccess |= Each.bSuccess;
				Result.bFailure &= Each.bFailure;
			}
			return Result;
		}
		// A guard turns false and a scope's own start or update fails, whatever the child does.
		case EADSKNodeKind::Guard:
		case EADSKNodeKind::Scope:
			return {Child(0).bSuccess, true};
		case EADSKNodeKind::Parallel:
			return Child(0);
		// Enough children able to succeed (or fail) for the policy; failure also when success may become unreachable.
		case EADSKNodeKind::Concurrent:
		{
			const FADSKParallelConfig& Config = *Node.Config.GetPtr<FADSKParallelConfig>();
			const int32 Success = ADSK::ParallelThreshold(Config.Success, Node.ChildCount);
			const int32 Failure = ADSK::ParallelThreshold(Config.Failure, Node.ChildCount);
			int32 CanSucceed = 0;
			int32 CanFail = 0;
			for (int32 Index = 0; Index < Node.ChildCount; ++Index)
			{
				const FOutcomes Each = Child(Index);
				CanSucceed += Each.bSuccess ? 1 : 0;
				CanFail += Each.bFailure ? 1 : 0;
			}
			return {CanSucceed >= Success, CanFail >= Failure || CanFail > Node.ChildCount - Success};
		}
		case EADSKNodeKind::Composite:
			return {true, !Node.Descriptor->bNeverFails};
		case EADSKNodeKind::Sensor:
			break;
		}
		return {true, true};
	}

	/** 0: no handler on the path, 1: only conditional ones, 2: an unconditional one. */
	int32 Coverage(const FADSKDefinition& Definition, int32 State, int32 Event);

	/**
	 * The tree succeeds only by an Emit whose event an unconditional handler on the state path leaves the state for:
	 * that event is queued before the completion, so the completion is stale when it dispatches.
	 */
	bool SucceedsThroughHandledEmit(const FADSKDefinition& Definition, int32 State, int32 NodeIndex)
	{
		const FADSKNodeDefinition& Node = Definition.GetNodes()[NodeIndex];
		switch (Node.Descriptor->Kind)
		{
		case EADSKNodeKind::Emit:
			return Coverage(Definition, State, Node.EmitEvent) == 2;
		case EADSKNodeKind::Sequence:
			return SucceedsThroughHandledEmit(Definition, State, Definition.GetNodeChildren()[Node.FirstChild + Node.ChildCount - 1]);
		case EADSKNodeKind::Guard:
		case EADSKNodeKind::Scope:
		case EADSKNodeKind::Parallel:
			return SucceedsThroughHandledEmit(Definition, State, Definition.GetNodeChildren()[Node.FirstChild]);
		default:
			return false;
		}
	}

	int32 Coverage(const FADSKDefinition& Definition, int32 State, int32 Event)
	{
		int32 Best = 0;
		for (int32 Current = State; Current != INDEX_NONE; Current = Definition.GetStates()[Current].Parent)
		{
			const FADSKStateDefinition& Each = Definition.GetStates()[Current];
			for (int32 Index = 0; Index < Each.TransitionCount; ++Index)
			{
				const FADSKTransitionDefinition& Transition = Definition.GetTransitions()[Each.FirstTransition + Index];
				if (Transition.Event == Event)
				{
					// Certain and leaving: an internal transition keeps the state, so its tree completion stays live.
					if (Transition.IsCertain() && (Event < 2 || Transition.Target != INDEX_NONE))
					{
						return 2;
					}
					Best = 1;
				}
			}
		}
		return Best;
	}
}

using namespace ADSKHoldAnalysisPrivate;

TArray<FADSKHoldRisk> ADSK::FindHoldRisks(const FADSKDefinition& Definition)
{
	TArray<FADSKHoldRisk> Risks;
	for (int32 State = 0; State < Definition.GetStates().Num(); ++State)
	{
		const FADSKStateDefinition& Each = Definition.GetStates()[State];
		if (Each.ChildCount != 0 || Each.RootNode == INDEX_NONE)
		{
			continue;
		}
		const FOutcomes Possible = Outcomes(Definition, Each.RootNode);
		for (const bool bSuccess : {true, false})
		{
			if (!(bSuccess ? Possible.bSuccess : Possible.bFailure) || (bSuccess && SucceedsThroughHandledEmit(Definition, State, Each.RootNode)))
			{
				continue;
			}
			const int32 Covered = Coverage(Definition, State, bSuccess ? TreeSuccessEvent : TreeFailureEvent);
			if (Covered < 2)
			{
				Risks.Add({State, Each.Line, bSuccess, Covered == 1});
			}
		}
	}
	return Risks;
}

FString ADSK::DescribeHoldRisk(const FADSKDefinition& Definition, const FADSKHoldRisk& Risk)
{
	return FString::Printf(TEXT("state '%s': %s %s; the brain holds there when it does"), *Definition.GetStates()[Risk.State].Id,
		Risk.bSuccess ? TEXT("tree.success") : TEXT("tree.failure"),
		Risk.bConditionalOnly ? TEXT("has only conditional handlers on its state path") : TEXT("has no handler on its state path"));
}
