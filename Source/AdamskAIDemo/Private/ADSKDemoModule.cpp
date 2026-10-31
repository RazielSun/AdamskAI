// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "ADSKDemoModule.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Sources/ADSKBehaviorRoots.h"

DEFINE_LOG_CATEGORY(LogAdamskAIDemo);

const TCHAR* FADSKDemoModule::Mount = TEXT("AdskDemo");

void FADSKDemoModule::StartupModule()
{
	// The scenes' behavior files live in the plugin, beside Examples/, and are addressed as `@AdskDemo/<file>.xml`.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AdamskAI"));
	if (Plugin.IsValid() && FADSKBehaviorRoots::Register(Mount, FPaths::Combine(Plugin->GetBaseDir(), TEXT("Demo"))))
	{
		bMounted = true;
	}
	else
	{
		UE_LOG(LogAdamskAIDemo, Warning, TEXT("could not mount @%s: the demo scenes will not find their files"), Mount);
	}
}

void FADSKDemoModule::ShutdownModule()
{
	if (bMounted)
	{
		FADSKBehaviorRoots::Unregister(Mount);
	}
}

IMPLEMENT_MODULE(FADSKDemoModule, AdamskAIDemo)
