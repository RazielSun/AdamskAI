// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Components/ActorTestSpawner.h"
#include "CQTest.h"
#include "Support/TestChecks.h"

namespace FBrainTickSpecPrivate
{
	struct FHooks
	{
		int32 Updates = 0;
		int32 Ends = 0;
		int32 Seen = 0;
	};

	struct FRuntime
	{
		TSharedRef<FHooks> Hooks = MakeShared<FHooks>();
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>();

		FRuntime()
		{
			Brain->AddToRoot();
			const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
			FADSKBuiltinNodes::RegisterAll(*Registry);
			const TSharedRef<FHooks> H = Hooks;
			FADSKNodeDescriptor Probe;
			Probe.Name = TEXT("TickProbe");
			Probe.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Running; };
			Probe.OnUpdate = [H](FADSKNodeContext& Context, float)
			{
				++H->Updates;
				FADSKValue Value;
				if (Context.GetHost<UADSKBrainComponent>()->GetInstance()->GetValue(TEXT("Seen"), Value)) { H->Seen = Value.Int; }
				return EADSKNodeStatus::Running;
			};
			Probe.OnEnd = [H](FADSKNodeContext&, EADSKEndReason) { ++H->Ends; };
			ADSKTest::Register(*Registry, MoveTemp(Probe));
			Registry->Freeze();
			const FString Xml = TEXT("<Agent version=\"1\" id=\"tick\"><Inputs><Input name=\"Seen\" type=\"Int\" default=\"0\"/></Inputs><Locals/><Events/><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><TickProbe id=\"p\"/></Tree></Agent>");
			Brain->InitializeDefinition(ADSKTest::LoadOrStandIn(TEXT("tick.xml"), Xml, Registry));
			Brain->StartLogic();
		}

		~FRuntime() { Brain->Cleanup(); Brain->RemoveFromRoot(); }
	};
}

using namespace FBrainTickSpecPrivate;

TEST_CLASS_WITH_FLAGS(FBrainTickSpec, "AdamskAI.Host.BrainTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AutoTickIsOptInAndConfiguredForPrePhysics)
	{
		FActorTestSpawner Spawner;
		AActor& Actor = Spawner.SpawnActor<AActor>();
		UADSKBrainComponent* Disabled = NewObject<UADSKBrainComponent>(&Actor);
		Actor.AddInstanceComponent(Disabled);
		Disabled->RegisterComponent();
		Disabled->BeginPlay();
		ASSERT_THAT(IsTrue(Disabled->PrimaryComponentTick.bCanEverTick, TEXT("the brain can never tick")));
		ASSERT_THAT(IsFalse(Disabled->PrimaryComponentTick.bStartWithTickEnabled, TEXT("the brain starts with its tick enabled")));
		ASSERT_THAT(IsTrue(Disabled->PrimaryComponentTick.TickGroup == TG_PrePhysics, FString::Printf(TEXT("tick group %d"), static_cast<int32>(Disabled->PrimaryComponentTick.TickGroup.GetValue()))));
		ASSERT_THAT(IsFalse(Disabled->IsComponentTickEnabled(), TEXT("auto tick is off but the component tick is enabled")));
		UADSKBrainComponent* Enabled = NewObject<UADSKBrainComponent>(&Actor);
		Enabled->bAutoTick = true;
		Actor.AddInstanceComponent(Enabled);
		Enabled->RegisterComponent();
		Enabled->BeginPlay();
		ASSERT_THAT(IsTrue(Enabled->IsComponentTickEnabled(), TEXT("auto tick is on but the component tick is disabled")));
		Disabled->Cleanup();
		Enabled->Cleanup();
	}

	TEST_METHOD(DirectTickPauseAndPreTickPublicationAreDeterministic)
	{
		FRuntime Runtime;
		int32 PreTicks = 0;
		Runtime.Brain->OnPreTick.AddLambda([&Runtime, &PreTicks]()
		{
			++PreTicks;
			Runtime.Brain->SetInput(TEXT("Seen"), FADSKValue::MakeInt(7));
		});
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(1, PreTicks));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Updates));
		ASSERT_THAT(AreEqual(7, Runtime.Hooks->Seen));
		Runtime.Brain->PauseLogic(TEXT("test"));
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(1, PreTicks));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Updates));
	}

	TEST_METHOD(PreTickStopDoesNotTickAStaleInstanceAndPublishesAfterEnd)
	{
		FRuntime Runtime;
		int32 Stops = 0;
		int32 EndsAtStop = 0;
		Runtime.Brain->OnStopped.AddLambda([&Runtime, &Stops, &EndsAtStop]() { ++Stops; EndsAtStop = Runtime.Hooks->Ends; });
		Runtime.Brain->OnPreTick.AddLambda([&Runtime]() { Runtime.Brain->StopLogic(TEXT("pre-tick")); });
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(0, Runtime.Hooks->Updates));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Ends));
		ASSERT_THAT(AreEqual(1, Stops));
		ASSERT_THAT(AreEqual(1, EndsAtStop));
		Runtime.Brain->RestartLogic();
		Runtime.Brain->StopLogic(TEXT("again"));
		ASSERT_THAT(AreEqual(2, Stops));
	}
};

#endif
