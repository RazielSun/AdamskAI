// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Compiler/ADSKCompilerInternal.h"

using namespace ADSKCompilerInternal;

/** Runs only after every state, tree and node has been read. */
namespace ADSKCompilerResolvePrivate
{
	void ResolveInitials(FCompileContext& Context)
	{
		for (FState& State : Context.States)
		{
			if (!State.bCompound || State.Initial.IsEmpty())
			{
				continue;
			}
			for (const int32 Child : State.Children)
			{
				if (Context.States[Child].Id.Equals(State.Initial, ESearchCase::CaseSensitive))
				{
					State.InitialState = Child;
				}
			}
			if (State.InitialState == INDEX_NONE)
			{
				Context.Error(EADSKDiagnosticCode::UnresolvedReference, State.Line, TEXT("initial must name a direct child state"));
			}
		}
	}

	void ResolveTargets(FCompileContext& Context)
	{
		for (FState& State : Context.States)
		{
			for (FTransition& Transition : State.Transitions)
			{
				if (!Transition.bHasGoto)
				{
					continue;
				}
				Transition.Target = Context.FindState(Transition.Goto);
				if (Transition.Target == INDEX_NONE)
				{
					Context.Error(EADSKDiagnosticCode::UnresolvedReference, Transition.Line,
					*FString::Printf(TEXT("goto names no state: '%s'%s"), *Transition.Goto, *DidYouMean(Transition.Goto, Context.StateIds())));
				}
			}
		}
	}

	/** Matches an Emit's `<Param>`s to the event's parameters, in parameter order; false after a report. */
	bool BindEmitArgs(FCompileContext& Context, FNode& Node, const FEventInfo& Event)
	{
		bool bOk = true;
		for (const FEmitArg& Arg : Node.EmitArgs)
		{
			if (!Event.Params.ContainsByPredicate([&Arg](const FParam& Param) { return Param.Name.Equals(Arg.Name, ESearchCase::CaseSensitive); }))
			{
				Context.Error(EADSKDiagnosticCode::UnresolvedReference, Arg.Line, TEXT("Param names no parameter of the event"), Node.Id);
				bOk = false;
			}
		}
		for (const FParam& Param : Event.Params)
		{
			const FEmitArg* Arg = Node.EmitArgs.FindByPredicate([&Param](const FEmitArg& Candidate) { return Candidate.Name.Equals(Param.Name, ESearchCase::CaseSensitive); });
			FEmitBinding& Binding = Node.EmitBindings.AddDefaulted_GetRef();
			if (!Arg)
			{
				Context.Error(EADSKDiagnosticCode::MissingRequired, Node.Line, *FString::Printf(TEXT("Emit does not supply event parameter '%s'"), *Param.Name), Node.Id);
				bOk = false;
			}
			else if (Arg->Slot != INDEX_NONE)
			{
				const FSlot& Slot = Context.Slots[Arg->Slot];
				if (Slot.Type != Param.Type || Slot.Enum != Param.Enum)
				{
					Context.Error(EADSKDiagnosticCode::TypeMismatch, Arg->Line, TEXT("from slot and event parameter types differ"), Node.Id);
					bOk = false;
				}
				Binding.Slot = Arg->Slot;
			}
			else if (Param.Type == EADSKValueType::Enum)
			{
				const int32 Value = Context.Enums[Param.Enum].Values.IndexOfByPredicate([Arg](const FString& Name) { return Name.Equals(Arg->Value, ESearchCase::CaseSensitive); });
				if (Value == INDEX_NONE)
				{
					Context.Error(EADSKDiagnosticCode::UnknownEnumValue, Arg->Line, TEXT("value names no value of the parameter's enum"), Node.Id);
					bOk = false;
				}
				Binding.Value = FADSKValue::MakeEnum(Param.Enum, Value);
			}
			else if (!ADSK::TryParseValue(Param.Type, Arg->Value, Binding.Value))
			{
				Context.Error(EADSKDiagnosticCode::TypeMismatch, Arg->Line, TEXT("value does not match the event parameter type"), Node.Id);
				bOk = false;
			}
		}
		if (!bOk)
		{
			Node.EmitBindings.Reset();
		}
		return bOk;
	}

	void ResolveEmitEvents(FCompileContext& Context)
	{
		for (FNode& Node : Context.Nodes)
		{
			if (!Node.bHasEmitEvent)
			{
				continue;
			}
			const int32 Event = Context.FindEvent(Node.EmitEventName);
			if (Event == INDEX_NONE)
			{
				Context.Error(EADSKDiagnosticCode::UnresolvedReference, Node.Line,
					*FString::Printf(TEXT("unknown event '%s'%s"), *Node.EmitEventName, *DidYouMean(Node.EmitEventName, Context.EventNames())), Node.Id);
			}
			else if (Context.Events[Event].bReserved || Context.Events[Event].ChangeSlot != INDEX_NONE)
			{
				Context.Error(EADSKDiagnosticCode::TypeMismatch, Node.Line,
					TEXT("Emit needs a declared event that is neither reserved nor a change event"), Node.Id);
			}
			else if (BindEmitArgs(Context, Node, Context.Events[Event]))
			{
				Node.EmitEvent = Event;
			}
		}
	}
}

using namespace ADSKCompilerResolvePrivate;

void ADSKCompilerInternal::ResolveTrees(FCompileContext& Context)
{
	for (FState& State : Context.States)
	{
		if (State.Tree.IsEmpty())
		{
			continue;
		}
		const int32 Tree = Context.FindTree(State.Tree);
		if (Tree == INDEX_NONE)
		{
			Context.Error(EADSKDiagnosticCode::UnresolvedReference, State.Line,
				*FString::Printf(TEXT("tree names no Tree: '%s'%s"), *State.Tree, *DidYouMean(State.Tree, Context.TreeIds())));
			continue;
		}
		Context.Trees[Tree].bReferenced = true;
		if (!Context.Trees[Tree].Formals.IsEmpty())
		{
			Context.Error(EADSKDiagnosticCode::MissingRequired, State.Line, TEXT("tree takes parameters; call it with Run"));
			continue;
		}
		State.RootNode = Context.Trees[Tree].Root;
	}
}

void ADSKCompilerInternal::WarnUnreferencedTrees(FCompileContext& Context)
{
	for (const FTree& Tree : Context.Trees)
	{
		if (!Tree.bReferenced)
		{
			Context.Report(EADSKSeverity::Warning, EADSKDiagnosticCode::UnreferencedTree, Tree.Line, FString(), TEXT("tree is not referenced"));
		}
	}
}

/** Order: initials and targets, Emit events, Run expansion, state tree roots, then warnings. */
void ADSKCompilerInternal::ResolveReferences(FCompileContext& Context, bool bSourceWalkClean)
{
	if (!bSourceWalkClean)
	{
		return;
	}
	ResolveInitials(Context);
	ResolveTargets(Context);
	ResolveEmitEvents(Context);
	ExpandRuns(Context);
	ResolveTrees(Context);
	WarnUnreferencedTrees(Context);
}
