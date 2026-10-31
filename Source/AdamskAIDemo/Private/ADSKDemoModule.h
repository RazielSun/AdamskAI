// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAdamskAIDemo, Log, All);

class FADSKDemoModule final : public IModuleInterface
{
public:
	/** The behavior mount of Plugins/AdamskAI/Demo. */
	static const TCHAR* Mount;

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	bool bMounted = false;
};
