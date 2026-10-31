// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "ADSKModule.h"

#include "Nodes/ADSKBuiltinNodes.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FAdamskAIModule, AdamskAI)

void FAdamskAIModule::StartupModule()
{
	const TSharedRef<FADSKNodeRegistry> Built = FADSKNodeRegistry::Create();
	FADSKBuiltinNodes::RegisterAll(*Built);
	checkf(Built->Find(TEXT("Sequence")) && Built->Find(TEXT("Selector")) && Built->Find(TEXT("Guard")) && Built->Find(TEXT("Wait")),
		TEXT("AdamskAI built-in nodes failed to register"));
	Built->Freeze();
	Registry = Built;
}

void FAdamskAIModule::ShutdownModule()
{
	Registry.Reset();
}

TSharedRef<const FADSKNodeRegistry> FAdamskAIModule::GetRegistry() const
{
	checkf(Registry.IsValid(), TEXT("AdamskAI default registry used outside the module's lifetime"));
	return Registry.ToSharedRef();
}
