// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Support/TempTree.h"
#include "Components/ActorTestSpawner.h"
#include "CQTest.h"

namespace FBindingValidatorSpecPrivate
{
	const TCHAR* SourceFile = TEXT("validator.xml");
	const TCHAR* BaseXml = TEXT("<Agent version=\"1\" id=\"validator\"><Inputs><Input name=\"Required\" type=\"Int\" required=\"true\"/></Inputs><Locals/><Events/><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>");

	struct FFixture
	{
		FADSKTempTree Tree;
		FActorTestSpawner Spawner;
		UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		AActor* Actor = &Spawner.SpawnActor<AActor>();
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(Actor);

		FFixture()
		{
			IFileManager::Get().MakeDirectory(*Tree.Root, true);
			Tree.Write(SourceFile, BaseXml);
			TArray<FString> Errors;
			Subsystem->SetTestRoot(Tree.Root);
			Subsystem->SetTestRegistry(FADSKRegistryAssembly::BuildPluginCatalogue(Errors));
			Actor->AddInstanceComponent(Brain);
			Brain->RegisterComponent();
		}

		~FFixture() { Brain->Cleanup(); }
	};
}

using namespace FBindingValidatorSpecPrivate;

TEST_CLASS_WITH_FLAGS(FBindingValidatorSpec, "AdamskAI.Host.BindingValidators",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(RejectsBeforeInitializationButDoesNotRequirePublishedInputs)
	{
		FFixture F;
		UObject* Owner = F.Actor;
		FString Error;
		ASSERT_THAT(IsTrue(F.Brain->AddBindingValidator(Owner, [](const FADSKDefinition&, FString& Out) { Out = TEXT("rejected"); return false; }, Error), Error));
		ASSERT_THAT(IsFalse(F.Brain->InitializeFromSource(SourceFile), TEXT("the rejecting validator let the brain initialize")));
		ASSERT_THAT(AreEqual(FString(TEXT("rejected")), F.Brain->GetLastError()));
		F.Brain->RemoveBindingValidators(Owner);
		ASSERT_THAT(IsTrue(F.Brain->InitializeFromSource(SourceFile), F.Brain->GetLastError()));
	}

	TEST_METHOD(RejectsReloadAndKeepsTheActiveDefinition)
	{
		FFixture F;
		ASSERT_THAT(IsTrue(F.Brain->InitializeFromSource(SourceFile), F.Brain->GetLastError()));
		const TSharedRef<const FADSKDefinition> Active = F.Brain->GetDefinition();
		bool bReject = false;
		UObject* Owner = F.Actor;
		FString Error;
		ASSERT_THAT(IsTrue(F.Brain->AddBindingValidator(Owner, [&bReject](const FADSKDefinition&, FString& Out) { if (!bReject) return true; Out = TEXT("reload rejected"); return false; }, Error), Error));
		bReject = true;
		F.Tree.Write(SourceFile, FString(BaseXml).Replace(TEXT("duration=\"100\""), TEXT("duration=\"50\"")));
		ASSERT_THAT(AreEqual(EADSKReloadResult::Rejected, F.Subsystem->RequestReload(SourceFile, Error)));
		ASSERT_THAT(IsTrue(&F.Brain->GetDefinition().Get() == &Active.Get(), TEXT("the rejected reload replaced the definition")));
	}

	TEST_METHOD(LateRejectionReplacementAndRemovalAreDeterministic)
	{
		FFixture F;
		ASSERT_THAT(IsTrue(F.Brain->InitializeFromSource(SourceFile), F.Brain->GetLastError()));
		UObject* Owner = F.Actor;
		FString Error;
		ASSERT_THAT(IsFalse(F.Brain->AddBindingValidator(Owner, [](const FADSKDefinition&, FString& Out) { Out = TEXT("late rejected"); return false; }, Error), TEXT("a validator that rejects the running definition was added")));
		ASSERT_THAT(IsTrue(F.Brain->RunBindingValidators(F.Brain->GetDefinition().Get(), Error), Error));
		int32 OldCalls = 0;
		int32 NewCalls = 0;
		ASSERT_THAT(IsTrue(F.Brain->AddBindingValidator(Owner, [&OldCalls](const FADSKDefinition&, FString&) { ++OldCalls; return true; }, Error), Error));
		OldCalls = 0;
		ASSERT_THAT(IsTrue(F.Brain->AddBindingValidator(Owner, [&NewCalls](const FADSKDefinition&, FString&) { ++NewCalls; return true; }, Error), Error));
		NewCalls = 0;
		ASSERT_THAT(IsTrue(F.Brain->RunBindingValidators(F.Brain->GetDefinition().Get(), Error), Error));
		ASSERT_THAT(AreEqual(0, OldCalls));
		ASSERT_THAT(AreEqual(1, NewCalls));
		F.Brain->RemoveBindingValidators(Owner);
		F.Brain->RemoveBindingValidators(Owner);
		ASSERT_THAT(IsTrue(F.Brain->RunBindingValidators(F.Brain->GetDefinition().Get(), Error), Error));
	}

	TEST_METHOD(PrunesDestroyedValidatorOwners)
	{
		FFixture F;
		ASSERT_THAT(IsTrue(F.Brain->InitializeFromSource(SourceFile), F.Brain->GetLastError()));
		AActor& Owner = F.Spawner.SpawnActor<AActor>();
		int32 Calls = 0;
		FString Error;
		ASSERT_THAT(IsTrue(F.Brain->AddBindingValidator(&Owner, [&Calls](const FADSKDefinition&, FString&) { ++Calls; return true; }, Error), Error));
		Calls = 0;
		Owner.Destroy();
		ASSERT_THAT(IsTrue(F.Brain->RunBindingValidators(F.Brain->GetDefinition().Get(), Error), Error));
		ASSERT_THAT(AreEqual(0, Calls));
	}
};

#endif
