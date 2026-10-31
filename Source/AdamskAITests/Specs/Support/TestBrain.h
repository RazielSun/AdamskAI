// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Brain/ADSKBrainComponent.h"
#include "CoreMinimal.h"

/** Rooted while in scope and cleaned up on scope exit, including when a failed assertion returns early. */
class FADSKTestBrain
{
public:
	FADSKTestBrain()
		: Brain(NewObject<UADSKBrainComponent>())
	{
		Brain->AddToRoot();
	}

	~FADSKTestBrain()
	{
		Brain->Cleanup();
		Brain->RemoveFromRoot();
	}

	FADSKTestBrain(const FADSKTestBrain&) = delete;
	FADSKTestBrain& operator=(const FADSKTestBrain&) = delete;

	UADSKBrainComponent* Get() const { return Brain; }
	UADSKBrainComponent* operator->() const { return Brain; }
	UADSKBrainComponent& operator*() const { return *Brain; }
	operator UADSKBrainComponent*() const { return Brain; }

private:
	UADSKBrainComponent* Brain;
};
