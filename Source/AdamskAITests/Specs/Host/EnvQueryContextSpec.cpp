// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AIController.h"
#include "Brain/ADSKBrainComponent.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Components/ActorTestSpawner.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "GameFramework/Pawn.h"
#include "Nodes/ADSKEnvQueryContexts.h"
#include "Objects/ADSKObjectHandles.h"
#include "Support/EnvQueryContexts.h"
#include "Support/TestChecks.h"
#include "UObject/Package.h"
#include "CQTest.h"

namespace FEnvQueryContextSpecPrivate
{
	const TCHAR* Xml = TEXT("<Agent version=\"1\" id=\"eqs_context\"><Inputs>")
		TEXT("<Input name=\"Target\" type=\"Object\" default=\"none\"/><Input name=\"Point\" type=\"Vector\" default=\"0,0,0\"/>")
		TEXT("<Input name=\"Flag\" type=\"Bool\" default=\"false\"/></Inputs>")
		TEXT("<StateMachine id=\"m\" initial=\"idle\"><State id=\"idle\" tree=\"wait\"/></StateMachine>")
		TEXT("<Tree id=\"wait\"><Wait id=\"wait_node\" duration=\"100\"/></Tree></Agent>");

	UADSKBrainComponent& AddBrain(AActor& Owner)
	{
		TArray<FString> Errors;
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Owner);
		Owner.AddInstanceComponent(Brain);
		Brain->RegisterComponent();
		Brain->InitializeDefinition(ADSKTest::LoadOrStandIn(TEXT("eqs_context.xml"), Xml, FADSKRegistryAssembly::BuildPluginCatalogue(Errors).ToSharedRef()));
		Brain->StartLogic();
		return *Brain;
	}

	/** Inputs land on the next tick. */
	void Set(UADSKBrainComponent& Brain, const TCHAR* Input, const FADSKValue& Value)
	{
		ADSKTest::Expect(Brain.SetInput(Input, Value) == EADSKInputResult::Accepted, FString(TEXT("write refused: ")) + Input);
		Brain.TickBrain(0.1f);
	}

	FEnvQueryContextData Provide(TSubclassOf<UADSKEnvQueryContext_Slot> Class, AActor& Querier)
	{
		FEnvQueryInstance Query;
		Query.Owner = &Querier;
		FEnvQueryContextData Data;
		NewObject<UADSKEnvQueryContext_Slot>(GetTransientPackage(), Class)->ProvideContext(Query, Data);
		return Data;
	}

	const AActor* ActorOf(const FEnvQueryContextData& Data)
	{
		return Data.NumValues == 1 && Data.ValueType == UEnvQueryItemType_Actor::StaticClass() ? UEnvQueryItemType_Actor::GetValue(Data.RawData.GetData()) : nullptr;
	}
}

using namespace FEnvQueryContextSpecPrivate;

TEST_CLASS_WITH_FLAGS(FEnvQueryContextSpec, "AdamskAI.Host.EnvQueryContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AnObjectSlotGivesItsActorAndNoneGivesNothing)
	{
		FActorTestSpawner Spawner;
		AActor& Host = Spawner.SpawnActor<AActor>();
		AActor& Enemy = Spawner.SpawnActor<AActor>();
		UADSKBrainComponent& Brain = AddBrain(Host);
		ASSERT_THAT(AreEqual(0, Provide(UADSKEnvQueryContext_Target::StaticClass(), Host).NumValues));
		Set(Brain, TEXT("Target"), FADSKObjectHandles::Make(&Enemy));
		ASSERT_THAT(IsTrue(ActorOf(Provide(UADSKEnvQueryContext_Target::StaticClass(), Host)) == &Enemy, TEXT("the context is not the Target actor")));
		Enemy.Destroy();
		ASSERT_THAT(AreEqual(0, Provide(UADSKEnvQueryContext_Target::StaticClass(), Host).NumValues));
		Brain.Cleanup();
	}

	TEST_METHOD(AVectorSlotGivesItsPoint)
	{
		FActorTestSpawner Spawner;
		AActor& Host = Spawner.SpawnActor<AActor>();
		UADSKBrainComponent& Brain = AddBrain(Host);
		Set(Brain, TEXT("Point"), FADSKValue::MakeVector(FVector(100, 200, 300)));
		const FEnvQueryContextData Data = Provide(UADSKTestEnvQueryContext_Point::StaticClass(), Host);
		ASSERT_THAT(IsTrue(Data.NumValues == 1 && Data.ValueType == UEnvQueryItemType_Point::StaticClass(), TEXT("no point context")));
		ASSERT_THAT(IsTrue(FVector(UEnvQueryItemType_Point::GetValue(Data.RawData.GetData())).Equals(FVector(100, 200, 300))));
		Brain.Cleanup();
	}

	TEST_METHOD(APawnQuerierReadsItsControllersBrain)
	{
		FActorTestSpawner Spawner;
		APawn& Pawn = Spawner.SpawnActor<APawn>();
		AAIController& Controller = Spawner.SpawnActor<AAIController>();
		AActor& Enemy = Spawner.SpawnActor<AActor>();
		UADSKBrainComponent& Brain = AddBrain(Controller);
		Controller.Possess(&Pawn);
		ASSERT_THAT(IsTrue(Pawn.GetController() == &Controller, TEXT("the controller did not possess the pawn")));
		Set(Brain, TEXT("Target"), FADSKObjectHandles::Make(&Enemy));
		ASSERT_THAT(IsTrue(ActorOf(Provide(UADSKEnvQueryContext_Target::StaticClass(), Pawn)) == &Enemy, TEXT("the pawn's context is not its controller's Target")));
		Brain.Cleanup();
	}

	TEST_METHOD(AWrongSlotOrNoBrainIsEmptyAndWarnsOnce)
	{
		FActorTestSpawner Spawner;
		AActor& Host = Spawner.SpawnActor<AActor>();
		AActor& Bare = Spawner.SpawnActor<AActor>();
		UADSKBrainComponent& Brain = AddBrain(Host);
		TestRunner->AddExpectedError(TEXT("no Object or Vector slot 'Flag'"), EAutomationExpectedErrorFlags::Contains, 1);
		TestRunner->AddExpectedError(TEXT("no AdamskAI brain"), EAutomationExpectedErrorFlags::Contains, 1);
		UADSKEnvQueryContext_Slot* Flag = NewObject<UADSKTestEnvQueryContext_Flag>();
		UADSKEnvQueryContext_Slot* Target = NewObject<UADSKEnvQueryContext_Target>();
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			FEnvQueryInstance Query;
			FEnvQueryContextData Data;
			Query.Owner = &Host;
			Flag->ProvideContext(Query, Data);
			ASSERT_THAT(AreEqual(0, Data.NumValues));
			Query.Owner = &Bare;
			Target->ProvideContext(Query, Data);
			ASSERT_THAT(AreEqual(0, Data.NumValues));
		}
		Brain.Cleanup();
	}
};

#endif
