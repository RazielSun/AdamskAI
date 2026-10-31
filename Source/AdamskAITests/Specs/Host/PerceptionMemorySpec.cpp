// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Bridges/ADSKPerceptionBridgeComponent.h"
#include "Brain/ADSKBrainComponent.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Components/ActorTestSpawner.h"
#include "Components/SceneComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "CQTest.h"
#include "Support/TestChecks.h"
#include "Support/FakeHostServices.h"

namespace FPerceptionMemorySpecPrivate
{
	const TCHAR* Xml = TEXT("<Agent version=\"1\" id=\"memory\"><Inputs>"
		"<Input name=\"Has\" type=\"Bool\" default=\"false\"/><Input name=\"Location\" type=\"Vector\" default=\"0,0,0\"/>"
		"<Input name=\"Sensed\" type=\"Bool\" default=\"false\"/><Input name=\"Count\" type=\"Int\" default=\"0\"/><Input name=\"Alert\" type=\"Float\" default=\"0\"/>"
		"</Inputs><Events><Event name=\"Lost\"/><Event name=\"Alerted\"/><Event name=\"Calmed\"/></Events>"
		"<StateMachine id=\"m\" initial=\"idle\"><State id=\"idle\" tree=\"wait\"><On event=\"Alerted\" goto=\"alarmed\"/></State>"
		"<State id=\"alarmed\" tree=\"wait\"><On event=\"Calmed\" goto=\"idle\"/></State></StateMachine>"
		"<Tree id=\"wait\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>");

	TSharedRef<const FADSKDefinition> LoadDefinition()
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		Registry->Freeze();
		return ADSKTest::LoadOrStandIn(TEXT("memory.xml"), Xml, Registry);
	}

	FADSKPerceptionBinding MemoryBinding()
	{
		FADSKPerceptionBinding Binding;
		Binding.Sense = UAISense_Sight::StaticClass();
		Binding.HasTargetInput = TEXT("Has");
		Binding.TargetLocationInput = TEXT("Location");
		Binding.TargetLostEvent = TEXT("Lost");
		Binding.ForgetAfter = 2.f;
		Binding.SensedInput = TEXT("Sensed");
		Binding.TargetCountInput = TEXT("Count");
		Binding.AlertInput = TEXT("Alert");
		Binding.AlertRise = 2.f;
		Binding.AlertDecay = 1.f;
		Binding.AlertedEvent = TEXT("Alerted");
		Binding.CalmedEvent = TEXT("Calmed");
		return Binding;
	}

	AActor& SpawnAt(FActorTestSpawner& Spawner, const FVector& Location)
	{
		AActor& Actor = Spawner.SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(&Actor);
		Actor.AddInstanceComponent(Root);
		Root->RegisterComponent();
		Actor.SetRootComponent(Root);
		Actor.SetActorLocation(Location);
		return Actor;
	}

	struct FMemoryRuntime
	{
		FActorTestSpawner Spawner;
		AActor* Host = &Spawner.SpawnActor<AActor>();
		FADSKScopedFakeHostServices Services{Spawner.GetWorld()};
		UAIPerceptionComponent* Perception = NewObject<UAIPerceptionComponent>(Host);
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(Host);
		UADSKPerceptionBridgeComponent* Bridge = NewObject<UADSKPerceptionBridgeComponent>(Host);

		explicit FMemoryRuntime(FADSKPerceptionBinding Binding = MemoryBinding())
		{
			Host->AddInstanceComponent(Perception);
			Host->AddInstanceComponent(Brain);
			Perception->RegisterComponent();
			Brain->RegisterComponent();
			Brain->InitializeDefinition(LoadDefinition());
			Bridge->Bindings = {Binding};
			Host->AddInstanceComponent(Bridge);
			Bridge->RegisterComponent();
			if (!Bridge->HasBegunPlay()) Bridge->BeginPlay();
			Services->Time = 0.0;
			Brain->StartLogic();
		}

		~FMemoryRuntime() { Brain->Cleanup(); }

		void TickAt(double Seconds)
		{
			Services->Time = Seconds;
			Brain->TickBrain(0.1f);
		}

		void See(AActor& Actor, bool bSensed) { Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &Actor, Actor.GetActorLocation(), bSensed); }

		FADSKValue Value(const TCHAR* Name) const
		{
			FADSKValue Result;
			ADSKTest::Expect(Brain->GetInstance()->GetValue(Name, Result), FString::Printf(TEXT("no slot %s"), Name));
			return Result;
		}

		FString State() const { return Brain->GetInstance()->GetActiveStatePath().Last(); }
	};
}

using namespace FPerceptionMemorySpecPrivate;

TEST_CLASS_WITH_FLAGS(FPerceptionMemorySpec, "AdamskAI.Host.PerceptionMemory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ALostTargetIsRememberedWhereItWasSensedUntilItIsForgotten)
	{
		FMemoryRuntime Runtime;
		AActor& Target = SpawnAt(Runtime.Spawner, FVector(10, 0, 0));
		Runtime.See(Target, true);
		Runtime.TickAt(0.0);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Has")) == FADSKValue::MakeBool(true), LexToString(Runtime.Value(TEXT("Has")))));
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Sensed")) == FADSKValue::MakeBool(true), LexToString(Runtime.Value(TEXT("Sensed")))));
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Count")) == FADSKValue::MakeInt(1), LexToString(Runtime.Value(TEXT("Count")))));

		Target.SetActorLocation(FVector(500, 0, 0));
		Runtime.Services->Time = 1.0;
		Runtime.See(Target, false);
		Runtime.TickAt(1.0);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Has")) == FADSKValue::MakeBool(true), LexToString(Runtime.Value(TEXT("Has")))));
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Sensed")) == FADSKValue::MakeBool(false), LexToString(Runtime.Value(TEXT("Sensed")))));
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Location")) == FADSKValue::MakeVector(FVector(10, 0, 0)), LexToString(Runtime.Value(TEXT("Location")))));
		Runtime.TickAt(2.9);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Has")) == FADSKValue::MakeBool(true), LexToString(Runtime.Value(TEXT("Has")))));
		Runtime.TickAt(3.0);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Has")) == FADSKValue::MakeBool(false), LexToString(Runtime.Value(TEXT("Has")))));
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Count")) == FADSKValue::MakeInt(0), LexToString(Runtime.Value(TEXT("Count")))));

		Runtime.See(Target, true);
		Runtime.TickAt(3.5);
		Runtime.Services->Time = 4.0;
		Runtime.See(Target, false);
		Runtime.See(Target, true);
		Runtime.TickAt(10.0);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Sensed")) == FADSKValue::MakeBool(true), LexToString(Runtime.Value(TEXT("Sensed")))));
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Location")) == FADSKValue::MakeVector(FVector(500, 0, 0)), LexToString(Runtime.Value(TEXT("Location")))));
	}

	TEST_METHOD(ASensedCandidateIsPreferredToANearerRememberedOne)
	{
		FMemoryRuntime Runtime;
		AActor& Near = SpawnAt(Runtime.Spawner, FVector(10, 0, 0));
		AActor& Far = SpawnAt(Runtime.Spawner, FVector(900, 0, 0));
		Runtime.See(Near, true);
		Runtime.See(Far, true);
		Runtime.See(Near, false);
		Runtime.TickAt(0.5);
		ASSERT_THAT(IsTrue(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass()) == &Far, TEXT("selected ") + GetNameSafe(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass()))));
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Count")) == FADSKValue::MakeInt(2), LexToString(Runtime.Value(TEXT("Count")))));
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Sensed")) == FADSKValue::MakeBool(true), LexToString(Runtime.Value(TEXT("Sensed")))));
		Runtime.See(Far, false);
		ASSERT_THAT(IsTrue(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass()) == &Near, TEXT("selected ") + GetNameSafe(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass()))));
	}

	TEST_METHOD(TheAlertLevelRisesWhileSensedFallsAfterAndRaisesItsEdges)
	{
		FADSKPerceptionBinding Binding = MemoryBinding();
		Binding.ForgetAfter = 0.f;
		FMemoryRuntime Runtime(Binding);
		AActor& Target = SpawnAt(Runtime.Spawner, FVector(10, 0, 0));
		Runtime.See(Target, true);
		Runtime.TickAt(0.0);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Alert")) == FADSKValue::MakeFloat(0.f), LexToString(Runtime.Value(TEXT("Alert")))));
		Runtime.TickAt(0.25);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Alert")) == FADSKValue::MakeFloat(0.5f), LexToString(Runtime.Value(TEXT("Alert")))));
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Runtime.State()));
		Runtime.TickAt(1.0);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Alert")) == FADSKValue::MakeFloat(1.f), LexToString(Runtime.Value(TEXT("Alert")))));
		ASSERT_THAT(AreEqual(FString(TEXT("alarmed")), Runtime.State()));
		Runtime.See(Target, false);
		Runtime.TickAt(1.5);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Alert")) == FADSKValue::MakeFloat(0.5f), LexToString(Runtime.Value(TEXT("Alert")))));
		ASSERT_THAT(AreEqual(FString(TEXT("alarmed")), Runtime.State()));
		Runtime.TickAt(3.0);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Alert")) == FADSKValue::MakeFloat(0.f), LexToString(Runtime.Value(TEXT("Alert")))));
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Runtime.State()));
	}

	TEST_METHOD(NewInputsAndEventsAreTypeChecked)
	{
		FMemoryRuntime Runtime;
		FString Error;
		for (const TFunction<void(FADSKPerceptionBinding&)>& Break : TArray<TFunction<void(FADSKPerceptionBinding&)>>{
			[](FADSKPerceptionBinding& B) { B.SensedInput = TEXT("Count"); },
			[](FADSKPerceptionBinding& B) { B.TargetCountInput = TEXT("Alert"); },
			[](FADSKPerceptionBinding& B) { B.AlertInput = TEXT("Sensed"); },
			[](FADSKPerceptionBinding& B) { B.AlertedEvent = TEXT("Missing"); },
			[](FADSKPerceptionBinding& B) { B.CalmedEvent = TEXT("Lost"); }})
		{
			FADSKPerceptionBinding Binding = MemoryBinding();
			Break(Binding);
			Runtime.Bridge->Bindings = {Binding};
			ASSERT_THAT(IsFalse(Runtime.Bridge->ValidateDefinitionForTest(*LoadDefinition(), Error), TEXT("the broken binding was accepted")));
		}
		Runtime.Bridge->Bindings = {MemoryBinding()};
		ASSERT_THAT(IsTrue(Runtime.Bridge->ValidateDefinitionForTest(*LoadDefinition(), Error), Error));
	}

	TEST_METHOD(AStopDropsTheMemoryAndTheAlertAndResyncsFromThePerception)
	{
		FMemoryRuntime Runtime;
		AActor& Seen = SpawnAt(Runtime.Spawner, FVector(10, 0, 0));
		AActor& StillSeen = SpawnAt(Runtime.Spawner, FVector(900, 0, 0));
		Runtime.See(Seen, true);
		Runtime.TickAt(0.0);
		Runtime.TickAt(0.25);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Alert")) == FADSKValue::MakeFloat(0.5f), LexToString(Runtime.Value(TEXT("Alert")))));
		Runtime.Services->Time = 0.3;
		Runtime.See(Seen, false);
		// After the restart only what the perception still reports comes back; the remembered actor does not.
		Runtime.Bridge->SetResyncCandidatesForTest(UAISense_Sight::StaticClass(), {{&StillSeen, StillSeen.GetActorLocation()}});
		Runtime.Brain->StopLogic(TEXT("test"));
		ASSERT_THAT(IsTrue(Runtime.Bridge->NeedsResyncForTest(UAISense_Sight::StaticClass()), TEXT("no resync is pending")));
		Runtime.Brain->StartLogic();
		// Well inside ForgetAfter: only the resync can have dropped the remembered actor.
		Runtime.TickAt(0.5);
		ASSERT_THAT(IsFalse(Runtime.Bridge->NeedsResyncForTest(UAISense_Sight::StaticClass()), TEXT("the resync is still pending")));
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Count")) == FADSKValue::MakeInt(1), LexToString(Runtime.Value(TEXT("Count")))));
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Sensed")) == FADSKValue::MakeBool(true), LexToString(Runtime.Value(TEXT("Sensed")))));
		ASSERT_THAT(IsTrue(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass()) == &StillSeen, TEXT("selected ") + GetNameSafe(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass()))));
		// The alert starts again from zero, and the restart's first pre-tick adds no time.
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Alert")) == FADSKValue::MakeFloat(0.f), LexToString(Runtime.Value(TEXT("Alert")))));
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Runtime.State()));
	}
};

#endif
