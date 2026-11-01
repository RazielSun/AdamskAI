// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Settings/ADSKSettings.h"
#include "Components/ActorTestSpawner.h"
#include "CQTest.h"
#include "Support/TestChecks.h"

// A brain skips ticks while nothing is due (UADSKSettings::bSleepWhenIdle) and wakes for inputs, events and starts.
namespace FSleepSpecPrivate
{
	const TCHAR* Xml = TEXT(
		"<Agent version=\"1\" id=\"sleep\">"
		"<Inputs><Input name=\"Alert\" type=\"Bool\" default=\"false\"/></Inputs>"
		"<Events><Event name=\"AlertChanged\" change=\"Alert\"/><Event name=\"Poke\"/></Events>"
		"<StateMachine id=\"m\" initial=\"wait\">"
		"<State id=\"wait\" tree=\"wait\"><On event=\"AlertChanged\" goto=\"alert\" if=\"Alert\"/><On event=\"Poke\" goto=\"poked\"/><On event=\"tree.success\" goto=\"done\"/></State>"
		"<State id=\"alert\"/><State id=\"poked\"/><State id=\"done\"/>"
		"</StateMachine>"
		"<Tree id=\"wait\"><Wait id=\"w\" duration=\"1\"/></Tree></Agent>");

	struct FFixture
	{
		FActorTestSpawner Spawner;
		bool bSavedSleep = GetDefault<UADSKSettings>()->bSleepWhenIdle;

		explicit FFixture(bool bSleep = true) { GetMutableDefault<UADSKSettings>()->bSleepWhenIdle = bSleep; }
		~FFixture() { GetMutableDefault<UADSKSettings>()->bSleepWhenIdle = bSavedSleep; }

		UADSKBrainComponent* Start(bool bAutoTick = false, bool bManaged = false)
		{
			AActor& Host = Spawner.SpawnActor<AActor>();
			UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Host);
			Brain->bAutoTick = bAutoTick;
			Brain->bManagedTick = bManaged;
			Host.AddInstanceComponent(Brain);
			Brain->RegisterComponent();
			if (!Brain->HasBegunPlay()) Brain->BeginPlay();
			Brain->InitializeDefinition(ADSKTest::LoadOrStandIn(TEXT("sleep.xml"), Xml, ADSKTest::PluginCatalogue()));
			Brain->StartLogic();
			return Brain;
		}
	};

	FString Leaf(const UADSKBrainComponent& Brain) { return Brain.GetInstance()->GetActiveStatePath().Last(); }
	uint64 Ticks(const UADSKBrainComponent& Brain) { return Brain.GetInstance()->GetTickCount(); }
}

using namespace FSleepSpecPrivate;

TEST_CLASS_WITH_FLAGS(FSleepSpec, "AdamskAI.Host.Sleep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AWaitingBrainSkipsTicksAndGetsTheTimeWhenItWakes)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.Start();
		for (int32 Frame = 0; Frame < 9; ++Frame) Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("wait")), Leaf(*Brain)));
		ASSERT_THAT(AreEqual(uint64(0), Ticks(*Brain)));
		Brain->TickBrain(0.1f);
		// The waking tick hands over the whole second; the completion is handled on the next one.
		Brain->TickBrain(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("done")), Leaf(*Brain)));
		ASSERT_THAT(AreEqual(uint64(2), Ticks(*Brain)));
		Brain->Cleanup();
	}

	TEST_METHOD(AnInputOrAnEventWakesItAtOnce)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.Start();
		Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(Brain->IsAsleep(0.f), TEXT("the waiting brain is awake")));
		Brain->SetInput(TEXT("Alert"), FADSKValue::MakeBool(true));
		ASSERT_THAT(IsFalse(Brain->IsAsleep(0.f), TEXT("an input did not wake the brain")));
		Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("alert")), Leaf(*Brain)));
		Brain->Cleanup();

		UADSKBrainComponent* Poked = F.Start();
		Poked->TickBrain(0.1f);
		Poked->EnqueueEvent(TEXT("Poke"));
		Poked->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("poked")), Leaf(*Poked)));
		Poked->Cleanup();
	}

	TEST_METHOD(OffItTicksEveryCall)
	{
		FFixture F(false);
		UADSKBrainComponent* Brain = F.Start();
		for (int32 Frame = 0; Frame < 5; ++Frame) Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(uint64(5), Ticks(*Brain)));
		Brain->Cleanup();
	}

	TEST_METHOD(ABoundPreTickKeepsItAwake)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.Start();
		Brain->OnPreTick.AddLambda([] {});
		for (int32 Frame = 0; Frame < 5; ++Frame) Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(uint64(5), Ticks(*Brain)));
		Brain->OnPreTick.Clear();
		Brain->Cleanup();
	}

	TEST_METHOD(TheManagedPassSkipsASleepingBrain)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.Start(false, true);
		UADSKWorldSubsystem* Subsystem = F.Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		for (int32 Frame = 0; Frame < 9; ++Frame) Subsystem->TickManagedBrains(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("wait")), Leaf(*Brain)));
		ASSERT_THAT(IsTrue(Ticks(*Brain) <= 1, FString::Printf(TEXT("%llu managed ticks while waiting"), Ticks(*Brain))));
		for (int32 Frame = 0; Frame < 3; ++Frame) Subsystem->TickManagedBrains(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("done")), Leaf(*Brain)));
		Brain->Cleanup();
	}

	TEST_METHOD(AnAutoTickingBrainStretchesItsIntervalAndStopsTickingInAHold)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.Start(true);
		ASSERT_THAT(IsTrue(Brain->IsComponentTickEnabled(), TEXT("the auto tick is off while a wait runs")));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Brain->GetComponentTickInterval(), 1.f), FString::SanitizeFloat(Brain->GetComponentTickInterval())));
		Brain->TickBrain(1.f);
		Brain->TickBrain(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("done")), Leaf(*Brain)));
		ASSERT_THAT(IsFalse(Brain->IsComponentTickEnabled(), TEXT("a holding brain still ticks")));
		Brain->Cleanup();
	}

	TEST_METHOD(AnAutoTickingBrainThatWokeTicksEveryFrame)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.Start(true);
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Brain->GetComponentTickInterval(), 1.f), FString::SanitizeFloat(Brain->GetComponentTickInterval())));
		// Awake from the next tick on; later awake ticks must keep interval 0.
		Brain->OnPreTick.AddLambda([] {});
		for (int32 Frame = 0; Frame < 5; ++Frame)
		{
			Brain->TickBrain(0.1f);
			ASSERT_THAT(IsTrue(Brain->IsComponentTickEnabled(), TEXT("an awake brain stopped ticking")));
			ASSERT_THAT(IsTrue(Brain->GetComponentTickInterval() == 0.f, FString::SanitizeFloat(Brain->GetComponentTickInterval())));
		}
		ASSERT_THAT(AreEqual(uint64(5), Ticks(*Brain)));
		Brain->OnPreTick.Clear();
		Brain->Cleanup();
	}

	TEST_METHOD(AManagedBrainStoppedInAHoldStartsItsNextRunWithoutTheStoppedTime)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.Start(false, true);
		UADSKWorldSubsystem* Subsystem = F.Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		for (int32 Frame = 0; Frame < 12; ++Frame) Subsystem->TickManagedBrains(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("done")), Leaf(*Brain)));
		Brain->StopLogic();
		for (int32 Frame = 0; Frame < 50; ++Frame) Subsystem->TickManagedBrains(0.1f);
		Brain->StartLogic();
		// The new run's Wait has had 0.2 of its second, not the five the brain spent stopped.
		for (int32 Frame = 0; Frame < 2; ++Frame) Subsystem->TickManagedBrains(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("wait")), Leaf(*Brain)));
		Brain->Cleanup();
	}

	TEST_METHOD(AnAutoTickingBrainStoppedInAHoldStartsItsNextRunWithoutTheStoppedTime)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.Start(true);
		UWorld& World = F.Spawner.GetWorld();
		const auto Frame = [&World, Brain](float Delta)
		{
			World.TimeSeconds += Delta;
			Brain->TickComponent(Delta, LEVELTICK_All, nullptr);
		};
		Frame(1.f);
		Frame(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("done")), Leaf(*Brain)));
		Brain->StopLogic();
		World.TimeSeconds += 5.0;
		Brain->StartLogic();
		Frame(0.1f);
		Frame(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("wait")), Leaf(*Brain)));
		Brain->Cleanup();
	}
};

#endif
