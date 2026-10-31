// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Nodes/ADSKAsyncLeaf.h"

#include "Brain/ADSKBrainComponent.h"
#include "Engine/World.h"

FADSKAsyncTicket::FADSKAsyncTicket(UADSKBrainComponent& InBrain, FADSKActivationHandle InHandle)
	: Brain(&InBrain)
	, Handle(InHandle)
{
}

EADSKAsyncCompletion FADSKAsyncTicket::Complete(EADSKNodeStatus Status, TOptional<FADSKValue> Payload) const
{
	UADSKBrainComponent* Target = Brain.Get();
	return Target ? Target->CompleteAsync(Handle, Status, MoveTemp(Payload)) : EADSKAsyncCompletion::NotRunning;
}

bool FADSKAsyncTicket::SetCancel(TFunction<void()> Cancel) const
{
	UADSKBrainComponent* Target = Brain.Get();
	return Target && Target->GetAsyncOps().SetCancel(Handle, MoveTemp(Cancel));
}

UADSKBrainComponent* FADSKAsyncTicket::GetBrain() const
{
	return Brain.Get();
}

void ADSK::MakeAsyncLeaf(FADSKNodeDescriptor& Descriptor, FName Kind, FADSKAsyncStartFn Start, int32 PayloadPort, FADSKEndFn BeforeEnd,
	FADSKAsyncCanStartFn CanStart)
{
	Descriptor.OnStart = [Kind, Start = MoveTemp(Start), CanStart = MoveTemp(CanStart)](FADSKNodeContext& Context)
	{
		UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
		if (!Brain || (CanStart && !CanStart(Context))) { return EADSKNodeStatus::Failed; }
		const UWorld* World = Brain->GetWorld();
		if (!Brain->GetAsyncOps().Begin(Context.GetHandle(), Kind, Context.GetNodeId(), World ? World->GetTimeSeconds() : 0.0)) { return EADSKNodeStatus::Failed; }
		Context.MarkPartiallyEntered();
		return Start(Context, FADSKAsyncTicket(*Brain, Context.GetHandle()));
	};
	Descriptor.OnEnd = [PayloadPort, BeforeEnd = MoveTemp(BeforeEnd)](FADSKNodeContext& Context, EADSKEndReason Reason)
	{
		if (BeforeEnd) { BeforeEnd(Context, Reason); }
		if (UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>()) { Brain->GetAsyncOps().End(Context, Reason, PayloadPort); }
	};
}
