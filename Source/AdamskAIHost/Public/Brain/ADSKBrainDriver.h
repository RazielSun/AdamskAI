// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Runtime/ADSKInstance.h"
#include "UObject/WeakObjectPtr.h"

class UADSKBrainComponent;

/** An Input written every tick by id, re-resolved after a reload; an undeclared name falls back to by-name. */
struct ADAMSKAIHOST_API FADSKInputRef
{
	FADSKInputRef() = default;
	explicit FADSKInputRef(const TCHAR* InName) : Name(InName) {}

	EADSKInputResult Set(UADSKBrainComponent& Brain, const FADSKValue& Value);
	const FString& GetName() const { return Name; }

private:
	FString Name;
	FADSKInputId Id;
};

/** One brain's host contract (docs/setup.md): Begin starts, Tick publishes and advances, End stops. Embed it. */
class ADAMSKAIHOST_API FADSKBrainDriver
{
public:
	using FPublish = TFunction<void(UADSKBrainComponent&)>;

	/** False, with the brain's GetLastError() logged once, when the file does not load or a required input is missing. */
	bool Begin(UADSKBrainComponent& Brain, FStringView BehaviorFile, FPublish InPublish);
	/** Publishes and ticks while the brain runs; does nothing before Begin or after End. */
	void Tick(float DeltaSeconds);
	/** Stops a running brain; call it while the pawn is still possessed, so ending nodes can release it. */
	void End(const TCHAR* Reason = TEXT("Unpossessed"));

	UADSKBrainComponent* GetBrain() const { return Brain.Get(); }

private:
	TWeakObjectPtr<UADSKBrainComponent> Brain;
	FPublish Publish;
};
