// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "ADSKHostModule.h"
#include "Debug/ADSKConsoleCommands.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAdamskAIHost);

class FAdamskAIHostModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RegistrationHandle = IModularFeatures::Get().OnModularFeatureRegistered().AddRaw(this, &FAdamskAIHostModule::OnFeatureChanged);
		UnregistrationHandle = IModularFeatures::Get().OnModularFeatureUnregistered().AddRaw(this, &FAdamskAIHostModule::OnFeatureChanged);
		ConsoleCommands.Register();
	}
	virtual void ShutdownModule() override
	{
		ConsoleCommands.Unregister();
		IModularFeatures::Get().OnModularFeatureRegistered().Remove(RegistrationHandle);
		IModularFeatures::Get().OnModularFeatureUnregistered().Remove(UnregistrationHandle);
		if (!FADSKRegistryAssembly::ReleaseAssembled())
		{
			UE_LOG(LogAdamskAIHost, Error, TEXT("AdamskAI assembled catalogue outlived host shutdown"));
			ensureMsgf(false, TEXT("AdamskAI assembled catalogue outlived host shutdown"));
		}
	}
private:
	void OnFeatureChanged(const FName& Type, IModularFeature*)
	{
		if (Type == TEXT("ADSKNodeProvider")) FADSKRegistryAssembly::MarkProvidersChanged();
	}
	FDelegateHandle RegistrationHandle;
	FDelegateHandle UnregistrationHandle;
	FADSKConsoleCommands ConsoleCommands;
};

IMPLEMENT_MODULE(FAdamskAIHostModule, AdamskAIHost)
