// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Compiler/ADSKCompilerInternal.h"

#include "Misc/Crc.h"
#include "UObject/NameTypes.h"

using namespace ADSKCompilerInternal;

namespace ADSKCompilerExpandRunsPrivate
{
	/** Runtime node ids are FNames, so generated ids must be unique ignoring case, like authored ones. */
	struct FNodeIdFuncs : BaseKeyFuncs<FString, FString, false>
	{
		static const FString& GetSetKey(const FString& Element) { return Element; }
		static bool Matches(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::IgnoreCase); }
		static uint32 GetKeyHash(const FString& Key) { return GetTypeHash(Key); }
	};

	/** A diagnostic's node id becomes an `FName`, which must stay below `NAME_SIZE`. */
	FString NameSafe(const FString& Id)
	{
		return Id.Len() < NAME_SIZE ? Id : FString();
	}

	/** Tree parameter slot -> the call site's slot, for the tree whose body is being cloned. */
	using FSubstitution = TMap<int32, int32>;

	/** Expands from an immutable copy; a tree with parameters is a template, expanded only in its clones. */
	class FExpander
	{
	public:
		explicit FExpander(FCompileContext& InContext)
			: Context(InContext)
			, Snapshot(InContext.Nodes)
		{
			for (const FNode& Node : Snapshot)
			{
				if (!Node.bRun)
				{
					Ids.Add(Node.Id);
				}
			}
		}

		void Run()
		{
			for (int32 Index = 0; Index < Snapshot.Num(); ++Index)
			{
				if (Snapshot[Index].bRun)
				{
					ExpandCallSite(Index);
				}
			}
			DropTemplates();
		}

	private:
		FCompileContext& Context;
		const TArray<FNode> Snapshot;
		TSet<FString, FNodeIdFuncs> Ids;
		TSet<int32> Reported;
		TArray<int32> Stack;
		int64 GeneratedChars = 0;
		/** Set by the first resource-limit or collision diagnostic of a call site; it ends that branch. */
		bool bStopped = false;

		void Limit(const FNode& Source, const TCHAR* Message)
		{
			bStopped = true;
			Context.Error(EADSKDiagnosticCode::LimitExceeded, Source.Line, Message, NameSafe(Source.Id));
		}

		void ExpandCallSite(int32 Index)
		{
			int32 Depth = 1;
			int32 Root = Index;
			for (; Snapshot[Root].Parent != INDEX_NONE; Root = Snapshot[Root].Parent)
			{
				++Depth;
			}
			const int32 Owner = Context.Trees.IndexOfByPredicate([Root](const FTree& Tree) { return Tree.Root == Root; });
			if (Owner == INDEX_NONE || !Context.Trees[Owner].Formals.IsEmpty())
			{
				return;
			}
			bStopped = false;
			Stack = {Owner};
			ExpandRun(Index, Index, FString(), Depth, FSubstitution());
		}

		void ReportOnce(int32 Source, EADSKDiagnosticCode Code, int32 Line, const FString& Message)
		{
			bool bAlready = false;
			Reported.Add(Source, &bAlready);
			if (!bAlready)
			{
				Context.Error(Code, Line, *Message, NameSafe(Snapshot[Source].Id));
			}
		}

		/** The target's parameters bound to slots visible at the call site; false after a report. */
		bool BindArgs(int32 Source, const FTree& Target, const FSubstitution& Outer, FSubstitution& OutInner)
		{
			const FNode& Run = Snapshot[Source];
			for (const FRunArg& Arg : Run.RunArgs)
			{
				const int32* Formal = Target.Formals.FindByPredicate([this, &Arg](int32 Slot) { return Context.Slots[Slot].Name.Equals(Arg.Name, ESearchCase::CaseSensitive); });
				if (!Formal)
				{
					ReportOnce(Source, EADSKDiagnosticCode::UnresolvedReference, Arg.Line, FString::Printf(TEXT("tree '%s' has no parameter '%s'"), *Target.Id, *Arg.Name));
					return false;
				}
				const int32* Mapped = Outer.Find(Arg.Slot);
				const int32 Actual = Mapped ? *Mapped : Arg.Slot;
				const FSlot& Want = Context.Slots[*Formal];
				if (Context.Slots[Actual].Type != Want.Type || Context.Slots[Actual].Enum != Want.Enum)
				{
					ReportOnce(Source, EADSKDiagnosticCode::TypeMismatch, Arg.Line, FString::Printf(TEXT("from slot and tree parameter '%s' types differ"), *Arg.Name));
					return false;
				}
				OutInner.Add(*Formal, Actual);
			}
			for (const int32 Formal : Target.Formals)
			{
				if (!OutInner.Contains(Formal))
				{
					ReportOnce(Source, EADSKDiagnosticCode::MissingRequired, Run.Line,
						FString::Printf(TEXT("Run does not bind parameter '%s' of tree '%s'"), *Context.Slots[Formal].Name, *Target.Id));
					return false;
				}
			}
			return true;
		}

		void ExpandRun(int32 Source, int32 Dest, const FString& Prefix, int32 Depth, const FSubstitution& Outer)
		{
			const FNode& Run = Snapshot[Source];
			const int32 Tree = Context.FindTree(Run.RunTree);
			if (Tree == INDEX_NONE)
			{
				bool bAlready = false;
				Reported.Add(Source, &bAlready);
				if (!bAlready)
				{
					Context.Error(EADSKDiagnosticCode::UnresolvedReference, Run.Line,
						*FString::Printf(TEXT("Run names no Tree: '%s'%s"), *Run.RunTree, *DidYouMean(Run.RunTree, Context.TreeIds())), NameSafe(Run.Id));
				}
				return;
			}
			Context.Trees[Tree].bReferenced = true;
			const int32 Cycle = Stack.Find(Tree);
			if (Cycle != INDEX_NONE)
			{
				bool bAlready = false;
				Reported.Add(Source, &bAlready);
				if (!bAlready)
				{
					FString Path;
					for (int32 Index = Cycle; Index < Stack.Num(); ++Index)
					{
						Path += Context.Trees[Stack[Index]].Id + TEXT(" -> ");
					}
					Path += Context.Trees[Tree].Id;
					Context.Error(EADSKDiagnosticCode::UnresolvedReference, Run.Line,
						*FString::Printf(TEXT("Run cycle: %s"), *Path), NameSafe(Run.Id));
				}
				return;
			}
			FSubstitution Inner;
			if (!BindArgs(Source, Context.Trees[Tree], Outer, Inner))
			{
				return;
			}
			Stack.Add(Tree);
			CloneNode(Context.Trees[Tree].Root, Dest, INDEX_NONE, Prefix + Run.Id + TEXT("/"), Depth, Inner);
			Stack.Pop();
		}

		/** Builds `Prefix + authored id` and checks the id bounds and case-insensitive uniqueness; false ends the branch. */
		bool GenerateId(const FNode& Src, const FString& Prefix, FString& OutId)
		{
			OutId = Prefix + Src.Id;
			if (OutId.Len() >= NAME_SIZE)
			{
				Limit(Src, TEXT("generated node id is too long"));
				return false;
			}
			if (GeneratedChars + OutId.Len() > FADSKCompiler::MaxGeneratedIdChars)
			{
				Limit(Src, TEXT("generated node ids exceed 1 Mi characters"));
				return false;
			}
			bool bCollides = false;
			Ids.Add(OutId, &bCollides);
			if (bCollides)
			{
				bStopped = true;
				Context.Error(EADSKDiagnosticCode::DuplicateId, Src.Line, TEXT("generated node id is not unique"), OutId);
				return false;
			}
			GeneratedChars += OutId.Len();
			return true;
		}

		/** Clones one node into `Dest` (in place) or under `ParentDest`; generated ids are `Prefix + authored id`. */
		void CloneNode(int32 Source, int32 Dest, int32 ParentDest, const FString& Prefix, int32 Depth, const FSubstitution& Substitution)
		{
			if (bStopped)
			{
				return;
			}
			const FNode& Src = Snapshot[Source];
			if (Depth > FADSKCompiler::MaxTreeDepth)
			{
				Limit(Src, TEXT("tree depth exceeds 16 after Run expansion"));
				return;
			}
			const bool bAppend = Dest == INDEX_NONE;
			if (bAppend)
			{
				if (Context.Nodes.Num() >= FADSKCompiler::MaxExpandedNodes)
				{
					Limit(Src, TEXT("expanded node count exceeds 4096"));
					return;
				}
				FNode Slot;
				Slot.Parent = ParentDest;
				Dest = Context.Nodes.Add(MoveTemp(Slot));
				Context.Nodes[ParentDest].Children.Add(Dest);
			}
			if (Src.bRun)
			{
				ExpandRun(Source, Dest, Prefix, Depth, Substitution);
				return;
			}
			FString Id;
			if (!GenerateId(Src, Prefix, Id))
			{
				return;
			}
			FNode Clone = Src;
			Clone.Id = Id;
			Clone.Parent = Context.Nodes[Dest].Parent;
			Clone.Children.Reset();
			if (!Substitute(Clone, Substitution))
			{
				return;
			}
			Context.Nodes[Dest] = MoveTemp(Clone);
			for (const int32 Child : Src.Children)
			{
				CloneNode(Child, INDEX_NONE, Dest, Prefix, Depth + 1, Substitution);
			}
		}

		/** Replaces every parameter slot the clone reads or writes; an Out port may not land on an Input. */
		bool Substitute(FNode& Clone, const FSubstitution& Substitution)
		{
			if (Substitution.IsEmpty())
			{
				return true;
			}
			for (int32 Port = 0; Port < Clone.PortSlots.Num(); ++Port)
			{
				const int32* Actual = Substitution.Find(Clone.PortSlots[Port]);
				if (!Actual)
				{
					continue;
				}
				if (Clone.Descriptor->Ports[Port].Direction == EADSKPortDirection::Out && Context.Slots[*Actual].bInput)
				{
					bStopped = true;
					Context.Error(EADSKDiagnosticCode::TypeMismatch, Clone.Line,
						*FString::Printf(TEXT("out port writes tree parameter '%s', which the call site bound to an Input"), *Context.Slots[Clone.PortSlots[Port]].Name), NameSafe(Clone.Id));
					return false;
				}
				Clone.PortSlots[Port] = *Actual;
			}
			if (const int32* Actual = Substitution.Find(Clone.Condition.OperandSlot))
			{
				Clone.Condition.OperandSlot = *Actual;
			}
			for (FADSKCondition& Term : Clone.Terms)
			{
				if (const int32* Actual = Substitution.Find(Term.Slot))
				{
					Term.Slot = *Actual;
				}
				if (const int32* Actual = Substitution.Find(Term.OperandSlot))
				{
					Term.OperandSlot = *Actual;
				}
			}
			for (FEmitBinding& Binding : Clone.EmitBindings)
			{
				if (const int32* Actual = Substitution.Find(Binding.Slot))
				{
					Binding.Slot = *Actual;
				}
			}
			if (const int32* Actual = Substitution.Find(Clone.Assign.Target))
			{
				if (Context.Slots[*Actual].bInput)
				{
					bStopped = true;
					Context.Error(EADSKDiagnosticCode::TypeMismatch, Clone.Line,
						*FString::Printf(TEXT("Set writes tree parameter '%s', which the call site bound to an Input"), *Context.Slots[Clone.Assign.Target].Name), NameSafe(Clone.Id));
					return false;
				}
				Clone.Assign.Target = *Actual;
			}
			if (const int32* Actual = Substitution.Find(Clone.Assign.Source))
			{
				Clone.Assign.Source = *Actual;
			}
			for (FADSKNodeArg& Arg : Clone.Args)
			{
				if (const int32* Actual = Substitution.Find(Arg.Slot))
				{
					Arg.Slot = *Actual;
				}
			}
			return true;
		}

		/** Removes the authored nodes of every tree with parameters; only their clones are ever run. */
		void DropTemplates()
		{
			TBitArray<> Drop(false, Context.Nodes.Num());
			bool bAny = false;
			for (FTree& Tree : Context.Trees)
			{
				if (Tree.Formals.IsEmpty() || Tree.Root == INDEX_NONE)
				{
					continue;
				}
				TArray<int32> Pending = {Tree.Root};
				while (!Pending.IsEmpty())
				{
					const int32 Node = Pending.Pop();
					Drop[Node] = true;
					Pending.Append(Context.Nodes[Node].Children);
				}
				Tree.Root = INDEX_NONE;
				bAny = true;
			}
			if (!bAny)
			{
				return;
			}
			TArray<int32> Remap;
			Remap.Init(INDEX_NONE, Context.Nodes.Num());
			TArray<FNode> Kept;
			for (int32 Index = 0; Index < Context.Nodes.Num(); ++Index)
			{
				if (!Drop[Index])
				{
					Remap[Index] = Kept.Add(MoveTemp(Context.Nodes[Index]));
				}
			}
			for (FNode& Node : Kept)
			{
				Node.Parent = Node.Parent == INDEX_NONE ? INDEX_NONE : Remap[Node.Parent];
				for (int32& Child : Node.Children)
				{
					Child = Remap[Child];
				}
			}
			for (FTree& Tree : Context.Trees)
			{
				Tree.Root = Tree.Root == INDEX_NONE ? INDEX_NONE : Remap[Tree.Root];
			}
			Context.Nodes = MoveTemp(Kept);
		}
	};
}

void ADSKCompilerInternal::ExpandRuns(FCompileContext& Context)
{
	ADSKCompilerExpandRunsPrivate::FExpander(Context).Run();
}
