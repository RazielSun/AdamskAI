// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Runtime/ADSKInstance.h"
#include "UObject/WeakObjectPtr.h"

class UADSKBrainComponent;

/**
 * An Input written every tick: resolved to an id once per definition and again after a reload, so a host writes by
 * id without tracking definition serials. A name the definition does not declare falls back to the by-name write,
 * which warns once (outside Shipping).
 */
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

/**
 * The host contract of docs/setup.md for an actor that owns one brain, so a controller does not repeat it: Begin
 * initializes from the file, publishes the facts and starts; Tick publishes and advances the brain once; End stops it.
 * Publish runs before the start and before every tick. Embed one in the controller (no base class needed).
 */
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
