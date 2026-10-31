// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Compiler/ADSKCompilerInternal.h"

using namespace ADSKCompilerInternal;

namespace ADSKCompilerValidateStatesPrivate
{
	bool IsNamed(const FADSKXmlElement& Element, const TCHAR* Name)
	{
		return Element.Name.Equals(Name, ESearchCase::CaseSensitive);
	}

	void ValidateBind(FCompileContext& Context, const FADSKXmlElement& Element, FTransition& Transition)
	{
		RejectChildren(Context, Element);
		RejectUnknownAttributes(Context, Element, {TEXT("param"), TEXT("to")});
		const FString* Param = RequireAttribute(Context, Element, TEXT("param"));
		const FString* To = RequireAttribute(Context, Element, TEXT("to"));
		if (!Param || !To || Transition.Event == INDEX_NONE)
		{
			return;
		}
		const TArray<FParam>& Params = Context.Events[Transition.Event].Params;
		const int32 ParamIndex = Params.IndexOfByPredicate([Param](const FParam& P) { return P.Name.Equals(*Param, ESearchCase::CaseSensitive); });
		const int32 SlotIndex = Context.FindSlot(*To);
		if (ParamIndex == INDEX_NONE || SlotIndex == INDEX_NONE || Context.Slots[SlotIndex].bInput)
		{
			Context.Error(EADSKDiagnosticCode::UnresolvedReference, Element.Line, TEXT("bind needs an event parameter and a Local"));
		}
		else if (Params[ParamIndex].Type != Context.Slots[SlotIndex].Type || Params[ParamIndex].Enum != Context.Slots[SlotIndex].Enum)
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("bind parameter and Local types differ"));
		}
		else
		{
			Transition.Binds.Add({ParamIndex, SlotIndex});
		}
	}

	/** `chance="p"`, p in (0, 1], or `chance="Slot"`, a Float slot; Chance stays 1 when absent or rejected. */
	void ValidateChance(FCompileContext& Context, const FADSKXmlElement& Element, FTransition& Transition)
	{
		const FString* Text = Element.FindAttribute(TEXT("chance"));
		if (!Text)
		{
			return;
		}
		float Chance = 0.f;
		if (!ADSK::TryParseFloat(*Text, Chance))
		{
			const int32 Slot = Context.FindSlot(*Text);
			if (Slot == INDEX_NONE)
			{
				Context.Error(EADSKDiagnosticCode::UnresolvedReference, Element.Line,
					*FString::Printf(TEXT("chance is no number and names no Input or Local: '%s'%s"), **Text, *DidYouMean(*Text, Context.SlotNames())));
			}
			else if (Context.Slots[Slot].Type != EADSKValueType::Float)
			{
				Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("chance needs a number or a Float slot"));
			}
			else
			{
				Transition.ChanceSlot = Slot;
			}
		}
		else if (!(Chance > 0.f && Chance <= 1.f))
		{
			Context.Error(EADSKDiagnosticCode::OutOfRange, Element.Line, TEXT("chance must be above 0 and at most 1"));
		}
		else
		{
			Transition.Chance = Chance;
		}
	}

	void ValidateOn(FCompileContext& Context, const FADSKXmlElement& Element, int32 StateIndex)
	{
		RejectUnknownAttributes(Context, Element, {TEXT("event"), TEXT("goto"), TEXT("if"), TEXT("unless"), TEXT("is"), TEXT("below"), TEXT("above"), TEXT("chance")});		FTransition Transition; Transition.Line = Element.Line;
		if (const FString* Event = RequireAttribute(Context, Element, TEXT("event")))
		{
			Transition.Event = Context.FindEvent(*Event);
			if (Transition.Event == INDEX_NONE)
			{
				Context.Error(EADSKDiagnosticCode::UnresolvedReference, Element.Line,
					*FString::Printf(TEXT("unknown event '%s'%s"), **Event, *DidYouMean(*Event, Context.EventNames())));
			}
		}
		if (const FString* Goto = Element.FindAttribute(TEXT("goto")))
		{
			Transition.Goto = *Goto;
			Transition.bHasGoto = true;
		}
		bool bConditionSyntaxOk = true;
		ParseConditionAttributes(Context, Element, FString(), Transition.Condition, Transition.bHasCondition, bConditionSyntaxOk);
		ValidateChance(Context, Element, Transition);
		for (const int32 Child : Element.Children)
		{
			const FADSKXmlElement& ChildElement = Context.Element(Child);
			if (IsNamed(ChildElement, TEXT("Bind")))
			{
				ValidateBind(Context, ChildElement, Transition);
			}
			else if (IsConditionElement(ChildElement))
			{
				ValidateConditionElement(Context, ChildElement, FString(), Transition.Terms, bConditionSyntaxOk);
			}
			else
			{
				Context.Error(EADSKDiagnosticCode::UnknownElement, ChildElement.Line, TEXT("unexpected element"));
			}
		}
		// An earlier handler with the same condition hides this one unless it may lose its draw.
		const bool bDuplicate = bConditionSyntaxOk && Transition.Event != INDEX_NONE && Context.States[StateIndex].Transitions.ContainsByPredicate([&Transition](const FTransition& T)
		{
			return T.Event == Transition.Event && T.bHasCondition == Transition.bHasCondition && T.Condition == Transition.Condition
				&& T.Terms == Transition.Terms && T.Chance >= 1.f && T.ChanceSlot == INDEX_NONE;
		});
		if (bDuplicate)
		{
			Context.Error(EADSKDiagnosticCode::DuplicateTransition, Element.Line, TEXT("state already handles this event"));
		}
		else
		{
			if (bConditionSyntaxOk && Transition.Event != INDEX_NONE && Context.States[StateIndex].Transitions.ContainsByPredicate([&Transition](const FTransition& T) { return T.Event == Transition.Event && T.IsCertain(); })) Context.Report(EADSKSeverity::Warning, EADSKDiagnosticCode::UnreachableTransition, Element.Line, FString(), TEXT("transition follows unconditional handler"));
			Context.States[StateIndex].Transitions.Add(MoveTemp(Transition));
		}
	}

	bool HasStateChild(const FCompileContext& Context, const FADSKXmlElement& Element)
	{
		return Element.Children.ContainsByPredicate([&Context](int32 Child) { return IsNamed(Context.Element(Child), TEXT("State")); });
	}

	/** Reports the depth and count limits before the state is created; true when it may be added. */
	bool CheckStateLimits(FCompileContext& Context, const FADSKXmlElement& Element, int32 Depth)
	{
		if (Depth > FADSKCompiler::MaxStateDepth)
		{
			Context.Error(EADSKDiagnosticCode::LimitExceeded, Element.Line, TEXT("state depth exceeds 8"));
			return false;
		}
		if (Context.States.Num() >= FADSKCompiler::MaxStates)
		{
			if (!Context.bStateLimitReported)
			{
				Context.Error(EADSKDiagnosticCode::LimitExceeded, Element.Line, TEXT("more than 64 states"));
			}
			Context.bStateLimitReported = true;
			return false;
		}
		return true;
	}

	/** A dotted name: segments of letters, digits and `_`, none empty and none starting with a digit. */
	bool IsTagName(const FString& Tag)
	{
		TArray<FString> Segments;
		Tag.ParseIntoArray(Segments, TEXT("."), false);
		if (Segments.IsEmpty()) return false;
		for (const FString& Segment : Segments)
		{
			if (Segment.IsEmpty() || FChar::IsDigit(Segment[0])) return false;
			for (const TCHAR Character : Segment)
			{
				if (!FChar::IsAlnum(Character) && Character != TEXT('_')) return false;
			}
		}
		return true;
	}

	/** `tags="A.B, C"`: a comma-separated list, spaces around the commas allowed. */
	void ParseStateTags(FCompileContext& Context, const FADSKXmlElement& Element, FState& State)
	{
		const FString* Tags = Element.FindAttribute(TEXT("tags"));
		if (!Tags) return;
		TArray<FString> Parts;
		Tags->ParseIntoArray(Parts, TEXT(","), false);
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
			if (!IsTagName(Part))
			{
				Context.Error(EADSKDiagnosticCode::OutOfRange, Element.Line, *FString::Printf(TEXT("'%s' is not a tag name (dotted segments of letters, digits and _)"), *Part));
				continue;
			}
			const FName Tag(*Part);
			if (State.Tags.Contains(Tag))
			{
				Context.Error(EADSKDiagnosticCode::DuplicateId, Element.Line, *FString::Printf(TEXT("tag '%s' appears twice on this state"), *Part));
				continue;
			}
			State.Tags.Add(Tag);
		}
	}

	/** `select` on a state with child states; `score`, `weight` and `bias` on a child of one. */
	void ParseSelection(FCompileContext& Context, const FADSKXmlElement& Element, FState& State)
	{
		// On a leaf, select is an unknown attribute.
		const FString* Select = State.bCompound ? Element.FindAttribute(TEXT("select")) : nullptr;
		if (Select)
		{
			if (Select->Equals(TEXT("best"), ESearchCase::CaseSensitive))
			{
				State.Select = EADSKStateSelect::Best;
			}
			else if (Select->Equals(TEXT("weighted"), ESearchCase::CaseSensitive))
			{
				State.Select = EADSKStateSelect::Weighted;
			}
			else
			{
				Context.Error(EADSKDiagnosticCode::UnknownEnumValue, Element.Line,
					*FString::Printf(TEXT("select is best or weighted, not '%s'"), **Select));
				// Its children's scores are still checked, not reported as misplaced.
				State.Select = EADSKStateSelect::Best;
			}
		}
		const FString* Score = Element.FindAttribute(TEXT("score"));
		const FString* Weight = Element.FindAttribute(TEXT("weight"));
		const FString* Bias = Element.FindAttribute(TEXT("bias"));
		if (!Score && !Weight && !Bias)
		{
			return;
		}
		if (State.Parent == INDEX_NONE || Context.States[State.Parent].Select == EADSKStateSelect::Initial)
		{
			Context.Error(EADSKDiagnosticCode::UnknownAttribute, Element.Line, TEXT("score, weight and bias need a parent state with select"));
			return;
		}
		State.bScored = true;
		if (Score)
		{
			State.ScoreSlot = Context.FindSlot(*Score);
			if (State.ScoreSlot == INDEX_NONE)
			{
				Context.Error(EADSKDiagnosticCode::UnresolvedReference, Element.Line,
					*FString::Printf(TEXT("unknown score slot '%s'%s"), **Score, *DidYouMean(*Score, Context.SlotNames())));
			}
			else if (Context.Slots[State.ScoreSlot].Type != EADSKValueType::Float && Context.Slots[State.ScoreSlot].Type != EADSKValueType::Int)
			{
				Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("score needs a Float or Int slot"));
			}
		}
		if (Weight && !ADSK::TryParseFloat(*Weight, State.ScoreWeight))
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("weight needs a number"));
		}
		if (Bias && !ADSK::TryParseFloat(*Bias, State.ScoreBias))
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("bias needs a number"));
		}
	}

	/** `history` on a state with child states; the machine is never left while it runs, so it takes none. */
	void ParseHistory(FCompileContext& Context, const FADSKXmlElement& Element, FState& State, bool bMachine)
	{
		const FString* History = State.bCompound && !bMachine ? Element.FindAttribute(TEXT("history")) : nullptr;
		if (!History)
		{
			return;
		}
		if (History->Equals(TEXT("shallow"), ESearchCase::CaseSensitive))
		{
			State.History = EADSKStateHistory::Shallow;
		}
		else if (History->Equals(TEXT("deep"), ESearchCase::CaseSensitive))
		{
			State.History = EADSKStateHistory::Deep;
		}
		else
		{
			Context.Error(EADSKDiagnosticCode::UnknownEnumValue, Element.Line,
				*FString::Printf(TEXT("history is shallow or deep, not '%s'"), **History));
		}
	}

	void ValidateStateAttributes(FCompileContext& Context, const FADSKXmlElement& Element, FState& State, bool bMachine)
	{
		if (!bMachine) ParseStateTags(Context, Element, State);
		ParseSelection(Context, Element, State);
		ParseHistory(Context, Element, State, bMachine);
		if (bMachine)
		{
			// score, weight and bias are reported by ParseSelection, which knows why they cannot be here.
			RejectUnknownAttributes(Context, Element, {TEXT("id"), TEXT("initial"), TEXT("select"), TEXT("score"), TEXT("weight"), TEXT("bias")});
			if (const FString* Initial = RequireAttribute(Context, Element, TEXT("initial")))
			{
				State.Initial = *Initial;
			}
		}
		else if (State.bCompound)
		{
			RejectUnknownAttributes(Context, Element, {TEXT("id"), TEXT("initial"), TEXT("tags"), TEXT("select"), TEXT("history"), TEXT("score"), TEXT("weight"), TEXT("bias")});
			if (const FString* Initial = RequireAttribute(Context, Element, TEXT("initial")))
			{
				State.Initial = *Initial;
			}
		}
		else
		{
			RejectUnknownAttributes(Context, Element, {TEXT("id"), TEXT("tree"), TEXT("tags"), TEXT("score"), TEXT("weight"), TEXT("bias")});
			if (const FString* Tree = Element.FindAttribute(TEXT("tree")))
			{
				State.Tree = *Tree;
			}
		}
		if (const FString* Id = RequireAttribute(Context, Element, TEXT("id")))
		{
			State.Id = *Id;
			if (Context.FindState(*Id) != INDEX_NONE)
			{
				Context.Error(EADSKDiagnosticCode::DuplicateId, Element.Line, TEXT("duplicate state id"));
			}
		}
	}

	void ValidateState(FCompileContext& Context, int32 ElementIndex, int32 Parent, int32 Depth, bool bMachine)
	{
		const FADSKXmlElement& Element = Context.Element(ElementIndex);
		if (!CheckStateLimits(Context, Element, Depth))
		{
			return;
		}
		FState State;
		State.Line = Element.Line;
		State.Parent = Parent;
		State.Depth = Depth;
		State.bCompound = HasStateChild(Context, Element);
		ValidateStateAttributes(Context, Element, State, bMachine);
		if (bMachine && !State.bCompound)
		{
			Context.Error(EADSKDiagnosticCode::BadArity, Element.Line, TEXT("StateMachine needs at least one State"));
		}
		const int32 Index = Context.States.Add(MoveTemp(State));
		if (Parent != INDEX_NONE)
		{
			Context.States[Parent].Children.Add(Index);
		}
		for (const int32 Child : Element.Children)
		{
			const FADSKXmlElement& ChildElement = Context.Element(Child);
			if (IsNamed(ChildElement, TEXT("State")))
			{
				ValidateState(Context, Child, Index, Depth + 1, false);
			}
			else if (IsNamed(ChildElement, TEXT("On")))
			{
				ValidateOn(Context, ChildElement, Index);
			}
			else
			{
				Context.Error(EADSKDiagnosticCode::UnknownElement, ChildElement.Line, TEXT("unexpected element"));
			}
		}
		const FState& Done = Context.States[Index];
		if (Done.Select != EADSKStateSelect::Initial && !Done.Children.ContainsByPredicate([&Context](int32 Child) { return Context.States[Child].bScored; }))
		{
			Context.Error(EADSKDiagnosticCode::MissingRequired, Element.Line, TEXT("select needs a child state with score, weight or bias"));
		}
	}
}

using namespace ADSKCompilerValidateStatesPrivate;

void ADSKCompilerInternal::ValidateStateMachine(FCompileContext& Context, int32 ElementIndex)
{
	ValidateState(Context, ElementIndex, INDEX_NONE, 1, true);
}
