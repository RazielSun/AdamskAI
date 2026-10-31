// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Bridges/ADSKPerceptionBridgeComponent.h"
#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Support/TempTree.h"
#include "Components/ActorTestSpawner.h"
#include "Components/SceneComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "CQTest.h"
#include "Support/TestChecks.h"

namespace FPerceptionBridgeSpecPrivate
{
	const TCHAR* SourceFile = TEXT("perception.xml");

	FString SchemaXml(
		const FString& Inputs = TEXT("<Input name=\"Has\" type=\"Bool\" default=\"false\"/><Input name=\"Location\" type=\"Vector\" default=\"0,0,0\"/>"),
		const FString& Locals = FString(),
		const FString& Events = TEXT("<Event name=\"Seen\"/><Event name=\"Lost\"/><Event name=\"Stimulus\"><Param name=\"location\" type=\"Vector\"/></Event>"),
		const TCHAR* Duration = TEXT("100"))
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"perception\"><Inputs>%s</Inputs><Locals>%s</Locals><Events>%s</Events>")
			TEXT("<StateMachine id=\"m\" initial=\"idle\"><State id=\"idle\" tree=\"wait\"/></StateMachine>")
			TEXT("<Tree id=\"wait\"><Wait id=\"wait-node\" duration=\"%s\"/></Tree></Agent>"), *Inputs, *Locals, *Events, Duration);
	}

	FString RuntimeXml()
	{
		return TEXT("<Agent version=\"1\" id=\"perception-runtime\">")
			TEXT("<Inputs><Input name=\"Has\" type=\"Bool\" default=\"false\"/><Input name=\"Location\" type=\"Vector\" default=\"0,0,0\"/></Inputs>")
			TEXT("<Locals><Local name=\"LastStimulus\" type=\"Vector\"/></Locals>")
			TEXT("<Events><Event name=\"Seen\"/><Event name=\"Lost\"/><Event name=\"Stimulus\"><Param name=\"location\" type=\"Vector\"/></Event></Events>")
			TEXT("<StateMachine id=\"m\" initial=\"idle\">")
			TEXT("<State id=\"idle\" tree=\"wait\"><On event=\"Seen\" goto=\"active\"/><On event=\"Stimulus\"><Bind param=\"location\" to=\"LastStimulus\"/></On></State>")
			TEXT("<State id=\"active\" tree=\"wait\"><On event=\"Lost\" goto=\"lost\"/><On event=\"Stimulus\"><Bind param=\"location\" to=\"LastStimulus\"/></On></State>")
			TEXT("<State id=\"lost\" tree=\"wait\"><On event=\"Seen\" goto=\"active\"/><On event=\"Stimulus\"><Bind param=\"location\" to=\"LastStimulus\"/></On></State>")
			TEXT("</StateMachine><Tree id=\"wait\"><Wait id=\"wait-node\" duration=\"100\"/></Tree></Agent>");
	}

	TSharedRef<const FADSKDefinition> LoadDefinition(const FString& Xml)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		Registry->Freeze();
		return ADSKTest::LoadOrStandIn(TEXT("perception.xml"), Xml, Registry);
	}

	FADSKPerceptionBinding SightBinding()
	{
		FADSKPerceptionBinding Result;
		Result.Sense = UAISense_Sight::StaticClass();
		Result.HasTargetInput = TEXT("Has");
		Result.TargetLocationInput = TEXT("Location");
		Result.TargetSeenEvent = TEXT("Seen");
		Result.TargetLostEvent = TEXT("Lost");
		Result.StimulusEvent = TEXT("Stimulus");
		return Result;
	}

	void SetLocation(AActor& Actor, const FVector& Location)
	{
		if (!Actor.GetRootComponent())
		{
			USceneComponent* Root = NewObject<USceneComponent>(&Actor);
			Actor.AddInstanceComponent(Root);
			Root->RegisterComponent();
			Actor.SetRootComponent(Root);
		}
		Actor.SetActorLocation(Location);
	}

	struct FBridgeRuntime
	{
		FActorTestSpawner Spawner;
		AActor* Host = &Spawner.SpawnActor<AActor>();
		UAIPerceptionComponent* Perception = NewObject<UAIPerceptionComponent>(Host);
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(Host);
		UADSKPerceptionBridgeComponent* Bridge = NewObject<UADSKPerceptionBridgeComponent>(Host);

		FBridgeRuntime(const TSharedRef<const FADSKDefinition>& Definition, TArray<FADSKPerceptionBinding> InBindings)
		{
			Host->SetActorLocation(FVector::ZeroVector);
			Host->AddInstanceComponent(Perception);
			Host->AddInstanceComponent(Brain);
			Perception->RegisterComponent();
			Brain->RegisterComponent();
			Brain->InitializeDefinition(Definition);
			Bridge->Bindings = MoveTemp(InBindings);
			Host->AddInstanceComponent(Bridge);
			Bridge->RegisterComponent();
			if (!Bridge->HasBegunPlay()) Bridge->BeginPlay();
			Brain->StartLogic();
		}

		explicit FBridgeRuntime(const FString& Xml = RuntimeXml())
			: FBridgeRuntime(LoadDefinition(Xml), {SightBinding()})
		{
		}

		~FBridgeRuntime() { Brain->Cleanup(); }

		FADSKValue Value(const TCHAR* Name) const
		{
			FADSKValue Result;
			ADSKTest::Expect(Brain->GetInstance()->GetValue(Name, Result), FString::Printf(TEXT("no slot %s"), Name));
			return Result;
		}

		FString State() const { return Brain->GetInstance()->GetActiveStatePath().Last(); }
	};

	bool Rejects(UADSKPerceptionBridgeComponent& Bridge, const FString& Xml, FString& OutError)
	{
		return !Bridge.ValidateDefinitionForTest(*LoadDefinition(Xml), OutError)
			&& OutError.StartsWith(TEXT("perception-binding: "));
	}
}

using namespace FPerceptionBridgeSpecPrivate;

TEST_CLASS_WITH_FLAGS(FPerceptionBridgeSpec, "AdamskAI.Host.PerceptionBridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(SelectsNearestWithStableTieAndSupportsPolicyResults)
	{
		FBridgeRuntime Runtime;
		AActor& First = Runtime.Spawner.SpawnActor<AActor>();
		AActor& Second = Runtime.Spawner.SpawnActor<AActor>();
		AActor& Foreign = Runtime.Spawner.SpawnActor<AActor>();
		SetLocation(First, FVector(100, 0, 0));
		SetLocation(Second, FVector(50, 0, 0));
		Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &First, FVector(1, 0, 0), true);
		Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &Second, FVector(2, 0, 0), true);
		ASSERT_THAT(IsTrue(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass()) == &Second, TEXT("selected ") + GetNameSafe(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass()))));

		SetLocation(First, FVector(-50, 0, 0));
		AActor* ExpectedTie = First.GetPathName() < Second.GetPathName() ? &First : &Second;
		ASSERT_THAT(IsTrue(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass()) == ExpectedTie, TEXT("selected ") + GetNameSafe(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass()))));

		Runtime.Bridge->SelectTarget.BindLambda([&First](const UADSKPerceptionBridgeComponent&, TSubclassOf<UAISense>, TConstArrayView<AActor*>) { return &First; });
		ASSERT_THAT(IsTrue(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass()) == &First, TEXT("selected ") + GetNameSafe(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass()))));
		Runtime.Bridge->SelectTarget.BindLambda([](const UADSKPerceptionBridgeComponent&, TSubclassOf<UAISense>, TConstArrayView<AActor*>) -> AActor* { return nullptr; });
		ASSERT_THAT(IsNull(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass())));

		TestRunner->AddExpectedError(TEXT("selector returned a foreign actor"), EAutomationExpectedErrorFlags::Contains, 1);
		Runtime.Bridge->SelectTarget.BindLambda([&Foreign](const UADSKPerceptionBridgeComponent&, TSubclassOf<UAISense>, TConstArrayView<AActor*>) { return &Foreign; });
		ASSERT_THAT(IsNull(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass())));
		ASSERT_THAT(IsNull(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass())));
	}

	TEST_METHOD(RemovesFailedAndDeadCandidatesAndCapsTheFifo)
	{
		FBridgeRuntime Runtime;
		AActor& Candidate = Runtime.Spawner.SpawnActor<AActor>();
		Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &Candidate, FVector(1), true);
		Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &Candidate, FVector(1), false);
		ASSERT_THAT(AreEqual(0, Runtime.Bridge->GetCandidateCountForTest(UAISense_Sight::StaticClass())));
		Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &Candidate, FVector(1), true);
		Candidate.Destroy();
		ASSERT_THAT(IsNull(Runtime.Bridge->SelectTargetForTest(UAISense_Sight::StaticClass())));
		ASSERT_THAT(AreEqual(0, Runtime.Bridge->GetCandidateCountForTest(UAISense_Sight::StaticClass())));
		Runtime.Brain->TickBrain(0.1f);

		AActor& Live = Runtime.Spawner.SpawnActor<AActor>();
		for (int32 Index = 1; Index <= 9; ++Index)
		{
			Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &Live, FVector(Index, 0, 0), true);
		}
		ASSERT_THAT(AreEqual(8, Runtime.Bridge->GetQueuedStimulusCountForTest(UAISense_Sight::StaticClass())));
		ASSERT_THAT(AreEqual(static_cast<uint32>(1), Runtime.Bridge->GetDroppedStimuli()));
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("LastStimulus")).Vector.Equals(FVector(9, 0, 0)), Runtime.Value(TEXT("LastStimulus")).Vector.ToString()));
	}

	TEST_METHOD(UnknownAndKnownUnboundSenseWarningsAreBucketed)
	{
		FBridgeRuntime Runtime;
		AActor& Candidate = Runtime.Spawner.SpawnActor<AActor>();
		TestRunner->AddExpectedError(TEXT("ignored null or unknown sense"), EAutomationExpectedErrorFlags::Contains, 1);
		Runtime.Perception->OnTargetPerceptionUpdated.Broadcast(&Candidate, FAIStimulus());
		Runtime.Perception->OnTargetPerceptionUpdated.Broadcast(&Candidate, FAIStimulus());

		TestRunner->AddExpectedError(TEXT("ignored unbound sense"), EAutomationExpectedErrorFlags::Contains, 1);
		Runtime.Bridge->InjectResolvedStimulusForTest(UAISense_Hearing::StaticClass(), &Candidate, FVector(10), true);
		Runtime.Bridge->InjectResolvedStimulusForTest(UAISense_Hearing::StaticClass(), &Candidate, FVector(10), true);
	}

	TEST_METHOD(RejectsEveryBindingSchemaShape)
	{
		FBridgeRuntime Runtime;
		FString Error;
		FADSKPerceptionBinding Binding = SightBinding();

		Runtime.Bridge->Bindings = {Binding};
		Runtime.Bridge->Bindings[0].Sense = nullptr;
		ASSERT_THAT(IsTrue(Rejects(*Runtime.Bridge, SchemaXml(), Error), TEXT("a binding without a sense was accepted")));
		Runtime.Bridge->Bindings = {Binding, Binding};
		ASSERT_THAT(IsTrue(Rejects(*Runtime.Bridge, SchemaXml(), Error), TEXT("a duplicate binding was accepted")));
		FADSKPerceptionBinding Hearing = Binding;
		Hearing.Sense = UAISense_Hearing::StaticClass();
		Hearing.TargetLocationInput.Reset();
		Hearing.TargetSeenEvent.Reset();
		Hearing.TargetLostEvent.Reset();
		Hearing.StimulusEvent.Reset();
		Runtime.Bridge->Bindings = {Binding, Hearing};
		ASSERT_THAT(IsTrue(Rejects(*Runtime.Bridge, SchemaXml(), Error), TEXT("the bridge accepted the bindings")));

		Runtime.Bridge->Bindings = {Binding};
		ASSERT_THAT(IsTrue(Rejects(*Runtime.Bridge, SchemaXml(TEXT("<Input name=\"Has\" type=\"Vector\" default=\"0,0,0\"/><Input name=\"Location\" type=\"Vector\" default=\"0,0,0\"/>")), Error), TEXT("the bridge accepted the schema")));
		ASSERT_THAT(IsTrue(Rejects(*Runtime.Bridge, SchemaXml(TEXT("<Input name=\"Location\" type=\"Vector\" default=\"0,0,0\"/>"), TEXT("<Local name=\"Has\" type=\"Bool\"/>")), Error), TEXT("the bridge accepted the schema")));
		ASSERT_THAT(IsTrue(Rejects(*Runtime.Bridge, SchemaXml(TEXT("<Input name=\"Has\" type=\"Bool\" default=\"false\"/><Input name=\"Location\" type=\"Bool\" default=\"false\"/>")), Error), TEXT("the bridge accepted the schema")));
		ASSERT_THAT(IsTrue(Rejects(*Runtime.Bridge, SchemaXml(TEXT("<Input name=\"Has\" type=\"Bool\" default=\"false\"/>"), TEXT("<Local name=\"Location\" type=\"Vector\"/>")), Error), TEXT("the bridge accepted the schema")));

		ASSERT_THAT(IsTrue(Rejects(*Runtime.Bridge, SchemaXml({}, {}, TEXT("<Event name=\"Seen\"><Param name=\"x\" type=\"Bool\"/></Event><Event name=\"Lost\"/><Event name=\"Stimulus\"><Param name=\"location\" type=\"Vector\"/></Event>")), Error), TEXT("the bridge accepted the schema")));
		ASSERT_THAT(IsTrue(Rejects(*Runtime.Bridge, SchemaXml({}, {}, TEXT("<Event name=\"Seen\"/><Event name=\"Lost\"><Param name=\"x\" type=\"Bool\"/></Event><Event name=\"Stimulus\"><Param name=\"location\" type=\"Vector\"/></Event>")), Error), TEXT("the bridge accepted the schema")));
		for (const FString& BadStimulus : {
			FString(TEXT("<Event name=\"Stimulus\"/>")),
			FString(TEXT("<Event name=\"Stimulus\"><Param name=\"where\" type=\"Vector\"/></Event>")),
			FString(TEXT("<Event name=\"Stimulus\"><Param name=\"location\" type=\"Bool\"/></Event>")),
			FString(TEXT("<Event name=\"Stimulus\"><Param name=\"location\" type=\"Vector\"/><Param name=\"other\" type=\"Vector\"/></Event>"))})
		{
			ASSERT_THAT(IsTrue(Rejects(*Runtime.Bridge, SchemaXml({}, {}, TEXT("<Event name=\"Seen\"/><Event name=\"Lost\"/>") + BadStimulus), Error), TEXT("the bridge accepted ") + BadStimulus));
		}
	}

	TEST_METHOD(RejectsReloadAndPreservesTheActiveDefinition)
	{
		FADSKTempTree Tree;
		FActorTestSpawner Spawner;
		UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		IFileManager::Get().MakeDirectory(*Tree.Root, true);
		Tree.Write(SourceFile, SchemaXml());
		TArray<FString> Errors;
		Subsystem->SetTestRoot(Tree.Root);
		Subsystem->SetTestRegistry(FADSKRegistryAssembly::BuildPluginCatalogue(Errors));
		AActor& Host = Spawner.SpawnActor<AActor>();
		UAIPerceptionComponent* Perception = NewObject<UAIPerceptionComponent>(&Host);
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Host);
		UADSKPerceptionBridgeComponent* Bridge = NewObject<UADSKPerceptionBridgeComponent>(&Host);
		Bridge->Bindings = {SightBinding()};
		for (UActorComponent* Component : {static_cast<UActorComponent*>(Perception), static_cast<UActorComponent*>(Brain), static_cast<UActorComponent*>(Bridge)})
		{
			Host.AddInstanceComponent(Component);
			Component->RegisterComponent();
		}
		if (!Bridge->HasBegunPlay()) Bridge->BeginPlay();
		ASSERT_THAT(IsTrue(Bridge->IsBridgeEnabled(), TEXT("the bridge is disabled")));
		ASSERT_THAT(IsTrue(Brain->InitializeFromSource(SourceFile), Brain->GetLastError()));
		const FADSKDefinition* Active = &Brain->GetDefinition().Get();
		Tree.Write(SourceFile, SchemaXml(TEXT("<Input name=\"Has\" type=\"Vector\" default=\"0,0,0\"/><Input name=\"Location\" type=\"Vector\" default=\"0,0,0\"/>"), {}, {}, TEXT("50")));
		FString Error;
		ASSERT_THAT(AreEqual(EADSKReloadResult::Rejected, Subsystem->RequestReload(SourceFile, Error)));
		ASSERT_THAT(IsTrue(&Brain->GetDefinition().Get() == Active, TEXT("the rejected reload replaced the definition")));
		Brain->Cleanup();
	}

	TEST_METHOD(MissingAmbiguousAndLateInvalidDependenciesDisableOnlyTheBridge)
	{
		TestRunner->AddExpectedError(TEXT("perception bridge disabled"), EAutomationExpectedErrorFlags::Contains, 3);
		FActorTestSpawner Spawner;
		AActor& Missing = Spawner.SpawnActor<AActor>();
		UADSKPerceptionBridgeComponent* MissingBridge = NewObject<UADSKPerceptionBridgeComponent>(&Missing);
		Missing.AddInstanceComponent(MissingBridge);
		MissingBridge->RegisterComponent();
		if (!MissingBridge->HasBegunPlay()) MissingBridge->BeginPlay();
		ASSERT_THAT(IsFalse(MissingBridge->IsBridgeEnabled(), TEXT("the bridge is enabled")));

		AActor& Ambiguous = Spawner.SpawnActor<AActor>();
		for (int32 Index = 0; Index < 2; ++Index)
		{
			UAIPerceptionComponent* Perception = NewObject<UAIPerceptionComponent>(&Ambiguous);
			Ambiguous.AddInstanceComponent(Perception);
			Perception->RegisterComponent();
		}
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Ambiguous);
		Ambiguous.AddInstanceComponent(Brain);
		Brain->RegisterComponent();
		UADSKPerceptionBridgeComponent* AmbiguousBridge = NewObject<UADSKPerceptionBridgeComponent>(&Ambiguous);
		Ambiguous.AddInstanceComponent(AmbiguousBridge);
		AmbiguousBridge->RegisterComponent();
		if (!AmbiguousBridge->HasBegunPlay()) AmbiguousBridge->BeginPlay();
		ASSERT_THAT(IsFalse(AmbiguousBridge->IsBridgeEnabled(), TEXT("the bridge is enabled")));

		FADSKPerceptionBinding Invalid = SightBinding();
		Invalid.HasTargetInput = TEXT("Missing");
		FBridgeRuntime Late(LoadDefinition(SchemaXml()), {Invalid});
		ASSERT_THAT(IsFalse(Late.Bridge->IsBridgeEnabled(), TEXT("the bridge is enabled")));
		ASSERT_THAT(IsTrue(Late.Brain->IsADSKRunning(), Late.Brain->GetLastError()));
	}

	TEST_METHOD(PublishesOnlyOnPreTickWithEdgesSwitchAndRetainedLocation)
	{
		FBridgeRuntime Runtime;
		AActor& First = Runtime.Spawner.SpawnActor<AActor>();
		AActor& Second = Runtime.Spawner.SpawnActor<AActor>();
		SetLocation(First, FVector(100, 0, 0));
		SetLocation(Second, FVector(200, 0, 0));
		Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &First, FVector(1), true);
		ASSERT_THAT(IsFalse(Runtime.Value(TEXT("Has")).bBool, TEXT("Has is true before the tick")));
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Runtime.State()));
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Has")).bBool, TEXT("Has is false")));
		ASSERT_THAT(AreEqual(FString(TEXT("active")), Runtime.State()));

		Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &First, FVector(1), false);
		Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &Second, FVector(2), true);
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("active")), Runtime.State()));
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Location")).Vector.Equals(Second.GetActorLocation()), Runtime.Value(TEXT("Location")).Vector.ToString()));

		Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &Second, FVector(2), false);
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsFalse(Runtime.Value(TEXT("Has")).bBool, TEXT("Has is true")));
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), Runtime.State()));
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("Location")).Vector.Equals(Second.GetActorLocation()), Runtime.Value(TEXT("Location")).Vector.ToString()));
	}

	TEST_METHOD(PublishesLiveSightAndStoredHearingLocations)
	{
		const FString Inputs = TEXT("<Input name=\"HasSight\" type=\"Bool\" default=\"false\"/><Input name=\"SightLocation\" type=\"Vector\" default=\"0,0,0\"/>")
			TEXT("<Input name=\"HasHearing\" type=\"Bool\" default=\"false\"/><Input name=\"HearingLocation\" type=\"Vector\" default=\"0,0,0\"/>");
		FADSKPerceptionBinding Sight;
		Sight.Sense = UAISense_Sight::StaticClass();
		Sight.HasTargetInput = TEXT("HasSight");
		Sight.TargetLocationInput = TEXT("SightLocation");
		FADSKPerceptionBinding Hearing;
		Hearing.Sense = UAISense_Hearing::StaticClass();
		Hearing.HasTargetInput = TEXT("HasHearing");
		Hearing.TargetLocationInput = TEXT("HearingLocation");
		FBridgeRuntime Runtime(LoadDefinition(SchemaXml(Inputs, {}, {})), {Sight, Hearing});
		AActor& SightActor = Runtime.Spawner.SpawnActor<AActor>();
		AActor& HearingActor = Runtime.Spawner.SpawnActor<AActor>();
		SetLocation(SightActor, FVector(900, 0, 0));
		SetLocation(HearingActor, FVector(800, 0, 0));
		Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &SightActor, FVector(10, 0, 0), true);
		Runtime.Bridge->InjectStimulusForTest(UAISense_Hearing::StaticClass(), &HearingActor, FVector(55, 0, 0), true);
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("SightLocation")).Vector.Equals(FVector(900, 0, 0)), Runtime.Value(TEXT("SightLocation")).Vector.ToString()));
		ASSERT_THAT(IsTrue(Runtime.Value(TEXT("HearingLocation")).Vector.Equals(FVector(55, 0, 0)), Runtime.Value(TEXT("HearingLocation")).Vector.ToString()));
	}

	TEST_METHOD(QueueFullConsumesStimuliWithoutRetry)
	{
		FBridgeRuntime Runtime;
		AActor& Candidate = Runtime.Spawner.SpawnActor<AActor>();
		for (int32 Index = 0; Index < ADSK::ExternalEventCapacity; ++Index)
		{
			ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Brain->GetInstance()->EnqueueEvent(TEXT("Seen"))));
		}
		Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &Candidate, FVector(7), true);
		TestRunner->AddExpectedError(TEXT("perception bridge input write failed"), EAutomationExpectedErrorFlags::Contains, 1);
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(0, Runtime.Bridge->GetQueuedStimulusCountForTest(UAISense_Sight::StaticClass())));
		Runtime.Brain->TickBrain(0.1f);
	}

	TEST_METHOD(StopClearsEdgesAndQueueThenResyncRefiresSeen)
	{
		FBridgeRuntime Runtime;
		AActor& First = Runtime.Spawner.SpawnActor<AActor>();
		AActor& Second = Runtime.Spawner.SpawnActor<AActor>();
		Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &First, FVector(1), true);
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("active")), Runtime.State()));
		Runtime.Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &First, FVector(2), true);
		Runtime.Bridge->SetResyncCandidatesForTest(UAISense_Sight::StaticClass(), {{&Second, FVector(22, 0, 0)}});
		Runtime.Brain->StopLogic(TEXT("test"));
		ASSERT_THAT(AreEqual(0, Runtime.Bridge->GetQueuedStimulusCountForTest(UAISense_Sight::StaticClass())));
		ASSERT_THAT(IsTrue(Runtime.Bridge->NeedsResyncForTest(UAISense_Sight::StaticClass()), TEXT("no resync is pending")));
		Runtime.Brain->RestartLogic();
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Runtime.State()));
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("active")), Runtime.State()));
		ASSERT_THAT(AreEqual(1, Runtime.Bridge->GetCandidateCountForTest(UAISense_Sight::StaticClass())));
	}

	TEST_METHOD(EndPlayRemovesPerceptionNativeAndValidatorRegistrations)
	{
		FBridgeRuntime Runtime;
		ASSERT_THAT(IsTrue(Runtime.Bridge->HasPerceptionRegistrationForTest(), TEXT("not registered with perception")));
		ASSERT_THAT(IsTrue(Runtime.Bridge->HasNativeRegistrationsForTest(), TEXT("no native registrations")));
		Runtime.Bridge->EndPlay(EEndPlayReason::Destroyed);
		ASSERT_THAT(IsFalse(Runtime.Bridge->HasPerceptionRegistrationForTest(), TEXT("still registered with perception after EndPlay")));
		ASSERT_THAT(IsFalse(Runtime.Bridge->HasNativeRegistrationsForTest(), TEXT("native registrations outlived EndPlay")));
		FString Error;
		const TSharedRef<const FADSKDefinition> Invalid = LoadDefinition(SchemaXml(TEXT("<Input name=\"Has\" type=\"Vector\" default=\"0,0,0\"/><Input name=\"Location\" type=\"Vector\" default=\"0,0,0\"/>")));
		ASSERT_THAT(IsTrue(Runtime.Brain->RunBindingValidators(*Invalid, Error), Error));
	}
};

#endif
