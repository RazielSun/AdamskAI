// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Catalogue/ADSKRegistryAssembly.h"

#include "Nodes/ADSKBuiltinNodes.h"
#include "Nodes/ADSKHostNodes.h"
#include "Catalogue/ADSKNodeProvider.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Features/IModularFeatures.h"
#include "ADSKHostModule.h"

namespace ADSKAssemblyPrivate
{
	TSharedPtr<const FADSKNodeRegistry> Assembled;
	bool bAttempted = false;
	bool bStale = false;
	uint32 Generation = 0;
	TArray<FString> AttemptErrors;

	TSharedPtr<const FADSKNodeRegistry> Assemble(TConstArrayView<IADSKNodeProvider*> Providers, TArray<FString>& Errors)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		FADSKHostNodes::RegisterAll(*Registry);
		for (const FString& Rejection : Registry->GetRejections()) { Errors.Add(TEXT("AdamskAI: ") + Rejection); }
		if (!Errors.IsEmpty()) { return nullptr; }
		TArray<IADSKNodeProvider*> Sorted(Providers);
		if (Sorted.Contains(nullptr))
		{
			Errors.Add(TEXT("provider: null provider"));
			return nullptr;
		}
		Sorted.Sort([](const IADSKNodeProvider& A, const IADSKNodeProvider& B) { return A.GetProviderName().Compare(B.GetProviderName(), ESearchCase::CaseSensitive) < 0; });
		FString Previous;
		for (IADSKNodeProvider* Provider : Sorted)
		{
			if (Provider->GetProviderName().IsEmpty() || Provider->GetProviderName() == Previous)
			{
				Errors.Add(TEXT("provider: empty or duplicate provider name"));
				return nullptr;
			}
			Previous = Provider->GetProviderName();
		}
		for (IADSKNodeProvider* Provider : Sorted)
		{
			TArray<FString> ProviderErrors;
			const int32 RejectedBefore = Registry->GetRejections().Num();
			Provider->RegisterNodes(*Registry, ProviderErrors);
			// A refusal the provider did not report itself still fails assembly.
			for (const FString& Rejection : Registry->GetRejections().RightChop(RejectedBefore))
			{
				ProviderErrors.AddUnique(Rejection);
			}
			for (const FString& Error : ProviderErrors) { Errors.Add(Provider->GetProviderName() + TEXT(": ") + Error); }
			if (!ProviderErrors.IsEmpty()) { return nullptr; }
		}
		Registry->Freeze();
		return Registry;
	}
}

TSharedPtr<const FADSKNodeRegistry> FADSKRegistryAssembly::TryGet(TArray<FString>& OutErrors)
{
	check(IsInGameThread());
	if (ADSKAssemblyPrivate::bAttempted && !ADSKAssemblyPrivate::bStale)
	{
		OutErrors.Append(ADSKAssemblyPrivate::AttemptErrors);
		return ADSKAssemblyPrivate::Assembled;
	}
	if (ADSKAssemblyPrivate::bStale)
	{
		UE_LOG(LogAdamskAIHost, Display, TEXT("AdamskAI node providers changed; assembling the catalogue again"));
	}
	ADSKAssemblyPrivate::bAttempted = true;
	ADSKAssemblyPrivate::bStale = false;
	++ADSKAssemblyPrivate::Generation;
	ADSKAssemblyPrivate::AttemptErrors.Reset();
	TArray<IADSKNodeProvider*> Providers;
	{
		IModularFeatures::FScopedLockModularFeatureList Lock;
		Providers = IModularFeatures::Get().GetModularFeatureImplementations<IADSKNodeProvider>(IADSKNodeProvider::GetFeatureName());
	}
	ADSKAssemblyPrivate::Assembled = ADSKAssemblyPrivate::Assemble(Providers, ADSKAssemblyPrivate::AttemptErrors);
	OutErrors.Append(ADSKAssemblyPrivate::AttemptErrors);
	return ADSKAssemblyPrivate::Assembled;
}

TSharedPtr<const FADSKNodeRegistry> FADSKRegistryAssembly::AssembleIsolated(TConstArrayView<IADSKNodeProvider*> Providers, TArray<FString>& OutErrors)
{
	check(IsInGameThread());
	return ADSKAssemblyPrivate::Assemble(Providers, OutErrors);
}

TSharedPtr<const FADSKNodeRegistry> FADSKRegistryAssembly::BuildPluginCatalogue(TArray<FString>& OutErrors)
{
	return AssembleIsolated({}, OutErrors);
}

bool FADSKRegistryAssembly::ReleaseAssembled()
{
	if (UADSKWorldSubsystem::GetLiveCount() != 0) { return false; }
	if (ADSKAssemblyPrivate::Assembled.IsValid() && !ADSKAssemblyPrivate::Assembled.IsUnique()) { return false; }
	ADSKAssemblyPrivate::Assembled.Reset();
	ADSKAssemblyPrivate::AttemptErrors.Reset();
	ADSKAssemblyPrivate::bAttempted = false;
	ADSKAssemblyPrivate::bStale = false;
	return true;
}

void FADSKRegistryAssembly::MarkProvidersChanged()
{
	ADSKAssemblyPrivate::bStale = ADSKAssemblyPrivate::bAttempted;
}

uint32 FADSKRegistryAssembly::GetGeneration()
{
	return ADSKAssemblyPrivate::Generation;
}

bool FADSKRegistryAssembly::HasAttempted()
{
	return ADSKAssemblyPrivate::bAttempted;
}
