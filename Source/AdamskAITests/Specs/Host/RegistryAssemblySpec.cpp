// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Runtime/ADSKNode.h"
#include "Catalogue/ADSKNodeProvider.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "CQTest.h"

namespace FRegistryAssemblySpecPrivate
{
	class FProvider final : public IADSKNodeProvider
	{
	public:
		FProvider(FString InName, FString InNode, TArray<FString>* InOrder = nullptr, bool bInReport = true)
			: Name(MoveTemp(InName)), Node(MoveTemp(InNode)), Order(InOrder), bReport(bInReport) {}
		virtual FString GetProviderName() const override { return Name; }
		virtual void RegisterNodes(FADSKNodeRegistry& Registry, TArray<FString>& OutErrors) override
		{
			if (Order) Order->Add(Name);
			if (Node.IsEmpty()) { OutErrors.Add(TEXT("deliberate error")); return; }
			FADSKNodeDescriptor Descriptor;
			Descriptor.Name = Node;
			Descriptor.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
			const EADSKRegisterResult Result = Registry.Register(MoveTemp(Descriptor));
			if (bReport && Result != EADSKRegisterResult::Accepted) OutErrors.Add(LexToString(Result));
		}
	private:
		FString Name;
		FString Node;
		TArray<FString>* Order;
		bool bReport;
	};
}

using namespace FRegistryAssemblySpecPrivate;

TEST_CLASS_WITH_FLAGS(FRegistryAssemblySpec, "AdamskAI.Host.RegistryAssembly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(SortsProvidersAndBuildsFrozenCatalogue)
	{
		TArray<FString> Order;
		FProvider Zulu(TEXT("Zulu"), TEXT("ZuluLeaf"), &Order);
		FProvider Alpha(TEXT("Alpha"), TEXT("AlphaLeaf"), &Order);
		TArray<IADSKNodeProvider*> Providers{&Zulu, &Alpha};
		TArray<FString> Errors;
		const TSharedPtr<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::AssembleIsolated(Providers, Errors);
		ASSERT_THAT(IsTrue(Registry.IsValid() && Registry->IsFrozen() && Errors.IsEmpty(), TEXT("errors: ") + FString::Join(Errors, TEXT("; "))));
		ASSERT_THAT(AreEqual(TArray<FString>{TEXT("Alpha"), TEXT("Zulu")}, Order));
		ASSERT_THAT(IsNotNull(Registry->Find(TEXT("Wait"))));
		ASSERT_THAT(IsNotNull(Registry->Find(TEXT("MoveTo"))));
		ASSERT_THAT(IsNotNull(Registry->Find(TEXT("LookAt"))));
	}

	TEST_METHOD(RejectsNamesBeforeCallingProviders)
	{
		TArray<FString> Order;
		FProvider First(TEXT("Same"), TEXT("FirstLeaf"), &Order);
		FProvider Second(TEXT("Same"), TEXT("SecondLeaf"), &Order);
		TArray<IADSKNodeProvider*> Providers{&First, &Second};
		TArray<FString> Errors;
		ASSERT_THAT(IsFalse(FADSKRegistryAssembly::AssembleIsolated(Providers, Errors).IsValid(), TEXT("duplicate providers assembled")));
		ASSERT_THAT(IsTrue(Order.IsEmpty(), TEXT("registered: ") + FString::Join(Order, TEXT(", "))));
		ASSERT_THAT(IsFalse(Errors.IsEmpty(), TEXT("no error for the duplicate provider")));
	}

	TEST_METHOD(PrefixesProviderErrorsAndFingerprintIsOrderIndependent)
	{
		FProvider Bad(TEXT("Broken"), TEXT(""));
		TArray<IADSKNodeProvider*> BadProviders{&Bad};
		TArray<FString> Errors;
		ASSERT_THAT(IsFalse(FADSKRegistryAssembly::AssembleIsolated(BadProviders, Errors).IsValid(), TEXT("a failing provider assembled")));
		ASSERT_THAT(AreEqual(1, Errors.Num(), TEXT("errors: ") + FString::Join(Errors, TEXT("; "))));
		ASSERT_THAT(AreEqual(FString(TEXT("Broken: deliberate error")), Errors[0]));
		FProvider A(TEXT("A"), TEXT("ALeaf"));
		FProvider B(TEXT("B"), TEXT("BLeaf"));
		TArray<IADSKNodeProvider*> AB{&A, &B};
		TArray<IADSKNodeProvider*> BA{&B, &A};
		Errors.Reset(); const TSharedPtr<const FADSKNodeRegistry> One = FADSKRegistryAssembly::AssembleIsolated(AB, Errors);
		Errors.Reset(); const TSharedPtr<const FADSKNodeRegistry> Two = FADSKRegistryAssembly::AssembleIsolated(BA, Errors);
		ASSERT_THAT(IsTrue(One.IsValid() && Two.IsValid(), TEXT("the providers did not assemble")));
		ASSERT_THAT(AreEqual(One->GetCatalogueFingerprint(), Two->GetCatalogueFingerprint()));
	}

	TEST_METHOD(FailsOnARefusalTheProviderIgnored)
	{
		FProvider Silent(TEXT("Silent"), TEXT("Wait"), nullptr, false);
		TArray<IADSKNodeProvider*> Providers{&Silent};
		TArray<FString> Errors;
		ASSERT_THAT(IsFalse(FADSKRegistryAssembly::AssembleIsolated(Providers, Errors).IsValid(), TEXT("a silently refused node assembled")));
		ASSERT_THAT(AreEqual(1, Errors.Num(), TEXT("errors: ") + FString::Join(Errors, TEXT("; "))));
		ASSERT_THAT(IsTrue(Errors[0].StartsWith(TEXT("Silent: Wait: ")), Errors[0]));
	}
};

#endif
