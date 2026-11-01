// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Core/ADSKTypes.h"
#include "CoreMinimal.h"
#include "UObject/Class.h"

class FProperty;

enum class EADSKNodeStatus : uint8
{
	Running,
	Succeeded,
	Failed,
};

/** PartialEntry means Start acquired something and then failed. */
enum class EADSKEndReason : uint8
{
	Succeeded,
	Failed,
	Cancelled,
	PartialEntry,
};

enum class EADSKPortDirection : uint8
{
	In,
	Out,
};

/**
 * Scope: one child, ends with its result. Parallel: loops the second child beside the first. Concurrent: every child
 * in its own lane, ends by policy. Sensor: never in a tree, sampled into Inputs before every tick. Set: writes a Local.
 */
enum class EADSKNodeKind : uint8
{
	Leaf,
	Sequence,
	Guard,
	Selector,
	Emit,
	Scope,
	Parallel,
	Composite,
	Sensor,
	Set,
	Concurrent,
};

/** ObjectPath and Choice ignore ValueType. */
enum class EADSKFieldKind : uint8
{
	Value,
	ObjectPath,
	/** Authored by enumerator name (`Best`, not `0`). */
	Choice,
	/** Case is not significant. */
	Name,
	String,
	/** Preloaded like an ObjectPath and checked against the field's MetaClass. */
	ClassPath,
	/** The validator reports a tag that is not registered. */
	Tag,
};

struct FADSKFieldMap
{
	/** Case-sensitive XML attribute name. An FString: FName would fold case. */
	FString XmlName;
	FName PropertyName;

	/** Filled in by the registry when the descriptor is accepted. */
	const FProperty* Property = nullptr;
	EADSKValueType ValueType = EADSKValueType::Bool;
	EADSKFieldKind Kind = EADSKFieldKind::Value;
	FString DefaultValue;
	/** Choice only: the enum and its authorable names, in declaration order, without the generated `_MAX`. */
	const UEnum* Enum = nullptr;
	TArray<FString> ChoiceNames;
	/** ClassPath only, set by the provider: the class the authored class must derive from; null accepts any. */
	const UClass* MetaClass = nullptr;
	/** Kept only so older files still compile; the catalogue marks it, and new files should not use it. */
	bool bLegacy = false;
};

namespace ADSK
{
	/** The tag name of an FGameplayTag value, read by reflection so the core needs no GameplayTags dependency. */
	ADAMSKAI_API FName GetTagName(const UScriptStruct& TagStruct, const void* Value);
	ADAMSKAI_API void SetTagName(const UScriptStruct& TagStruct, void* Value, FName TagName);
	ADAMSKAI_API bool IsGameplayTagStruct(const UScriptStruct* Struct);
}

/** XML binds it with `in-<Name>` or `out-<Name>`. */
struct FADSKPortDescriptor
{
	FString Name;
	EADSKValueType Type = EADSKValueType::Bool;
	EADSKPortDirection Direction = EADSKPortDirection::In;
};

struct FADSKNamedValue
{
	FString Name;
	FADSKValue Value;
};

/** One `<Param name from/>` of a node whose descriptor sets bTakesArgs: the slot read whenever the node reads it. */
struct FADSKNodeArg
{
	FString Name;
	int32 Slot = INDEX_NONE;
};

/** Told about each Out port write that changed its slot, so the instance can trace it. */
class IADSKSlotWriteSink
{
public:
	virtual void SlotWritten(int32 Node, int32 Slot, FADSKActivationHandle Handle) = 0;

protected:
	~IADSKSlotWriteSink() = default;
};

struct FADSKNodeContextParams
{
	const UScriptStruct* ConfigStruct = nullptr;
	const void* Config = nullptr;
	const UScriptStruct* MemoryStruct = nullptr;
	void* Memory = nullptr;
	const UScriptStruct* StateStruct = nullptr;
	void* State = nullptr;
	TArrayView<const FADSKPortDescriptor> Ports;
	TArrayView<const int32> PortSlots;
	TArrayView<const FADSKNodeArg> Args;
	TArrayView<FADSKValue> Slots;
	float Delta = 0.f;
	FADSKActivationHandle Handle;
	FName NodeId;
	TWeakObjectPtr<UObject> Host;
	FRandomStream* Random = nullptr;
	double Time = 0.0;
	int32 ChildCount = 0;
	/** Null when nothing records the writes. */
	IADSKSlotWriteSink* SlotSink = nullptr;
	/** The node's index in the definition, passed to SlotSink. */
	int32 Node = INDEX_NONE;
};

enum class EADSKSelectorPolicy : uint8
{
	Ordered,
	/** An earlier Guard option turning true since last seen cancels the running option and takes its place. */
	Reactive,
	/** Options scoring 0 or less are skipped; a rescore switches only when another wins by more than `margin`. */
	Utility,
};

/** Callback-scoped; must not be retained past the callback. */
class ADAMSKAI_API FADSKNodeContext
{
public:
	explicit FADSKNodeContext(const FADSKNodeContextParams& Params);

	FADSKNodeContext(const FADSKNodeContext&) = delete;
	FADSKNodeContext& operator=(const FADSKNodeContext&) = delete;

	template <typename T>
	const T& GetConfig() const
	{
		checkf(ConfigStruct == T::StaticStruct(), TEXT("node configuration type mismatch"));
		return *static_cast<const T*>(Config);
	}

	template <typename T>
	T& GetMemory() const
	{
		checkf(MemoryStruct == T::StaticStruct(), TEXT("node memory type mismatch"));
		return *static_cast<T*>(Memory);
	}

	float GetDelta() const { return Delta; }
	FADSKActivationHandle GetHandle() const { return Handle; }

	FName GetNodeId() const { return NodeId; }
	UObject* GetHost() const { return Host.Get(); }
	template <typename T> T* GetHost() const { return Cast<T>(Host.Get()); }

	/** Reseeded on every Start. Draw only from callbacks; never keep the reference. */
	FRandomStream& GetRandom() const
	{
		checkf(Random, TEXT("node context has no random stream"));
		return *Random;
	}

	/** Seconds of tick delta since the instance started. */
	double GetTime() const { return Time; }
	/** A composite's child count; 0 for other kinds. */
	int32 GetChildCount() const { return ChildCount; }

	/** Kept across activations, reset when the instance starts. */
	template <typename T>
	T& GetState() const
	{
		checkf(StateStruct == T::StaticStruct() && State, TEXT("node state type mismatch"));
		return *static_cast<T*>(State);
	}

	/** Signals that Start acquired something, so a following failure ends with PartialEntry. */
	void MarkPartiallyEntered() { bPartiallyEntered = true; }
	bool IsPartiallyEntered() const { return bPartiallyEntered; }

	/** False when the index, direction, type or slot binding is wrong. */
	bool ReadPort(int32 PortIndex, FADSKValue& OutValue) const;
	bool ReadBool(int32 PortIndex, bool& bOutValue) const;
	bool ReadFloat(int32 PortIndex, float& OutValue) const;
	bool ReadVector(int32 PortIndex, FVector& OutValue) const;
	bool ReadInt(int32 PortIndex, int32& OutValue) const;
	/** The opaque id of an Object port; 0 is `none`. The host resolves it. */
	bool ReadObject(int32 PortIndex, uint64& OutId) const;

	/** False when the index, direction, exact value type or slot binding is wrong. */
	bool WritePort(int32 PortIndex, const FADSKValue& Value);
	bool WriteInt(int32 PortIndex, int32 Value);

	/** The node's `<Param>` values in document order, shaped for EnqueueEvent; empty unless bTakesArgs. */
	TArray<FADSKNamedValue> ReadArgs() const;

private:
	FADSKValue* ResolvePort(int32 PortIndex, EADSKPortDirection Direction, EADSKValueType Type) const;

	const UScriptStruct* ConfigStruct;
	const void* Config;
	const UScriptStruct* MemoryStruct;
	void* Memory;
	TArrayView<const FADSKPortDescriptor> Ports;
	TArrayView<const int32> PortSlots;
	TArrayView<const FADSKNodeArg> Args;
	TArrayView<FADSKValue> Slots;
	float Delta;
	FADSKActivationHandle Handle;
	FName NodeId;
	TWeakObjectPtr<UObject> Host;
	FRandomStream* Random = nullptr;
	double Time = 0.0;
	int32 ChildCount = 0;
	const UScriptStruct* StateStruct = nullptr;
	void* State = nullptr;
	IADSKSlotWriteSink* SlotSink = nullptr;
	int32 Node = INDEX_NONE;
	bool bPartiallyEntered = false;
};

struct FADSKCompositeStep
{
	enum class EAction : uint8 { StartChild, Succeed, Fail };
	EAction Action = EAction::Fail;
	int32 Child = INDEX_NONE;

	static FADSKCompositeStep Start(int32 InChild) { return {EAction::StartChild, InChild}; }
	static FADSKCompositeStep Succeed() { return {EAction::Succeed}; }
	static FADSKCompositeStep Fail() { return {EAction::Fail}; }
	static FADSKCompositeStep Finish(EADSKNodeStatus Status) { return Status == EADSKNodeStatus::Succeeded ? Succeed() : Fail(); }
};

using FADSKStartFn = TFunction<EADSKNodeStatus(FADSKNodeContext&)>;
using FADSKUpdateFn = TFunction<EADSKNodeStatus(FADSKNodeContext&, float)>;
using FADSKEndFn = TFunction<void(FADSKNodeContext&, EADSKEndReason)>;
using FADSKCompositeStartFn = TFunction<FADSKCompositeStep(FADSKNodeContext&)>;
/** The child that finished (its index) and its result. Starting a child out of range fails the composite. */
using FADSKChildFinishedFn = TFunction<FADSKCompositeStep(FADSKNodeContext&, int32, EADSKNodeStatus)>;
/** A node's utility for a UtilitySelector; the context has no memory, as the node is not running. */
using FADSKScoreFn = TFunction<float(FADSKNodeContext&)>;
/** Whether the node, started now, would get going; the context has state but no memory, and the call changes nothing. */
using FADSKCheckFn = TFunction<bool(FADSKNodeContext&)>;
/** A sensor's reading: writes its out ports from the host and its in ports; the context has no memory or state. */
using FADSKSampleFn = TFunction<void(FADSKNodeContext&)>;
/** Tick seconds until OnUpdate or OnCheck is next needed; NeverWake when untimed. Never answer late. */
using FADSKNextWakeFn = TFunction<float(FADSKNodeContext&)>;

namespace ADSK
{
	/** FADSKNextWakeFn's "not by time": an instance with nothing else due sleeps until a slot write, event or result. */
	inline constexpr float NeverWake = TNumericLimits<float>::Max();
}
/** Receives the descriptor's configuration struct memory after every mapped field converted. */
using FADSKValidateConfigFn = TFunction<bool(const void*)>;
/** Once ObjectPath fields resolve: false with a message when the asset cannot serve this configuration. */
using FADSKValidateAssetsFn = TFunction<bool(const void*, FString&)>;

/** Registered by value; the registry keeps a heap-stable copy. */
struct ADAMSKAI_API FADSKNodeDescriptor
{
	/** Case-sensitive XML element name. An FString: FName would fold case. */
	FString Name;
	int32 SchemaVersion = 1;
	EADSKNodeKind Kind = EADSKNodeKind::Leaf;
	/** One or two catalogue sentences: what it does, when it succeeds and fails. Not part of the fingerprint. */
	FString Description;
	/** Optional author advice, one point per line, listed under the node in the catalogue. */
	FString Guidance;

	const UScriptStruct* ConfigStruct = nullptr;
	TArray<FADSKFieldMap> Fields;
	TArray<FADSKPortDescriptor> Ports;
	const UScriptStruct* MemoryStruct = nullptr;
	/** Optional per-node state that outlives activations; value-only. */
	const UScriptStruct* StateStruct = nullptr;

	/** Optional `TArray<FADSKNamedParam>` filled from `<Param>` children, which do not count against MaxChildren. */
	FName ParamsProperty;
	/** Takes `<Param name from/>` of any non-enum slot, read with FADSKNodeContext::ReadArgs. */
	bool bTakesArgs = false;

	int32 MinChildren = 0;
	int32 MaxChildren = 0;
	/** The validator then expects no tree.failure from this node. */
	bool bNeverFails = false;
	/** Selector only. Anything but Ordered requires that policy's memory struct. */
	EADSKSelectorPolicy SelectorPolicy = EADSKSelectorPolicy::Ordered;
	/** Callbacks touch only their context, never the host, so FADSKParallelTick may run the node on a worker. */
	bool bThreadSafe = false;

	/** Runs inside the compiler, which may be on a worker thread: it must only read the config it is given. */
	FADSKValidateConfigFn ValidateConfig;
	FADSKValidateAssetsFn ValidateAssets;
	FADSKStartFn OnStart;
	FADSKUpdateFn OnUpdate;
	FADSKEndFn OnEnd;
	/** Composite only, both required. */
	FADSKCompositeStartFn OnCompositeStart;
	FADSKChildFinishedFn OnChildFinished;
	/** Called without starting the node. */
	FADSKScoreFn OnScore;
	/** Optional: a ReactiveSelector checks earlier options with it every tick; true interrupts the running one. */
	FADSKCheckFn OnCheck;
	/** Optional: when the node is next due. Without it an OnUpdate or OnCheck node keeps its brain awake. */
	FADSKNextWakeFn OnNextWake;
	/** Sensor only, required. */
	FADSKSampleFn OnSample;

	/** Exact-case lookup; null when absent. */
	const FADSKFieldMap* FindField(FStringView XmlName) const;

	/** Exact-case lookup; INDEX_NONE when absent. */
	int32 FindPort(FStringView PortName) const;
};
