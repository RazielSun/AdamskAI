// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Bridges/ADSKPerceptionBridgeComponent.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Debug/ADSKDebugSnapshot.h"
#include "Objects/ADSKObjectHandles.h"
#include "Compiler/ADSKCompiler.h"
#include "AIController.h"
#include "Components/ActorTestSpawner.h"
#include "GameFramework/Pawn.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "CQTest.h"
#include "Support/TestChecks.h"

namespace FObjectHostSpecPrivate
{
	TSharedRef<const FADSKDefinition> Load(const FString& Tree)
	{
		const FString Xml = FString::Printf(TEXT("<Agent version=\"1\" id=\"o\"><Inputs><Input name=\"Target\" type=\"Object\" default=\"none\"/></Inputs>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"><On event=\"tree.success\" goto=\"won\"/><On event=\"tree.failure\" goto=\"lost\"/></State>")
			TEXT("<State id=\"won\"/><State id=\"lost\"/></StateMachine><Tree id=\"t\">%s</Tree></Agent>"), *Tree);
		return ADSKTest::LoadOrStandIn(TEXT("object.xml"), Xml, ADSKTest::PluginCatalogue());
	}

	struct FControlled
	{
		FActorTestSpawner Spawner;
		AAIController& Controller = Spawner.SpawnActor<AAIController>();
		APawn& Pawn = Spawner.SpawnActor<APawn>();
		AActor* Target = &Spawner.SpawnActor<AActor>();
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Controller);

		explicit FControlled(const FString& Tree)
		{
			Controller.Possess(&Pawn);
			Controller.AddInstanceComponent(Brain);
			Brain->RegisterComponent();
			Brain->InitializeDefinition(Load(Tree), FADSKRecorder::Create(64));
		}
		~FControlled() { Brain->Cleanup(); }

		void Start(const UObject* Object)
		{
			Brain->SetInput(TEXT("Target"), FADSKObjectHandles::Make(Object));
			Brain->StartLogic();
		}
		FString State() const { return Brain->GetInstance()->GetActiveStatePath().Last(); }
		AActor* Focus() const { return Controller.GetFocusActorForPriority(EAIFocusPriority::Gameplay); }
	};
}

using namespace FObjectHostSpecPrivate;

TEST_CLASS_WITH_FLAGS(FObjectHandlesSpec, "AdamskAI.Host.Objects.Handles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AnObjectKeepsItsIdAndIdsAreNeverReused)
	{
		FActorTestSpawner Spawner;
		AActor& First = Spawner.SpawnActor<AActor>();
		AActor& Second = Spawner.SpawnActor<AActor>();
		const FADSKValue FirstId = FADSKObjectHandles::Make(&First);
		ASSERT_THAT(IsTrue(FirstId.Type == EADSKValueType::Object && FirstId.ObjectId != 0, LexToString(FirstId)));
		ASSERT_THAT(IsTrue(FADSKObjectHandles::Make(&First) == FirstId, LexToString(FADSKObjectHandles::Make(&First))));
		ASSERT_THAT(IsTrue(FADSKObjectHandles::Make(&Second) != FirstId, TEXT("two actors share a handle")));
		ASSERT_THAT(IsTrue(FADSKObjectHandles::Make(nullptr) == FADSKValue::MakeObject(0), LexToString(FADSKObjectHandles::Make(nullptr))));
		ASSERT_THAT(IsTrue(FADSKObjectHandles::Resolve<AActor>(FirstId) == &First, TEXT("the handle does not resolve to its actor")));
		ASSERT_THAT(IsNull(FADSKObjectHandles::Resolve(FADSKValue::MakeObject(0))));
		ASSERT_THAT(IsNull(FADSKObjectHandles::Resolve(FADSKValue::MakeInt(static_cast<int32>(FirstId.ObjectId)))));

		First.Destroy();
		ASSERT_THAT(IsNull(FADSKObjectHandles::Resolve(FirstId)));
		AActor& Third = Spawner.SpawnActor<AActor>();
		const FADSKValue ThirdId = FADSKObjectHandles::Make(&Third);
		ASSERT_THAT(IsTrue(ThirdId.ObjectId > FirstId.ObjectId, FString::Printf(TEXT("ids %llu then %llu"), FirstId.ObjectId, ThirdId.ObjectId)));
	}

	TEST_METHOD(DescribeNamesTheObjectOrSaysItIsGone)
	{
		FActorTestSpawner Spawner;
		AActor& Actor = Spawner.SpawnActor<AActor>();
		const FADSKValue Id = FADSKObjectHandles::Make(&Actor);
		ASSERT_THAT(AreEqual(FString::Printf(TEXT("Object#%llu (%s)"), Id.ObjectId, *Actor.GetName()), FADSKObjectHandles::Describe(Id)));
		Actor.Destroy();
		ASSERT_THAT(IsTrue(FADSKObjectHandles::Describe(Id).EndsWith(TEXT("(destroyed)")), FADSKObjectHandles::Describe(Id)));
		ASSERT_THAT(AreEqual(FString(TEXT("none")), FADSKObjectHandles::Describe(FADSKValue::MakeObject(0))));
	}

	TEST_METHOD(DestroyedEntriesArePrunedAsTheTableGrows)
	{
		FActorTestSpawner Spawner;
		AActor& Kept = Spawner.SpawnActor<AActor>();
		const FADSKValue KeptId = FADSKObjectHandles::Make(&Kept);
		for (int32 Index = 0; Index < 600; ++Index)
		{
			AActor& Temporary = Spawner.SpawnActor<AActor>();
			FADSKObjectHandles::Make(&Temporary);
			Temporary.Destroy();
		}
		ASSERT_THAT(IsTrue(FADSKObjectHandles::GetTrackedCountForTest() < 600, FString::Printf(TEXT("%lld tracked handles"), static_cast<int64>(FADSKObjectHandles::GetTrackedCountForTest()))));
		ASSERT_THAT(IsTrue(FADSKObjectHandles::Resolve<AActor>(KeptId) == &Kept, TEXT("the kept actor's handle no longer resolves")));
		ASSERT_THAT(IsTrue(FADSKObjectHandles::Make(&Kept) == KeptId, LexToString(FADSKObjectHandles::Make(&Kept))));
	}
};

TEST_CLASS_WITH_FLAGS(FObjectNodesSpec, "AdamskAI.Host.Objects.Nodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(FocusActorFacesTheTargetAndFailsOnceItIsDestroyed)
	{
		FControlled F(TEXT("<FocusActor id=\"face\" in-target=\"Target\"><Wait id=\"w\" duration=\"100\"/></FocusActor>"));
		F.Start(F.Target);
		F.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(F.Focus() == F.Target, TEXT("the focus is ") + GetNameSafe(F.Focus())));
		ASSERT_THAT(AreEqual(FString(TEXT("s")), F.State()));
		F.Target->Destroy();
		F.Brain->TickBrain(0.1f);
		F.Brain->TickBrain(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), F.State()));
	}

	TEST_METHOD(FocusActorReleasesOnlyItsOwnFocus)
	{
		FControlled F(TEXT("<FocusActor id=\"face\" in-target=\"Target\"><Wait id=\"w\" duration=\"100\"/></FocusActor>"));
		F.Start(F.Target);
		F.Brain->TickBrain(0.1f);
		F.Brain->StopLogic();
		ASSERT_THAT(IsNull(F.Focus()));

		AActor& Other = F.Spawner.SpawnActor<AActor>();
		F.Brain->StartLogic();
		F.Brain->TickBrain(0.1f);
		F.Controller.SetFocus(&Other, EAIFocusPriority::Gameplay);
		F.Brain->StopLogic();
		ASSERT_THAT(IsTrue(F.Focus() == &Other, TEXT("the focus is ") + GetNameSafe(F.Focus())));
	}

	TEST_METHOD(AFocusScopeWithNoTargetFails)
	{
		FControlled F(TEXT("<FocusActor id=\"face\" in-target=\"Target\"><Wait id=\"w\" duration=\"100\"/></FocusActor>"));
		F.Start(nullptr);
		F.Brain->TickBrain(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), F.State()));
	}

	TEST_METHOD(LookAtActorSucceedsAfterItsDurationAndClearsFocus)
	{
		FControlled F(TEXT("<LookAtActor id=\"look\" in-target=\"Target\" duration=\"0.5\"/>"));
		F.Start(F.Target);
		F.Brain->TickBrain(0.25f);
		ASSERT_THAT(IsTrue(F.Focus() == F.Target, TEXT("the focus is ") + GetNameSafe(F.Focus())));
		F.Brain->TickBrain(0.3f);
		F.Brain->TickBrain(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("won")), F.State()));
		ASSERT_THAT(IsNull(F.Focus()));
	}

	TEST_METHOD(MoveToActorFailsWithoutATargetOrController)
	{
		FControlled F(TEXT("<MoveToActor id=\"move\" in-target=\"Target\" acceptanceRadius=\"10\"/>"));
		F.Start(nullptr);
		F.Brain->TickBrain(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), F.State()));
		ASSERT_THAT(IsFalse(F.Brain->GetActiveMoveRequest().IsValid(), TEXT("a move is still active")));

		AActor& Plain = F.Spawner.SpawnActor<AActor>();
		UADSKBrainComponent* PlainBrain = NewObject<UADSKBrainComponent>(&Plain);
		Plain.AddInstanceComponent(PlainBrain);
		PlainBrain->RegisterComponent();
		PlainBrain->InitializeDefinition(Load(TEXT("<MoveToActor id=\"move\" in-target=\"Target\"/>")), FADSKRecorder::Create(16));
		PlainBrain->SetInput(TEXT("Target"), FADSKObjectHandles::Make(F.Target));
		PlainBrain->StartLogic();
		PlainBrain->TickBrain(0.f);
		const FString PlainState = PlainBrain->GetInstance()->GetActiveStatePath().Last();
		PlainBrain->Cleanup();
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), PlainState));
	}

	TEST_METHOD(TheSnapshotNamesTheObjectAnInputHolds)
	{
		FControlled F(TEXT("<Wait id=\"w\" duration=\"100\"/>"));
		UADSKWorldSubsystem* Subsystem = F.Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		Subsystem->RegisterBrain(TEXT("debug/object.xml"), F.Brain);
		F.Start(F.Target);
		F.Brain->TickBrain(0.f);
		const FADSKDebugSnapshot Snapshot = FADSKDebugSnapshot::Collect(&F.Spawner.GetWorld());
		Subsystem->UnregisterBrain(TEXT("debug/object.xml"), F.Brain);
		ASSERT_THAT(AreEqual(1, Snapshot.Rows.Num()));
		const FADSKDebugNamedValue& Input = Snapshot.Rows[0].Inputs[0];
		ASSERT_THAT(AreEqual(FString(TEXT("Object")), Input.Type));
		ASSERT_THAT(IsTrue(Input.Value.EndsWith(FString::Printf(TEXT("(%s)"), *F.Target->GetName())), Input.Value));
	}
};

TEST_CLASS_WITH_FLAGS(FObjectPerceptionSpec, "AdamskAI.Host.Objects.Perception",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(TheBridgePublishesTheSelectedActorAndNoneWhenItIsGone)
	{
		TArray<FString> Errors;
		const TSharedPtr<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::BuildPluginCatalogue(Errors);
		const TCHAR* Xml = TEXT("<Agent version=\"1\" id=\"p\"><Inputs><Input name=\"Target\" type=\"Object\" default=\"none\"/></Inputs>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>");
		const FADSKCompileResult Loaded = FADSKCompiler::Compile(TEXT("p.xml"), Xml, Registry.ToSharedRef());
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));

		FActorTestSpawner Spawner;
		AActor& Host = Spawner.SpawnActor<AActor>();
		UAIPerceptionComponent* Perception = NewObject<UAIPerceptionComponent>(&Host);
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Host);
		UADSKPerceptionBridgeComponent* Bridge = NewObject<UADSKPerceptionBridgeComponent>(&Host);
		Host.AddInstanceComponent(Perception); Perception->RegisterComponent();
		Host.AddInstanceComponent(Brain); Brain->RegisterComponent();
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef());
		FADSKPerceptionBinding Binding;
		Binding.Sense = UAISense_Sight::StaticClass();
		Binding.TargetInput = TEXT("Target");
		Bridge->Bindings.Add(Binding);
		Host.AddInstanceComponent(Bridge); Bridge->RegisterComponent();
		if (!Bridge->HasBegunPlay()) { Bridge->BeginPlay(); }
		Brain->StartLogic();

		AActor& Seen = Spawner.SpawnActor<AActor>();
		Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &Seen, FVector(1, 0, 0), true);
		Brain->TickBrain(0.1f);
		FADSKValue Published;
		Brain->GetInstance()->GetValue(TEXT("Target"), Published);
		const bool bPublished = Published == FADSKObjectHandles::Make(&Seen);
		Seen.Destroy();
		Brain->TickBrain(0.1f);
		Brain->GetInstance()->GetValue(TEXT("Target"), Published);
		const bool bCleared = Published == FADSKValue::MakeObject(0);
		Brain->Cleanup();
		ASSERT_THAT(IsTrue(bPublished, TEXT("the sensed actor was not published to Target")));
		ASSERT_THAT(IsTrue(bCleared, TEXT("Target still names the destroyed actor")));
	}

	TEST_METHOD(ATargetInputMustBeAnObject)
	{
		FActorTestSpawner Spawner;
		AActor& Host = Spawner.SpawnActor<AActor>();
		UADSKPerceptionBridgeComponent* Bridge = NewObject<UADSKPerceptionBridgeComponent>(&Host);
		FADSKPerceptionBinding Binding;
		Binding.Sense = UAISense_Sight::StaticClass();
		Binding.TargetInput = TEXT("Target");
		Bridge->Bindings.Add(Binding);
		TArray<FString> Errors;
		const TSharedPtr<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::BuildPluginCatalogue(Errors);
		const TCHAR* Xml = TEXT("<Agent version=\"1\" id=\"p\"><Inputs><Input name=\"Target\" type=\"Vector\" default=\"0,0,0\"/></Inputs>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\"/></StateMachine></Agent>");
		const FADSKCompileResult Loaded = FADSKCompiler::Compile(TEXT("p.xml"), Xml, Registry.ToSharedRef());
		FString Error;
		ASSERT_THAT(IsFalse(Bridge->ValidateDefinitionForTest(*Loaded.Definition, Error), TEXT("the bridge accepted the definition")));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("exact input type")), Error));
	}
};

#endif
