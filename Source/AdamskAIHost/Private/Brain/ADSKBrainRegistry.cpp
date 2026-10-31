// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Brain/ADSKBrainRegistry.h"

#include "Brain/ADSKBrainComponent.h"

void FADSKBrainRegistry::Register(const FString& Path, UADSKBrainComponent* Brain)
{
	if (!Brain) return;
	TArray<TWeakObjectPtr<UADSKBrainComponent>>& Brains = BySource.FindOrAdd(Path);
	Brains.RemoveAll([](const TWeakObjectPtr<UADSKBrainComponent>& Item) { return !Item.IsValid(); });
	Brains.AddUnique(Brain);
}

void FADSKBrainRegistry::Unregister(const FString& Path, UADSKBrainComponent* Brain)
{
	if (TArray<TWeakObjectPtr<UADSKBrainComponent>>* Brains = BySource.Find(Path))
	{
		Brains->RemoveAll([Brain](const TWeakObjectPtr<UADSKBrainComponent>& Item) { return !Item.IsValid() || Item.Get() == Brain; });
	}
}

TArray<UADSKBrainComponent*> FADSKBrainRegistry::GetLive(const FString& Path)
{
	TArray<UADSKBrainComponent*> Out;
	if (TArray<TWeakObjectPtr<UADSKBrainComponent>>* Brains = BySource.Find(Path))
	{
		Brains->RemoveAll([](const TWeakObjectPtr<UADSKBrainComponent>& Item) { return !Item.IsValid(); });
		for (const TWeakObjectPtr<UADSKBrainComponent>& Brain : *Brains) Out.Add(Brain.Get());
	}
	return Out;
}

TArray<TWeakObjectPtr<UADSKBrainComponent>> FADSKBrainRegistry::Get(const FString& Path) const
{
	const TArray<TWeakObjectPtr<UADSKBrainComponent>>* Brains = BySource.Find(Path);
	return Brains ? *Brains : TArray<TWeakObjectPtr<UADSKBrainComponent>>();
}

TArray<UADSKBrainComponent*> FADSKBrainRegistry::GetAll() const
{
	TArray<UADSKBrainComponent*> Out;
	for (const TPair<FString, TArray<TWeakObjectPtr<UADSKBrainComponent>>>& Pair : BySource)
	{
		for (const TWeakObjectPtr<UADSKBrainComponent>& Brain : Pair.Value) if (Brain.IsValid()) Out.Add(Brain.Get());
	}
	return Out;
}
