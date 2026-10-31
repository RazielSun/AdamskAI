// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Program/ADSKDefinition.h"
#include "Program/ADSKDiagnostic.h"
#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKNode.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Xml/ADSKXmlReader.h"
#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"

namespace ADSKCompilerInternal
{
	struct FSlot
	{
		FString Name;
		EADSKValueType Type = EADSKValueType::Bool;
		bool bInput = false;
		bool bRequired = false;
		FADSKValue Default;
		int32 Enum = INDEX_NONE;
		int32 Line = 0;
		/** A tree parameter: visible only inside that tree, replaced by the call site's slot when `Run` expands it. */
		int32 FormalTree = INDEX_NONE;
	};

	struct FEnum
	{
		FString Name;
		TArray<FString> Values;
	};

	struct FParam
	{
		FString Name;
		EADSKValueType Type = EADSKValueType::Bool;
		int32 Enum = INDEX_NONE;
	};

	struct FEventInfo
	{
		FString Name;
		int32 Priority = 0;
		bool bReserved = false;
		int32 ChangeSlot = INDEX_NONE;
		TArray<FParam> Params;
		int32 Line = 0;
	};

	struct FBind
	{
		int32 Param = INDEX_NONE;
		int32 Slot = INDEX_NONE;
	};

	struct FTransition
	{
		int32 Line = 0;
		int32 Event = INDEX_NONE;
		FString Goto;
		bool bHasGoto = false;
		int32 Target = INDEX_NONE;
		/** Authored `if` or `unless`, even when its slot did not resolve. */
		bool bHasCondition = false;
		FADSKCondition Condition;
		/** `<When>` and `<Any>` children, in document order. */
		TArray<FADSKCondition> Terms;
		float Chance = 1.f;
		int32 ChanceSlot = INDEX_NONE;
		TArray<FBind> Binds;

		bool IsCertain() const { return !bHasCondition && Terms.IsEmpty() && Chance >= 1.f && ChanceSlot == INDEX_NONE; }
	};

	struct FState
	{
		FString Id;
		int32 Line = 0;
		int32 Parent = INDEX_NONE;
		int32 Depth = 1;
		bool bCompound = false;
		FString Initial;
		int32 InitialState = INDEX_NONE;
		FString Tree;
		int32 RootNode = INDEX_NONE;
		TArray<FName> Tags;
		TArray<int32> Children;
		TArray<FTransition> Transitions;
		EADSKStateSelect Select = EADSKStateSelect::Initial;
		EADSKStateHistory History = EADSKStateHistory::None;
		bool bScored = false;
		int32 ScoreSlot = INDEX_NONE;
		float ScoreWeight = 1.f;
		float ScoreBias = 0.f;
	};

	struct FTree
	{
		FString Id;
		int32 Line = 0;
		int32 Root = INDEX_NONE;
		bool bReferenced = false;
		/** Slot indices of the tree's parameters; a tree with any is reachable only through `Run`. */
		TArray<int32> Formals;
	};

	struct FRunArg
	{
		FString Name;
		int32 Slot = INDEX_NONE;
		int32 Line = 0;
	};

	/** `<Param name value|from/>` under an `Emit`, as authored; resolved against the event once it is known. */
	struct FEmitArg
	{
		FString Name;
		int32 Slot = INDEX_NONE;
		FString Value;
		int32 Line = 0;
	};

	/** One event parameter an `Emit` supplies, in parameter order: a slot read at emit time, or a constant. */
	struct FEmitBinding
	{
		int32 Slot = INDEX_NONE;
		FADSKValue Value;
	};

	struct FNode
	{
		const FADSKNodeDescriptor* Descriptor = nullptr;
		FString Id;
		int32 Line = 0;
		int32 Parent = INDEX_NONE;
		TArray<int32> Children;
		TArray<int32> PortSlots;
		FInstancedStruct Config;
		FString EmitEventName;
		bool bHasEmitEvent = false;
		int32 EmitEvent = INDEX_NONE;
		/** Guard: its comparison, if authored. The tested slot is the `condition` port's binding, filled at link time. */
		FADSKCondition Condition;
		/** Guard: its `<When>` and `<Any>` children, in document order. */
		TArray<FADSKCondition> Terms;
		/** `Run` placeholder: compile-time syntax with no descriptor; expanded away before linking. */
		bool bRun = false;
		/** Declared under `<Sensors>`: in no tree, its out ports write Inputs. */
		bool bSensor = false;
		FString RunTree;
		TArray<FRunArg> RunArgs;
		TArray<FEmitArg> EmitArgs;
		TArray<FEmitBinding> EmitBindings;
		/** A node whose descriptor takes args: its `<Param name from/>` children, in document order. */
		TArray<FADSKNodeArg> Args;
		/** Set: `to`, and `from` or the parsed `value`. */
		FADSKAssignDefinition Assign;
	};

	struct FCompileContext
	{
		FCompileContext(const FADSKXmlDocument& InDocument, const TSharedRef<const FADSKNodeRegistry>& InRegistry,
			const FString& InSource, TArray<FADSKDiagnostic>& InDiagnostics);

		const FADSKXmlDocument& Document;
		TSharedRef<const FADSKNodeRegistry> Registry;
		FString Source;
		TArray<FADSKDiagnostic>& Diagnostics;

		FString AgentId;
		TArray<FSlot> Slots;
		TArray<FEnum> Enums;
		/** Indices 0 and 1 are the reserved tree completion events. */
		TArray<FEventInfo> Events;
		TArray<FState> States;
		TArray<FTree> Trees;
		TArray<FNode> Nodes;
		bool bStateLimitReported = false;
		/** The tree being walked, whose parameters FindSlot also sees; INDEX_NONE outside trees. */
		int32 CurrentTree = INDEX_NONE;
		/** The `<Sensors>` section is being walked: sensors only, out ports on Inputs. */
		bool bInSensors = false;

		const FADSKXmlElement& Element(int32 Index) const { return Document.Elements[Index]; }

		void Report(EADSKSeverity Severity, EADSKDiagnosticCode Code, int32 Line, const FString& NodeId, const TCHAR* Message);
		void Error(EADSKDiagnosticCode Code, int32 Line, const TCHAR* Message, const FString& NodeId = FString());

		/** Inputs, Locals and the parameters of CurrentTree. */
		int32 FindSlot(const FString& Name) const;
		int32 FindEnum(const FString& Name) const;
		int32 FindEvent(const FString& Name) const;
		int32 FindState(const FString& Id) const;
		int32 FindTree(const FString& Id) const;

		/** Candidates for DidYouMean: what FindSlot, FindEvent, FindState and FindTree would accept. */
		TArray<FString> SlotNames() const;
		TArray<FString> EventNames() const;
		TArray<FString> StateIds() const;
		TArray<FString> TreeIds() const;
	};

	/** `; did you mean 'X'?` naming the closest candidate within a few edits (a case difference is one), else empty. */
	FString DidYouMean(FStringView Name, TConstArrayView<FString> Candidates);

	void RejectUnknownAttributes(FCompileContext& Context, const FADSKXmlElement& Element, TConstArrayView<const TCHAR*> Allowed);

	const FString* RequireAttribute(FCompileContext& Context, const FADSKXmlElement& Element, const TCHAR* Name);

	void RejectChildren(FCompileContext& Context, const FADSKXmlElement& Element);

	bool ParseValueType(const FString& Text, EADSKValueType& OutType);

	/** More than one comparison attribute is `bad-arity`, reported here; the result is then null and OutOp unset. */
	const FADSKXmlAttribute* FindCompareAttribute(FCompileContext& Context, const FADSKXmlElement& Element, EADSKCompareOp& OutOp, const FString& NodeId = FString());

	/**
	 * `is` takes an Int literal or an enum value name; `below`/`above` take an Int or Float slot and a literal
	 * of that type or a same-typed slot (OutSlot). Reports and returns false otherwise.
	 */
	bool ParseCompareOperand(FCompileContext& Context, const FSlot& Slot, EADSKCompareOp Op, const FString& Text, int32 Line,
		const FString& NodeId, FADSKValue& OutValue, int32& OutSlot);

	/**
	 * `if`/`unless` with an optional `is`/`below`/`above`, as on `On` and `When`. bOutHas: one of `if`/`unless` was
	 * authored. bOutSyntaxOk turns false on conflicting or orphan attributes; an unresolved slot leaves it true.
	 */
	void ParseConditionAttributes(FCompileContext& Context, const FADSKXmlElement& Element, const FString& NodeId,
		FADSKCondition& OutCondition, bool& bOutHas, bool& bOutSyntaxOk);

	bool IsConditionElement(const FADSKXmlElement& Element);

	/** Appends a `<When>`'s term, or an `<Any>`'s terms joined by bAnyWithNext; bOutSyntaxOk as for attributes. */
	void ValidateConditionElement(FCompileContext& Context, const FADSKXmlElement& Element, const FString& NodeId,
		TArray<FADSKCondition>& OutTerms, bool& bOutSyntaxOk);

	/** Walks the document and fills the context; reference passes run last. */
	void ValidateDocument(FCompileContext& Context);
	void ValidateStateMachine(FCompileContext& Context, int32 ElementIndex);
	void ValidateTree(FCompileContext& Context, int32 ElementIndex);
	void ValidateSensors(FCompileContext& Context, int32 ElementIndex);
	/** `<Param name type/>` of a Tree: a parameter slot of the tree being walked; INDEX_NONE after a report. */
	int32 ValidateTreeParam(FCompileContext& Context, const FADSKXmlElement& Element);
	void ResolveReferences(FCompileContext& Context, bool bSourceWalkClean);

	void ExpandRuns(FCompileContext& Context);
	/** Points each state at its tree's root node; runs after `ExpandRuns` so roots are final. */
	void ResolveTrees(FCompileContext& Context);
	void WarnUnreferencedTrees(FCompileContext& Context);
}

namespace ADSKLink
{
	/** Requires a validated, error-free context. */
	struct FLinker
	{
		static TSharedRef<const FADSKDefinition> Link(const ADSKCompilerInternal::FCompileContext& Context);
	};
}
