// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Core/ADSKTypes.h"
#include "Runtime/ADSKNode.h"
#include "AITypes.h"
#include "CoreMinimal.h"

/** Cancel must not retain FADSKNodeContext, activation-memory pointers or raw UObjects; capture UObjects weakly. */
struct FADSKAsyncOp
{
	FADSKActivationHandle Handle;
	FName Kind;
	FName NodeId;
	double StartTime = 0.0;
	TFunction<void()> Cancel;
	FAIRequestID MoveRequest;
	TOptional<FADSKValue> Payload;
	TOptional<EADSKNodeStatus> Result;
};

enum class EADSKAsyncCompletion : uint8
{
	Accepted,
	Stale,
	QueueFull,
	NotRunning,
};

/** Brain-owned table of live async operations, keyed by activation handle. Game-thread only. */
class ADAMSKAIHOST_API FADSKAsyncOps
{
public:
	/** False, with a non-Shipping ensure, for an invalid or already present handle. */
	bool Begin(FADSKActivationHandle Handle, FName Kind, FName NodeId, double Now);
	/** Succeeds only for an existing entry that has no accepted result. */
	bool SetCancel(FADSKActivationHandle Handle, TFunction<void()> Cancel);
	/** Present with no accepted result. */
	bool IsPending(FADSKActivationHandle Handle) const;
	FADSKAsyncOp* Find(FADSKActivationHandle Handle);
	const FADSKAsyncOp* Find(FADSKActivationHandle Handle) const;
	FADSKAsyncOp* FindByMove(FAIRequestID RequestId);
	/**
	 * Removes the entry before any payload write or cancellation, so a synchronous completion from Cancel finds
	 * nothing. On success writes the stored payload to PayloadPort; cancels only while no result was accepted.
	 */
	void End(FADSKNodeContext& Context, EADSKEndReason Reason, int32 PayloadPort = INDEX_NONE);
	/** Detaches every entry, cancels the unfinished ones and returns the detached count. */
	int32 CancelAll();
	TConstArrayView<FADSKAsyncOp> GetOps() const { return Ops; }

private:
	TArray<FADSKAsyncOp> Ops;
};
