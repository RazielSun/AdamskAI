// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Runtime/ADSKInstanceInternal.h"

#include "Nodes/ADSKBuiltinNodes.h"
#include "Algo/StableSort.h"

FADSKProcedure::FGuardResult FADSKProcedure::EvaluateGuard(const FADSKActiveNode& Active)
{
	// Reads the condition slot directly: a full context per re-check would be most of a reactive selector's cost.
	FGuardResult Result;
	const FADSKNodeDefinition& Node = NodeDef(Active);
	const bool bHolds = ADSK::EvaluateCondition(Node.Condition, Owner.Slots)
		&& ADSK::EvaluateTerms(Owner.Definition->GetConditionTerms().Slice(Node.FirstTerm, Node.TermCount), Owner.Slots);
	// With `<When>`/`<Any>` terms no single slot tells the outcome, so the recorded value is the outcome itself.
	Result.Raw = Node.TermCount == 0 ? Owner.Slots[Node.Condition.Slot] : FADSKValue::MakeBool(bHolds);
	Result.bPass = bHolds != Node.Config.Get<FADSKGuardConfig>().bInvert;
	return Result;
}

bool FADSKProcedure::OptionPasses(const FADSKNodeDefinition& Selector, int32 Child)
{
	FADSKActiveNode Option;
	Option.Node = Owner.Definition->GetNodeChildren()[Selector.FirstChild + Child];
	const FADSKNodeDescriptor& Descriptor = *Owner.Definition->GetNodes()[Option.Node].Descriptor;
	if (Descriptor.Kind == EADSKNodeKind::Guard)
	{
		return EvaluateGuard(Option).bPass;
	}
	if (!Descriptor.OnCheck)
	{
		return false;
	}
	FADSKNodeContext Context = MakeContext(Option, 0.f);
	FADSKCallbackScope Scope(Owner);
	return Descriptor.OnCheck(Context);
}

bool FADSKProcedure::ReactToEarlierOptions(int32 Index)
{
	FADSKActiveNode& Active = Lane->Stack[Index];
	const FADSKNodeDefinition& Node = NodeDef(Active);
	for (int32 Child = 0; Child < Active.ChildCursor; ++Child)
	{
		const uint64 Bit = uint64(1) << Child;
		const bool bPasses = OptionPasses(Node, Child);
		uint64& Seen = SelectorMemory<FADSKReactiveSelectorMemory>(Active).Seen;
		const bool bRose = bPasses && !(Seen & Bit);
		Seen = bPasses ? Seen | Bit : Seen & ~Bit;
		if (bRose)
		{
			CancelFrom(Index + 1);
			FADSKActiveNode& Selector = Lane->Stack[Index];
			Selector.ChildCursor = Child;
			StartNode(Owner.Definition->GetNodeChildren()[Node.FirstChild + Child]);
			return true;
		}
	}
	return false;
}

FADSKProcedure::FOptionScores FADSKProcedure::ScoreOptions(const FADSKNodeDefinition& Selector)
{
	FOptionScores Scores;
	for (int32 Child = 0; Child < Selector.ChildCount; ++Child)
	{
		FADSKActiveNode Option;
		Option.Node = Owner.Definition->GetNodeChildren()[Selector.FirstChild + Child];
		const FADSKNodeDescriptor& Descriptor = *NodeDef(Option).Descriptor;
		float Score = 0.f;
		if (Descriptor.OnScore)
		{
			FADSKNodeContext Context = MakeContext(Option, 0.f);
			FADSKCallbackScope Scope(Owner);
			Score = Descriptor.OnScore(Context);
		}
		Scores.Add(FMath::IsFinite(Score) ? Score : 0.f);
	}
	return Scores;
}

void FADSKProcedure::OrderByScore(TConstArrayView<float> Scores, FADSKUtilitySelectorMemory& Out)
{
	Out.Count = 0;
	Out.Cursor = 0;
	for (int32 Child = 0; Child < Scores.Num(); ++Child)
	{
		if (Scores[Child] > 0.f) Out.Order[Out.Count++] = static_cast<uint8>(Child);
	}
	Algo::StableSortBy(MakeArrayView(Out.Order, Out.Count), [&Scores](uint8 Child) { return -Scores[Child]; });
}

void FADSKProcedure::StartUtility(FADSKActiveNode& Active, const FADSKNodeDefinition& Node)
{
	FADSKUtilitySelectorMemory& Utility = SelectorMemory<FADSKUtilitySelectorMemory>(Active);
	OrderByScore(ScoreOptions(Node), Utility);
	Utility.LastScored = Owner.Clock;
	if (Owner.bStopRequested)
	{
		return;
	}
	if (Utility.Count == 0)
	{
		FinishTop(EADSKNodeStatus::Failed);
		return;
	}
	Active.ChildCursor = Utility.Order[0];
	StartNode(Owner.Definition->GetNodeChildren()[Node.FirstChild + Active.ChildCursor]);
}

bool FADSKProcedure::RescoreUtility(int32 Index)
{
	FADSKActiveNode& Active = Lane->Stack[Index];
	const FADSKNodeDefinition& Node = NodeDef(Active);
	const FADSKUtilityConfig& Config = *Node.Config.GetPtr<FADSKUtilityConfig>();
	FADSKUtilitySelectorMemory& Utility = SelectorMemory<FADSKUtilitySelectorMemory>(Active);
	if (Config.Reevaluate <= 0.f || Owner.Clock - Utility.LastScored + KINDA_SMALL_NUMBER < Config.Reevaluate)
	{
		return false;
	}
	Utility.LastScored = Owner.Clock;
	const FOptionScores Scores = ScoreOptions(Node);
	FADSKUtilitySelectorMemory Rescored;
	OrderByScore(Scores, Rescored);
	if (Rescored.Count == 0 || Rescored.Order[0] == Active.ChildCursor || Scores[Rescored.Order[0]] <= Scores[Active.ChildCursor] + Config.Margin)
	{
		return false;
	}
	Rescored.LastScored = Utility.LastScored;
	// The memory lives in the instance's arena, not on the stack, so cancelling the running option leaves it in place.
	CancelFrom(Index + 1);
	Utility = Rescored;
	FADSKActiveNode& Selector = Lane->Stack[Index];
	Selector.ChildCursor = Utility.Order[0];
	StartNode(Owner.Definition->GetNodeChildren()[Node.FirstChild + Selector.ChildCursor]);
	return true;
}
