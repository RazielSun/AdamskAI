// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Runtime/ADSKInstance.h"
#include "Compiler/ADSKCompiler.h"
#include "Nodes/ADSKBuiltinNodes.h"

/** A slice of the instance arena. Move-only; the stack relocates it bitwise, which a pointer pair survives. */
struct FADSKNodeMemory
{
	FADSKNodeMemory() = default;
	FADSKNodeMemory(const FADSKNodeMemory&) = delete;
	FADSKNodeMemory& operator=(const FADSKNodeMemory&) = delete;
	FADSKNodeMemory(FADSKNodeMemory&& Other) : Struct(Other.Struct), Data(Other.Data) { Other.Struct = nullptr; Other.Data = nullptr; }
	FADSKNodeMemory& operator=(FADSKNodeMemory&& Other)
	{
		if (this != &Other)
		{
			Reset();
			Struct = Other.Struct;
			Data = Other.Data;
			Other.Struct = nullptr;
			Other.Data = nullptr;
		}
		return *this;
	}
	~FADSKNodeMemory() { Reset(); }

	void Initialize(const UScriptStruct* InStruct, uint8* InData)
	{
		Reset();
		Struct = InStruct;
		Data = InData;
		Struct->InitializeStruct(Data);
	}
	void Reset()
	{
		if (Struct && Data) Struct->DestroyStruct(Data);
		Struct = nullptr;
		Data = nullptr;
	}
	const UScriptStruct* GetScriptStruct() const { return Struct; }
	uint8* GetMutableMemory() const { return Data; }

private:
	const UScriptStruct* Struct = nullptr;
	uint8* Data = nullptr;
};

struct FADSKActiveNode
{
	int32 Node = INDEX_NONE;
	uint32 ActivationId = 0;
	FADSKNodeMemory Memory;
	int32 ChildCursor = 0;
	bool bPartiallyEntered = false;
	uint64 StartedTick = 0;
	int32 BackgroundLane = INDEX_NONE;
	/** A Parallel's child lanes, one per child in document order. */
	TArray<int32, TInlineAllocator<ADSK::MaxParallelChildren>> ChildLanes;
	/** A Guard's last recorded value: guard-value is recorded at the start and then only when it changes. */
	FADSKValue GuardValue;
};

/**
 * Each active SimpleParallel owns a background lane for its second child, which restarts the tick after it
 * finishes and is cancelled with its parallel.
 */
struct FADSKLane
{
	int32 Id = 0;
	TArray<FADSKActiveNode, TInlineAllocator<FADSKCompiler::MaxTreeDepth>> Stack;
	TOptional<int32> DeferredNode;
	bool bDeferredRecorded = false;
	/** Background lanes only; RestartTick is 0 while the lane runs. */
	int32 Root = INDEX_NONE;
	uint64 RestartTick = 0;
	/** A Parallel's child lane: the lane and activation of that Parallel, and which child this lane runs. */
	int32 OwnerLane = INDEX_NONE;
	uint32 OwnerActivation = 0;
	int32 ChildIndex = INDEX_NONE;
};

class FADSKProcedure
{
public:
	explicit FADSKProcedure(FADSKInstance::FImpl& InOwner);

	void ResetForGeneration();
	void ResetBudget();
	void Start(int32 RootNode, int32 OwnerState, uint64 OwnerToken);
	void ApplyResult(FADSKActivationHandle Handle, EADSKNodeStatus Status);
	void Step(float Delta);
	void Cancel();

	/** The main lane's running leaf. */
	FADSKActivationHandle GetDeepestLeafHandle() const;
	int32 GetDeepestNodeIndex() const { return Main().Stack.IsEmpty() ? INDEX_NONE : Main().Stack.Last().Node; }
	/** Main lane first, then background lanes in start order: the deepest node of each nonempty lane. */
	void GetDeepestNodes(TArray<int32>& OutNodes, TArray<FADSKActivationHandle>& OutLeafHandles) const;
	/** The running leaf of any lane. */
	bool IsCurrentRunningLeaf(FADSKActivationHandle Handle) const;
	bool HasDeferredContinuation() const { return Main().DeferredNode.IsSet(); }
	/**
	 * Seconds of tick time until some lane needs a step that time alone causes: 0 for a pending terminal, a deferred
	 * start, a background lane restarting, a running node with OnUpdate and no OnNextWake, or an option checked
	 * without one; otherwise the nearest OnNextWake or utility re-score. ADSK::NeverWake when nothing is timed.
	 */
	float GetNextWake();

private:
	friend struct FADSKInstance::FImpl;

	enum class EStartOutcome : uint8 { Started, Deferred };

	/** Makes a lane current for the scope; every stack operation acts on the current lane. */
	struct FLaneScope
	{
		FLaneScope(FADSKProcedure& InOwner, FADSKLane& InLane) : Owner(InOwner), Saved(InOwner.Lane) { Owner.Lane = &InLane; }
		~FLaneScope() { Owner.Lane = Saved; }
		FLaneScope(const FLaneScope&) = delete;
		FLaneScope& operator=(const FLaneScope&) = delete;
		FADSKProcedure& Owner;
		FADSKLane* Saved;
	};

	FADSKLane& Main() { return *Lanes[0]; }
	const FADSKLane& Main() const { return *Lanes[0]; }
	FADSKLane* FindLane(int32 Id) const;
	/** The lane whose top is the running leaf Handle names, or null. */
	FADSKLane* FindLeafLane(FADSKActivationHandle Handle) const;
	FADSKActivationHandle TopLeafHandle(const FADSKLane& InLane) const;

	EStartOutcome StartNode(int32 Node);
	void StartGuard(FADSKActiveNode& Active, int32 NodeIndex, const FADSKNodeDefinition& Node);
	void StartLeaf(FADSKActiveNode& Active, const FADSKNodeDefinition& Node);
	void StartScope(FADSKActiveNode& Active, const FADSKNodeDefinition& Node);
	void StartParallel(FADSKActiveNode& Active, const FADSKNodeDefinition& Node);
	/** A Parallel: a lane per child, started in document order while the node is still active. */
	void StartConcurrent(FADSKActiveNode& Active, const FADSKNodeDefinition& Node);
	/** The current lane is a Parallel's child lane that just emptied with Status: counts it, and ends the Parallel when decided. */
	void ReportToParallel(EADSKNodeStatus Status);
	/** A lane's place in its stack of the active node with this activation id; INDEX_NONE when it is gone. */
	static int32 FindActivation(const FADSKLane& InLane, uint32 ActivationId);
	void StartComposite(FADSKActiveNode& Active, const FADSKNodeDefinition& Node);
	void ApplyCompositeStep(const FADSKCompositeStep& Step);
	/** Leaves and scopes run descriptor callbacks; the other kinds are stepped intrinsically. */
	static bool HasCallbacks(const FADSKNodeDefinition& Node);
	void FinishTop(EADSKNodeStatus Status, EADSKEndReason OverrideReason = EADSKEndReason::Succeeded,
		bool bUseOverride = false);
	void Propagate(EADSKNodeStatus Status);
	/** Pops the current lane's top; a SimpleParallel cancels its background lane first, a Parallel its child lanes. */
	void EndTop(EADSKEndReason Reason, EADSKNodeStatus Status);
	void CompleteRoot(EADSKNodeStatus Status);
	/** Steps the current lane: a deferred start, a background restart, wrappers, then its leaf. */
	void StepLane(float Delta);
	/** Outermost first. False when the active path changed or a stop was requested: the leaf must not update this tick. */
	bool UpdateWrappers(float Delta);
	struct FGuardResult
	{
		bool bPass = false;
		FADSKValue Raw = FADSKValue::MakeBool(false);
	};
	FGuardResult EvaluateGuard(const FADSKActiveNode& Active);
	/** A reactive option is ready: a Guard's condition passes, or the node's OnCheck says so; false for any other. */
	bool OptionPasses(const FADSKNodeDefinition& Selector, int32 Child);
	/** A reactive selector at Index: switches to the first earlier option that turned ready. True when it did. */
	bool ReactToEarlierOptions(int32 Index);
	/** One score per option, inline so rescoring never allocates. */
	using FOptionScores = TArray<float, TInlineAllocator<ADSK::MaxReactiveChildren>>;
	FOptionScores ScoreOptions(const FADSKNodeDefinition& Selector);
	/** A reactive or utility selector's memory; the registry admits such a selector only with exactly this memory type. */
	template <typename T>
	static T& SelectorMemory(FADSKActiveNode& Active)
	{
		return *reinterpret_cast<T*>(Active.Memory.GetMutableMemory());
	}
	/** Options scoring above 0, best first, ties in document order. */
	static void OrderByScore(TConstArrayView<float> Scores, FADSKUtilitySelectorMemory& Out);
	void StartUtility(FADSKActiveNode& Active, const FADSKNodeDefinition& Node);
	/** A utility selector at Index whose rescoring is due: switches to a clearly better option. True when it did. */
	bool RescoreUtility(int32 Index);
	void CancelFrom(int32 FirstIndex);
	/** Cancels a background lane's stack, nested lanes included, and moves it to SpareLanes. */
	void CancelLane(int32 Id);
	FADSKNodeContext MakeContext(FADSKActiveNode& Active, float Delta);
	/** The composite's chosen child, or INDEX_NONE when the index is out of range (the composite then fails). */
	int32 CompositeChild(const FADSKNodeDefinition& Node, int32 Index) const;
	const FADSKNodeDefinition& NodeDef(const FADSKActiveNode& Active) const;

	FADSKInstance::FImpl& Owner;
	/** Lanes[0] is the main lane; lanes are heap-stable because callers hold references across starts. */
	TArray<TUniquePtr<FADSKLane>, TInlineAllocator<4>> Lanes;
	/** Cancelled background lanes, empty and kept for the next SimpleParallel start so a restart does not allocate. */
	TArray<TUniquePtr<FADSKLane>, TInlineAllocator<4>> SpareLanes;
	FADSKLane* Lane = nullptr;
	int32 NextLaneId = 1;
	int32 Root = INDEX_NONE;
	int32 OwnerState = INDEX_NONE;
	uint64 OwnerToken = 0;
	uint32 NextActivationId = 1;
	int32 OpsThisTick = 0;
	bool bFinished = true;
	TOptional<EADSKNodeStatus> PendingTerminal;
};
