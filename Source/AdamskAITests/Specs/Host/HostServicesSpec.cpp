// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKHostServices.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Components/ActorTestSpawner.h"
#include "Engine/World.h"
#include "Support/FakeHostServices.h"
#include "CQTest.h"

TEST_CLASS_WITH_FLAGS(FHostServicesSpec, "AdamskAI.Host.HostServices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AWorldUsesTheEngineServicesUntilItIsGivenOthers)
	{
		ASSERT_THAT(IsTrue(&IADSKHostServices::Get(nullptr) == &IADSKHostServices::Engine(), TEXT("no world does not get the engine services")));
		FActorTestSpawner Spawner;
		UWorld& World = Spawner.GetWorld();
		ASSERT_THAT(IsTrue(&IADSKHostServices::Get(&World) == &IADSKHostServices::Engine(), TEXT("a plain world does not get the engine services")));
		{
			FADSKScopedFakeHostServices Services(World);
			Services->Time = 12.5;
			Services->Viewers = TArray<FVector>{FVector(1, 2, 3)};
			ASSERT_THAT(IsTrue(IADSKHostServices::Get(&World).GetTimeSeconds(&World) == 12.5, FString::Printf(TEXT("the time is %f"), IADSKHostServices::Get(&World).GetTimeSeconds(&World))));
			TArray<FVector> Viewers;
			IADSKHostServices::Get(&World).GetViewers(&World, Viewers);
			ASSERT_THAT(AreEqual(TArray<FVector>{FVector(1, 2, 3)}, Viewers));
		}
		ASSERT_THAT(IsTrue(&IADSKHostServices::Get(&World) == &IADSKHostServices::Engine(), TEXT("the fake services outlived their scope")));
	}

	/** A test world has no player controller, so the engine reports no view and every managed brain ticks at full rate. */
	TEST_METHOD(TheEngineReportsOneViewerPerPlayerController)
	{
		FActorTestSpawner Spawner;
		TArray<FVector> Viewers = {FVector::ZeroVector};
		IADSKHostServices::Engine().GetViewers(&Spawner.GetWorld(), Viewers);
		ASSERT_THAT(AreEqual(0, Viewers.Num()));
		IADSKHostServices::Engine().GetViewers(nullptr, Viewers);
		ASSERT_THAT(AreEqual(0, Viewers.Num()));
	}
};

#endif
