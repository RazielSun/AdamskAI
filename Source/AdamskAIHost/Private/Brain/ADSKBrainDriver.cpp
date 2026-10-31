// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Brain/ADSKBrainDriver.h"

#include "ADSKHostModule.h"
#include "Brain/ADSKBrainComponent.h"
#include "Program/ADSKDefinition.h"

EADSKInputResult FADSKInputRef::Set(UADSKBrainComponent& Brain, const FADSKValue& Value)
{
	if (!Brain.GetInstance()) return EADSKInputResult::NotRunning;
	if (Id.Definition != Brain.GetDefinition()->GetSerial())
	{
		Id = Brain.FindInput(Name);
	}
	return Id.IsValid() ? Brain.SetInput(Id, Value) : Brain.SetInput(Name, Value);
}

bool FADSKBrainDriver::Begin(UADSKBrainComponent& InBrain, FStringView BehaviorFile, FPublish InPublish)
{
	Brain = &InBrain;
	Publish = MoveTemp(InPublish);
	if (!InBrain.InitializeFromSource(BehaviorFile))
	{
		UE_LOG(LogAdamskAIHost, Warning, TEXT("%s: cannot load '%.*s': %s"), *InBrain.GetPathName(), BehaviorFile.Len(), BehaviorFile.GetData(), *InBrain.GetLastError());
		return false;
	}
	if (Publish) Publish(InBrain);
	InBrain.StartLogic();
	// StartLogic already logs a missing required input.
	return InBrain.IsADSKRunning();
}

void FADSKBrainDriver::Tick(float DeltaSeconds)
{
	UADSKBrainComponent* Current = Brain.Get();
	if (!Current || !Current->IsRunning()) return;
	if (Publish) Publish(*Current);
	Current->TickBrain(DeltaSeconds);
}

void FADSKBrainDriver::End(const TCHAR* Reason)
{
	UADSKBrainComponent* Current = Brain.Get();
	if (Current && Current->IsRunning()) Current->StopLogic(Reason);
}
