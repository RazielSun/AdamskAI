// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FADSKNodeRegistry;

class FAdamskAIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Valid only between StartupModule and ShutdownModule (checked). */
	TSharedRef<const FADSKNodeRegistry> GetRegistry() const;

private:
	TSharedPtr<FADSKNodeRegistry> Registry;
};
