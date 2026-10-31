// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "ADSKGameplayDebuggerCategory.h"
#include "Modules/ModuleManager.h"

#if WITH_GAMEPLAY_DEBUGGER_MENU
#include "GameplayDebugger.h"
#endif

class FADSKDebugModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_GAMEPLAY_DEBUGGER_MENU
		IGameplayDebugger& Debugger = IGameplayDebugger::Get();
		Debugger.RegisterCategory(TEXT("AdamskAI"), IGameplayDebugger::FOnGetCategory::CreateStatic(&FADSKGameplayDebuggerCategory::MakeInstance),
			EGameplayDebuggerCategoryState::EnabledInGameAndSimulate);
		Debugger.NotifyCategoriesChanged();
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_GAMEPLAY_DEBUGGER_MENU
		if (IGameplayDebugger::IsAvailable())
		{
			IGameplayDebugger::Get().UnregisterCategory(TEXT("AdamskAI"));
			IGameplayDebugger::Get().NotifyCategoriesChanged();
		}
#endif
	}
};

IMPLEMENT_MODULE(FADSKDebugModule, AdamskAIDebug)
