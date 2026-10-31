// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Brain/ADSKAsyncOps.h"

bool FADSKAsyncOps::Begin(FADSKActivationHandle Handle, FName Kind, FName NodeId, double Now)
{
	ADSK_CHECK_GAME_THREAD();
	if (!ensureMsgf(Handle.IsValid(), TEXT("async op %s@%s began with an invalid handle"), *Kind.ToString(), *NodeId.ToString())
		|| !ensureMsgf(!Find(Handle), TEXT("async op %s@%s began twice for one activation"), *Kind.ToString(), *NodeId.ToString()))
	{
		return false;
	}
	FADSKAsyncOp& Op = Ops.AddDefaulted_GetRef();
	Op.Handle = Handle;
	Op.Kind = Kind;
	Op.NodeId = NodeId;
	Op.StartTime = Now;
	return true;
}

bool FADSKAsyncOps::SetCancel(FADSKActivationHandle Handle, TFunction<void()> Cancel)
{
	ADSK_CHECK_GAME_THREAD();
	FADSKAsyncOp* Op = Find(Handle);
	if (!Op || Op->Result.IsSet()) { return false; }
	Op->Cancel = MoveTemp(Cancel);
	return true;
}

bool FADSKAsyncOps::IsPending(FADSKActivationHandle Handle) const
{
	ADSK_CHECK_GAME_THREAD();
	const FADSKAsyncOp* Op = Find(Handle);
	return Op && !Op->Result.IsSet();
}

FADSKAsyncOp* FADSKAsyncOps::Find(FADSKActivationHandle Handle)
{
	ADSK_CHECK_GAME_THREAD();
	return Ops.FindByPredicate([Handle](const FADSKAsyncOp& Op) { return Op.Handle == Handle; });
}

const FADSKAsyncOp* FADSKAsyncOps::Find(FADSKActivationHandle Handle) const
{
	ADSK_CHECK_GAME_THREAD();
	return Ops.FindByPredicate([Handle](const FADSKAsyncOp& Op) { return Op.Handle == Handle; });
}

FADSKAsyncOp* FADSKAsyncOps::FindByMove(FAIRequestID RequestId)
{
	ADSK_CHECK_GAME_THREAD();
	if (!RequestId.IsValid()) { return nullptr; }
	return Ops.FindByPredicate([RequestId](const FADSKAsyncOp& Op) { return Op.MoveRequest == RequestId; });
}

void FADSKAsyncOps::End(FADSKNodeContext& Context, EADSKEndReason Reason, int32 PayloadPort)
{
	ADSK_CHECK_GAME_THREAD();
	const int32 Index = Ops.IndexOfByPredicate([Handle = Context.GetHandle()](const FADSKAsyncOp& Op) { return Op.Handle == Handle; });
	if (Index == INDEX_NONE) { return; }
	FADSKAsyncOp Op = MoveTemp(Ops[Index]);
	Ops.RemoveAt(Index);
	if (Reason == EADSKEndReason::Succeeded && PayloadPort != INDEX_NONE && Op.Payload.IsSet())
	{
		ensureMsgf(Context.WritePort(PayloadPort, Op.Payload.GetValue()), TEXT("async op %s@%s could not write its payload port"), *Op.Kind.ToString(), *Op.NodeId.ToString());
	}
	// A start that returned Succeeded without completing has nothing left to cancel; a failed one may (partial entry).
	if (!Op.Result.IsSet() && Reason != EADSKEndReason::Succeeded && Op.Cancel) { Op.Cancel(); }
}

int32 FADSKAsyncOps::CancelAll()
{
	ADSK_CHECK_GAME_THREAD();
	TArray<FADSKAsyncOp> Detached = MoveTemp(Ops);
	Ops.Reset();
	for (FADSKAsyncOp& Op : Detached)
	{
		if (!Op.Result.IsSet() && Op.Cancel) { Op.Cancel(); }
	}
	return Detached.Num();
}
