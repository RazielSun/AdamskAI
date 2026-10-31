// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Runtime/ADSKNodeRegistry.h"
#include "Core/ADSKTypes.h"
#include "CoreMinimal.h"
#include "Misc/SecureHash.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/SoftObjectPath.h"

namespace ADSKLink
{
	struct FLinker;
}

namespace ADSK
{
	/** Reserved tree completion events always occupy these event indices. */
	constexpr int32 TreeSuccessEvent = 0;
	constexpr int32 TreeFailureEvent = 1;
}

enum class EADSKCompareOp : uint8
{
	Equal,
	Below,
	Above,
};

/** The operand is OperandSlot's current value when that is set, Operand otherwise. Without a Slot it always holds. */
struct FADSKCondition
{
	int32 Slot = INDEX_NONE;
	/** `unless`: the outcome is inverted. */
	bool bNegated = false;
	/** Compares Slot with the operand instead of reading a Bool. */
	bool bCompares = false;
	EADSKCompareOp Op = EADSKCompareOp::Equal;
	FADSKValue Operand;
	int32 OperandSlot = INDEX_NONE;
	/** A term of an `<Any>`: it and the next term are alternatives; a run of such terms is one group. */
	bool bAnyWithNext = false;

	bool IsSet() const { return Slot != INDEX_NONE; }

	bool operator==(const FADSKCondition& Other) const
	{
		return Slot == Other.Slot && bNegated == Other.bNegated && bCompares == Other.bCompares && bAnyWithNext == Other.bAnyWithNext
			&& (!bCompares || (Op == Other.Op && OperandSlot == Other.OperandSlot && (OperandSlot != INDEX_NONE || Operand == Other.Operand)));
	}
};

struct FADSKSlotDefinition
{
	FString Name;
	EADSKValueType Type = EADSKValueType::Bool;
	bool bIsInput = false;
	bool bRequired = false;
	/** Starting value: the authored default for an Input, zero for a Local or required Input. */
	FADSKValue Default;
	int32 Enum = INDEX_NONE;
	int32 Line = 0;
};

struct FADSKEnumDefinition
{
	FString Name;
	int32 FirstValue = 0;
	int32 ValueCount = 0;
};

struct FADSKEventParamDefinition
{
	FString Name;
	EADSKValueType Type = EADSKValueType::Bool;
	int32 Enum = INDEX_NONE;
};

struct FADSKEventDefinition
{
	FString Name;
	int32 Priority = 0;
	int32 FirstParam = 0;
	int32 ParamCount = 0;
	bool bReserved = false;
	/** `change="Slot"`: the core raises the event whenever that slot's value changes. INDEX_NONE otherwise. */
	int32 ChangeSlot = INDEX_NONE;
	/** The `<Event>` element's line; 0 for the reserved completions. */
	int32 Line = 0;

	/** Reserved completions and change events; hosts and Emit cannot raise them. */
	bool IsRaisedByCore() const { return bReserved || ChangeSlot != INDEX_NONE; }
};

/** Copies one event parameter, by index within its event, into a Local slot. */
struct FADSKBindDefinition
{
	int32 Param = INDEX_NONE;
	int32 Slot = INDEX_NONE;
};

struct FADSKTransitionDefinition
{
	int32 Source = INDEX_NONE;
	int32 Event = INDEX_NONE;
	/** INDEX_NONE for an internal transition. */
	int32 Target = INDEX_NONE;
	int32 FirstBind = 0;
	int32 BindCount = 0;
	FADSKCondition Condition;
	/** `<When>` and `<Any>` children: a range of FADSKDefinition::GetConditionTerms that must hold besides Condition. */
	int32 FirstTerm = 0;
	int32 TermCount = 0;
	/** `chance`: once the condition passes, the handler is taken with this probability; 1 always takes it. */
	float Chance = 1.f;
	/** `chance="Slot"`: the probability is that Float slot's value, clamped to [0, 1], read when the handler is tried. */
	int32 ChanceSlot = INDEX_NONE;
	int32 Line = 0;

	/** No condition and no chance: later handlers for the same event in this state never run. */
	bool IsCertain() const { return !Condition.IsSet() && TermCount == 0 && Chance >= 1.f && ChanceSlot == INDEX_NONE; }
};

/** How entering a state with child states picks the child to enter. */
enum class EADSKStateSelect : uint8
{
	/** Its `initial` child. */
	Initial,
	/** `select="best"`: the highest-scoring child, ties in document order. */
	Best,
	/** `select="weighted"`: a child drawn in proportion to its score. */
	Weighted,
};

/** What re-entering a state with child states resumes, once it has been left. */
enum class EADSKStateHistory : uint8
{
	None,
	/** `history="shallow"`: the child it left; that child enters its own entry child as usual. */
	Shallow,
	/** `history="deep"`: the child it left, and below it every state as it was left. */
	Deep,
};

struct FADSKStateDefinition
{
	FString Id;
	int32 Parent = INDEX_NONE;
	int32 FirstChild = 0;
	int32 ChildCount = 0;
	int32 Initial = INDEX_NONE;
	int32 Depth = 1;
	int32 RootNode = INDEX_NONE;
	int32 FirstTransition = 0;
	int32 TransitionCount = 0;
	int32 Line = 0;
	/** The state's `tags`, a range of FADSKDefinition::GetStateTags; they hold while it or a descendant is active. */
	int32 FirstTag = 0;
	int32 TagCount = 0;
	/** A child scoring above 0 is entered instead of Initial; with none, Initial is. */
	EADSKStateSelect Select = EADSKStateSelect::Initial;
	/** Entering it again after leaving it resumes the child it left, before Select or Initial are consulted. */
	EADSKStateHistory History = EADSKStateHistory::None;
	/**
	 * As a child of a selecting state: scores `ScoreWeight * ScoreSlot + ScoreBias`, the slot reading 1 when unset.
	 * A child without `score`, `weight` or `bias` scores 0 and is entered only through a transition or as Initial.
	 */
	bool bScored = false;
	int32 ScoreSlot = INDEX_NONE;
	float ScoreWeight = 1.f;
	float ScoreBias = 0.f;
};

struct FADSKTreeDefinition
{
	FString Id;
	int32 RootNode = INDEX_NONE;
	int32 Line = 0;
};

/** One event parameter an Emit supplies: the value of Slot when it is set, Value otherwise. */
struct FADSKEmitArgDefinition
{
	int32 Slot = INDEX_NONE;
	FADSKValue Value;
};

/** A Set: Target takes the value of Source when it is set, Value otherwise. */
struct FADSKAssignDefinition
{
	int32 Target = INDEX_NONE;
	int32 Source = INDEX_NONE;
	FADSKValue Value;
};

struct FADSKNodeDefinition
{
	/** Owned by the retained registry. */
	const FADSKNodeDescriptor* Descriptor = nullptr;
	FName Id;
	/** The id as authored. FName keeps the case it was first made with outside the editor; this keeps the file's. */
	FString AuthoredId;
	int32 Line = 0;
	int32 Parent = INDEX_NONE;
	int32 FirstChild = 0;
	int32 ChildCount = 0;
	/** One entry per descriptor port. */
	int32 FirstPortBinding = 0;
	int32 PortCount = 0;
	int32 EmitEvent = INDEX_NONE;
	/** Emit only: one entry per event parameter, in parameter order. */
	int32 FirstEmitArg = 0;
	int32 EmitArgCount = 0;
	/** Set only. */
	FADSKAssignDefinition Assign;
	/** A node whose descriptor takes args: its `<Param name from/>` children, a range of GetNodeArgs. */
	int32 FirstArg = 0;
	int32 ArgCount = 0;
	/** Guard: its condition, on the slot bound to its `condition` port. Unset for every other kind. */
	FADSKCondition Condition;
	/** Guard: its `<When>` and `<Any>` children, which must hold besides Condition; a range of GetConditionTerms. */
	int32 FirstTerm = 0;
	int32 TermCount = 0;
	/** Descriptor config struct with every authored field applied; invalid when the node has none. */
	FInstancedStruct Config;
};

/** Immutable and shared by every instance; retains the frozen registry that owns its nodes' descriptors. */
class ADAMSKAI_API FADSKDefinition
{
public:
	FADSKDefinition(const FADSKDefinition&) = delete;
	FADSKDefinition& operator=(const FADSKDefinition&) = delete;

	const FString& GetSourceName() const { return SourceName; }
	const FString& GetAgentId() const { return AgentId; }
	const FSHAHash& GetSourceHash() const { return SourceHash; }
	const FSHAHash& GetCatalogueFingerprint() const { return CatalogueFingerprint; }
	const FSHAHash& GetCompositeHash() const { return CompositeHash; }
	FString GetCatalogueFingerprintHex() const { return CatalogueFingerprint.ToString().ToLower(); }
	FString GetCompositeHashHex() const { return CompositeHash.ToString().ToLower(); }
	int32 GetLanguageVersion() const { return LanguageVersion; }
	/** Every node is a core intrinsic kind or thread-safe, so FADSKParallelTick may tick an instance on a worker. */
	bool IsThreadSafe() const { return bThreadSafe; }
	/** Unique per compiled definition for the process lifetime, unlike its address, which a later one may reuse. */
	uint64 GetSerial() const { return Serial; }
	const TSharedRef<const FADSKNodeRegistry>& GetRegistry() const { return Registry; }

	TConstArrayView<FADSKSlotDefinition> GetSlots() const { return Slots; }
	TConstArrayView<FADSKEnumDefinition> GetEnums() const { return Enums; }
	TConstArrayView<FString> GetEnumValueNames() const { return EnumValueNames; }
	TConstArrayView<FADSKEventDefinition> GetEvents() const { return Events; }
	TConstArrayView<FADSKEventParamDefinition> GetEventParams() const { return EventParams; }
	TConstArrayView<FADSKStateDefinition> GetStates() const { return States; }
	TConstArrayView<int32> GetStateChildren() const { return StateChildren; }
	/** Every state's tags, addressed by FADSKStateDefinition::FirstTag/TagCount; dotted names (`AI.State.Combat`). */
	TConstArrayView<FName> GetStateTags() const { return StateTags; }
	TConstArrayView<FADSKTransitionDefinition> GetTransitions() const { return Transitions; }
	TConstArrayView<FADSKBindDefinition> GetBinds() const { return Binds; }
	TConstArrayView<FADSKTreeDefinition> GetTrees() const { return Trees; }
	TConstArrayView<FADSKNodeDefinition> GetNodes() const { return Nodes; }
	TConstArrayView<int32> GetNodeChildren() const { return NodeChildren; }
	/** Slot index per node port; INDEX_NONE never appears in a valid definition. */
	TConstArrayView<int32> GetPortBindings() const { return PortBindings; }
	TConstArrayView<FADSKEmitArgDefinition> GetEmitArgs() const { return EmitArgs; }
	TConstArrayView<FADSKNodeArg> GetNodeArgs() const { return NodeArgs; }
	/** The `<When>`/`<Any>` terms of every transition and Guard, addressed by their FirstTerm/TermCount. */
	TConstArrayView<FADSKCondition> GetConditionTerms() const { return ConditionTerms; }
	/** Node indices of the `<Sensors>` in document order; no tree holds them, and the host samples them. */
	TConstArrayView<int32> GetSensors() const { return Sensors; }

	/** Diagnostic and query lookups (exact case); INDEX_NONE when absent. Execution never uses these. */
	int32 FindState(FStringView Id) const;
	int32 FindEvent(FStringView Name) const;
	int32 FindSlot(FStringView Name) const;
	int32 FindEnum(FStringView Name) const;
	TOptional<FADSKValue> MakeEnumValue(FStringView EnumName, FStringView ValueName) const;
	bool ResolveEnumValue(const FADSKValue& Value, FString& OutTypeName, FString& OutValueName) const;
	int32 FindNode(FName Id) const;

private:
	friend struct ADSKLink::FLinker;

	explicit FADSKDefinition(const TSharedRef<const FADSKNodeRegistry>& InRegistry)
		: Registry(InRegistry)
	{
	}

	TSharedRef<const FADSKNodeRegistry> Registry;
	FString SourceName;
	FString AgentId;
	FSHAHash SourceHash;
	FSHAHash CatalogueFingerprint;
	FSHAHash CompositeHash;
	int32 LanguageVersion = 1;
	uint64 Serial = 0;
	bool bThreadSafe = false;
	TArray<FADSKSlotDefinition> Slots;
	TArray<FADSKEnumDefinition> Enums;
	TArray<FString> EnumValueNames;
	TArray<FADSKEventDefinition> Events;
	TArray<FADSKEventParamDefinition> EventParams;
	TArray<FADSKStateDefinition> States;
	TArray<int32> StateChildren;
	TArray<FName> StateTags;
	TArray<FADSKTransitionDefinition> Transitions;
	TArray<FADSKBindDefinition> Binds;
	TArray<FADSKTreeDefinition> Trees;
	TArray<FADSKNodeDefinition> Nodes;
	TArray<int32> NodeChildren;
	TArray<int32> PortBindings;
	TArray<FADSKEmitArgDefinition> EmitArgs;
	TArray<FADSKNodeArg> NodeArgs;
	TArray<FADSKCondition> ConditionTerms;
	TArray<int32> Sensors;
};

/** One ObjectPath or ClassPath configuration value of a compiled node, authored or defaulted. */
struct ADAMSKAI_API FADSKObjectPathField
{
	int32 NodeIndex = INDEX_NONE;
	FName NodeId;
	int32 Line = 0;
	FSoftObjectPath Path;
	/** A ClassPath field: the path names a class, which must derive from MetaClass when that is set. */
	bool bClass = false;
	const UClass* MetaClass = nullptr;
};

/** One Tag configuration value of a compiled node, authored or defaulted. */
struct ADAMSKAI_API FADSKTagField
{
	int32 NodeIndex = INDEX_NONE;
	FName NodeId;
	int32 Line = 0;
	FName Tag;
};

namespace ADSK
{
	/** Every ObjectPath config field of every compiled node, in node then descriptor field order. */
	ADAMSKAI_API TArray<FADSKObjectPathField> GetObjectPathFields(const FADSKDefinition& Definition);

	/** Every nonempty Tag config field of every compiled node, in node then descriptor field order. */
	ADAMSKAI_API TArray<FADSKTagField> GetTagFields(const FADSKDefinition& Definition);

	/** Equal compares type and value; Below and Above are false for anything but two Ints or two Floats. */
	ADAMSKAI_API bool Compare(const FADSKValue& Current, EADSKCompareOp Op, const FADSKValue& Operand);

	/** An unset condition always holds. */
	ADAMSKAI_API bool EvaluateCondition(const FADSKCondition& Condition, TConstArrayView<FADSKValue> Slots);

	/** Every group holds: a lone term, or a run joined by bAnyWithNext of which one term holds. No terms hold. */
	ADAMSKAI_API bool EvaluateTerms(TConstArrayView<FADSKCondition> Terms, TConstArrayView<FADSKValue> Slots);

	/** The handler's attribute condition and every one of its `<When>`/`<Any>` terms. */
	ADAMSKAI_API bool EvaluateTransitionCondition(const FADSKDefinition& Definition, const FADSKTransitionDefinition& Transition, TConstArrayView<FADSKValue> Slots);

	ADAMSKAI_API const TCHAR* CompareOpAttribute(EADSKCompareOp Op);
}
