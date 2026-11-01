// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Brain/ADSKAsyncOps.h"
#include "CoreMinimal.h"
#include "Runtime/ADSKNode.h"

class UADSKBrainComponent;

/** One async activation, safe to copy into completion callbacks and to call after the node or the brain is gone. */
class ADAMSKAIHOST_API FADSKAsyncTicket
{
public:
	FADSKAsyncTicket(UADSKBrainComponent& InBrain, FADSKActivationHandle InHandle);

	/** Posts the result for the next brain tick; a late or repeated call is stale and harmless. */
	EADSKAsyncCompletion Complete(EADSKNodeStatus Status, TOptional<FADSKValue> Payload = {}) const;
	/** Runs once if the node ends before a completion was accepted; capture UObjects weakly. */
	bool SetCancel(TFunction<void()> Cancel) const;

	UADSKBrainComponent* GetBrain() const;
	FADSKActivationHandle GetHandle() const { return Handle; }

private:
	TWeakObjectPtr<UADSKBrainComponent> Brain;
	FADSKActivationHandle Handle;
};

/** Starts the external work: Running when it will Complete later, Succeeded or Failed when it already knows. */
using FADSKAsyncStartFn = TFunction<EADSKNodeStatus(FADSKNodeContext&, const FADSKAsyncTicket&)>;
/** Whether the work can start at all (a controller, a target, an asset); checked before anything is opened. */
using FADSKAsyncCanStartFn = TFunction<bool(FADSKNodeContext&)>;

namespace ADSK
{
	/**
	 * Fills OnStart/OnEnd with the async protocol: CanStart (false fails, no entry), Start once the entry is open;
	 * at the end BeforeEnd (unbind), then the entry closes with its payload or its cancel.
	 */
	ADAMSKAIHOST_API void MakeAsyncLeaf(FADSKNodeDescriptor& Descriptor, FName Kind, FADSKAsyncStartFn Start,
		int32 PayloadPort = INDEX_NONE, FADSKEndFn BeforeEnd = nullptr, FADSKAsyncCanStartFn CanStart = nullptr);
}
