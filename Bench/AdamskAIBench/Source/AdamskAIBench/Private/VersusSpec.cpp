// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "ADSKBenchTypes.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "Brain/ADSKBrainComponent.h"
#include "Compiler/ADSKCompiler.h"
#include "Components/StateTreeComponent.h"
#include "Components/StateTreeComponentSchema.h"
#include "CQTest.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Interfaces/IPluginManager.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Runtime/ADSKInstance.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StateTree.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeInstanceData.h"
#include "StateTreeState.h"
#include "Tasks/StateTreeDelayTask.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

/**
 * One behavior written three ways, each in its own idiom, and timed side by side.
 *
 * The behavior: patrol (count, wait 1 s, wait 1 s, again) until the host raises Alert, then alert (count, wait 0.25 s,
 * wait 0.25 s, again) until it lowers it; the switch interrupts whatever runs. The host flips Alert for each agent
 * every 1.5 s, staggered. Count is each system's own native task, so the entries counted show the three ran the same
 * behavior.
 *
 * - AdamskAI: a file whose Alert input raises a change event; a brain component ticking itself.
 * - Behavior Tree: a selector whose alert branch has a Blackboard decorator aborting both ways; RunBehaviorTree.
 * - StateTree: Patrol and Alert states with child steps, switched by events; a StateTree component.
 *
 * Benchmarks, not thresholds: a test fails only when a behavior does not compile or does not run.
 */
namespace FVersusSpecPrivate
{
	/**
	 * The StateTree's event tags, declared in the bench plugin's Config/Tags. A native tag needs a Runtime module and
	 * the engine reads no plugin's Config/Tags by itself, so the first use adds that directory.
	 */
	FGameplayTag Tag(const TCHAR* Name)
	{
		static const bool bLoaded = []()
		{
			if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AdamskAIBench")))
			{
				UGameplayTagsManager::Get().AddTagIniSearchPath(Plugin->GetBaseDir() / TEXT("Config") / TEXT("Tags"));
			}
			return true;
		}();
		return FGameplayTag::RequestGameplayTag(Name);
	}
	FGameplayTag AlertOn() { return Tag(TEXT("AdamskAIBench.AlertOn")); }
	FGameplayTag AlertOff() { return Tag(TEXT("AdamskAIBench.AlertOff")); }

	constexpr int32 Agents = 1000;
	constexpr int32 WarmupFrames = 60;
	constexpr int32 MeasuredFrames = 600;
	constexpr float FrameDelta = 1.f / 60.f;
	/** Each agent flips Alert every this many frames, starting at a frame of its own. */
	constexpr int32 FlipPeriod = 90;

	const TCHAR* Xml = TEXT(
		"<Agent version=\"1\" id=\"versus\">"
		"<Inputs><Input name=\"Alert\" type=\"Bool\" default=\"false\"/></Inputs>"
		"<Events><Event name=\"AlertChanged\" priority=\"10\" change=\"Alert\"/></Events>"
		"<StateMachine id=\"mode\" initial=\"patrol\">"
		"<State id=\"patrol\" tree=\"patrol\"><On event=\"AlertChanged\" goto=\"alert\" if=\"Alert\"/><On event=\"tree.success\" goto=\"patrol\"/></State>"
		"<State id=\"alert\" tree=\"alert\"><On event=\"AlertChanged\" goto=\"patrol\" unless=\"Alert\"/><On event=\"tree.success\" goto=\"alert\"/></State>"
		"</StateMachine>"
		"<Tree id=\"patrol\"><Sequence id=\"patrol_steps\"><BenchCount id=\"patrol_count\"/>"
		"<Wait id=\"patrol_a\" duration=\"1\"/><Wait id=\"patrol_b\" duration=\"1\"/></Sequence></Tree>"
		"<Tree id=\"alert\"><Sequence id=\"alert_steps\"><BenchCount id=\"alert_count\"/>"
		"<Wait id=\"alert_a\" duration=\"0.25\"/><Wait id=\"alert_b\" duration=\"0.25\"/></Sequence></Tree>"
		"</Agent>");

	bool IsFlipFrame(int32 Agent, int32 Frame) { return (Frame + Agent * 7) % FlipPeriod == 0; }

	TSharedPtr<const FADSKDefinition> MakeDefinition(FString& OutError)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		FADSKNodeDescriptor Count;
		Count.Name = TEXT("BenchCount");
		Count.Description = TEXT("Counts a branch entry and succeeds.");
		Count.bThreadSafe = true;
		Count.OnStart = [](FADSKNodeContext&) { ADSKBench::Count(EADSKBenchSystem::AdamskAI); return EADSKNodeStatus::Succeeded; };
		if (Registry->Register(MoveTemp(Count)) != EADSKRegisterResult::Accepted) { OutError = TEXT("BenchCount rejected"); return nullptr; }
		Registry->Freeze();
		const FADSKCompileResult Result = FADSKCompiler::Compile(TEXT("versus.xml"), Xml, Registry);
		if (!Result.Definition) { OutError = TEXT("versus.xml does not compile"); }
		return Result.Definition;
	}

	template <typename T>
	void SetByName(UObject& Object, UClass* Owner, const TCHAR* Name, const T& Value)
	{
		if (FProperty* Property = FindFProperty<FProperty>(Owner, Name))
		{
			if (FByteProperty* Byte = CastField<FByteProperty>(Property)) { Byte->SetIntPropertyValue(Byte->ContainerPtrToValuePtr<uint8>(&Object), (uint64)Value); }
			else if (FEnumProperty* Enum = CastField<FEnumProperty>(Property)) { Enum->GetUnderlyingProperty()->SetIntPropertyValue(Enum->ContainerPtrToValuePtr<uint8>(&Object), (int64)Value); }
		}
	}

	UBehaviorTree* MakeBehaviorTree()
	{
		UBlackboardData* Blackboard = NewObject<UBlackboardData>(GetTransientPackage());
		FBlackboardEntry Key;
		Key.EntryName = TEXT("Alert");
		Key.KeyType = NewObject<UBlackboardKeyType_Bool>(Blackboard);
		Blackboard->Keys.Add(Key);
		Blackboard->UpdateParentKeys();

		UBehaviorTree* Tree = NewObject<UBehaviorTree>(GetTransientPackage());
		Tree->BlackboardAsset = Blackboard;
		UBTComposite_Selector* Root = NewObject<UBTComposite_Selector>(Tree);
		Tree->RootNode = Root;

		auto AddBranch = [Tree, Root](float Wait) -> FBTCompositeChild&
		{
			UBTComposite_Sequence* Steps = NewObject<UBTComposite_Sequence>(Tree);
			Steps->Children.AddDefaulted_GetRef().ChildTask = NewObject<UADSKBenchBTCountTask>(Tree);
			for (int32 Step = 0; Step < 2; ++Step)
			{
				UBTTask_Wait* WaitTask = NewObject<UBTTask_Wait>(Tree);
				WaitTask->WaitTime = FValueOrBBKey_Float(Wait);
				Steps->Children.AddDefaulted_GetRef().ChildTask = WaitTask;
			}
			FBTCompositeChild& Child = Root->Children.AddDefaulted_GetRef();
			Child.ChildComposite = Steps;
			return Child;
		};

		FBTCompositeChild& Alert = AddBranch(0.25f);
		UBTDecorator_Blackboard* IsAlert = NewObject<UBTDecorator_Blackboard>(Tree);
		SetByName(*IsAlert, UBTDecorator_Blackboard::StaticClass(), TEXT("OperationType"), EBasicKeyOperation::Set);
		SetByName(*IsAlert, UBTDecorator::StaticClass(), TEXT("FlowAbortMode"), EBTFlowAbortMode::Both);
		if (FStructProperty* KeyProperty = FindFProperty<FStructProperty>(UBTDecorator_BlackboardBase::StaticClass(), TEXT("BlackboardKey")))
		{
			KeyProperty->ContainerPtrToValuePtr<FBlackboardKeySelector>(IsAlert)->SelectedKeyName = TEXT("Alert");
		}
		Alert.Decorators.Add(IsAlert);
		AddBranch(1.f);
		return Tree;
	}

	UStateTree* MakeStateTree(UStateTreeSchema* Schema, FString& OutError)
	{
		UStateTree* Tree = NewObject<UStateTree>(GetTransientPackage());
		UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(Tree);
		Tree->EditorData = EditorData;
		EditorData->Schema = Schema;

		UStateTreeState& Root = EditorData->AddSubTree(TEXT("Root"));
		UStateTreeState& Patrol = Root.AddChildState(TEXT("Patrol"));
		UStateTreeState& Alert = Root.AddChildState(TEXT("Alert"));
		auto AddSteps = [](UStateTreeState& Branch, const FString& Name, float Wait)
		{
			UStateTreeState& First = Branch.AddChildState(*(Name + TEXT("A")));
			First.AddTask<FADSKBenchSTCountTask>();
			First.AddTask<FStateTreeDelayTask>().GetInstanceData().Duration = Wait;
			First.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::NextState);
			UStateTreeState& Second = Branch.AddChildState(*(Name + TEXT("B")));
			Second.AddTask<FStateTreeDelayTask>().GetInstanceData().Duration = Wait;
			Second.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Branch);
		};
		AddSteps(Patrol, TEXT("Patrol"), 1.f);
		AddSteps(Alert, TEXT("Alert"), 0.25f);
		Patrol.AddTransition(EStateTreeTransitionTrigger::OnEvent, AlertOn(), EStateTreeTransitionType::GotoState, &Alert);
		Alert.AddTransition(EStateTreeTransitionTrigger::OnEvent, AlertOff(), EStateTreeTransitionType::GotoState, &Patrol);

		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		if (!Compiler.Compile(Tree) || !Tree->IsReadyToRun())
		{
			OutError = TEXT("the StateTree does not compile");
			return nullptr;
		}
		return Tree;
	}

	void ResetEntries() { FMemory::Memzero(ADSKBench::Entries); }

	int64 EntriesOf(EADSKBenchSystem System) { return ADSKBench::Entries[static_cast<int32>(System)]; }

	struct FTiming
	{
		double TotalMs = 0.0;
		double MedianMs = 0.0;
	};

	FTiming Summarize(TArray<double>& FrameMs)
	{
		FTiming Result;
		for (double Ms : FrameMs) { Result.TotalMs += Ms; }
		FrameMs.Sort();
		Result.MedianMs = FrameMs.IsEmpty() ? 0.0 : FrameMs[FrameMs.Num() / 2];
		return Result;
	}

	void Write(FAutomationTestBase& Test, const FString& Name, const TSharedRef<FJsonObject>& Numbers)
	{
		for (const auto& Pair : Numbers->Values)
		{
			Test.AddInfo(FString::Printf(TEXT("%s %s=%s"), *Name, *Pair.Key, *Pair.Value->AsString()));
		}
		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Numbers, Writer);
		const FString Path = FPaths::ProjectSavedDir() / TEXT("AdamskAI/perf") / (Name + TEXT(".json"));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveStringToFile(Json, *Path);
	}

	/**
	 * A game world of Agents AI controllers, each running the bench behavior in System (Count: none, the baseline).
	 * The engine's test world (a game instance, a game mode, begun play), ticked as a game ticks it.
	 */
	struct FWorldFleet
	{
		FTestWorldWrapper Wrapper;
		EADSKBenchSystem System;
		TArray<AAIController*> Controllers;
		TArray<UADSKBrainComponent*> Brains;
		TArray<UStateTreeComponent*> StateTrees;
		TArray<bool> Alert;
		FString Error;

		/** bManaged: AdamskAI brains on the world's managed pass instead of a component tick each. */
		FWorldFleet(EADSKBenchSystem InSystem, bool bManaged, const TSharedPtr<const FADSKDefinition>& Definition, UBehaviorTree* BehaviorTree, UStateTree* StateTree)
			: System(InSystem)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Error = TEXT("the test world did not start");
				return;
			}
			UWorld& World = *Wrapper.GetTestWorld();
			// A Behavior Tree needs the AI system for its manager and blackboards; the test world does not make one.
			if (!World.GetAISystem() && World.CreateAISystem()) { World.GetAISystem()->StartPlay(); }
			if (!World.GetAISystem())
			{
				Error = TEXT("the test world has no AI system");
				return;
			}
			Alert.Init(false, Agents);
			for (int32 Agent = 0; Agent < Agents; ++Agent)
			{
				AAIController& Controller = *World.SpawnActor<AAIController>();
				Controllers.Add(&Controller);
				switch (System)
				{
				case EADSKBenchSystem::AdamskAI:
				{
					UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Controller);
					Brain->bAutoTick = !bManaged;
					Brain->bManagedTick = bManaged;
					Brain->ManagedTickInterval = 0.f;
					Controller.AddInstanceComponent(Brain);
					Brain->RegisterComponent();
					Brain->InitializeDefinition(Definition.ToSharedRef());
					Brain->StartLogic();
					if (!Brain->IsRunning()) { Error = TEXT("an AdamskAI brain did not start: ") + Brain->GetLastError(); }
					Brains.Add(Brain);
					break;
				}
				case EADSKBenchSystem::BehaviorTree:
					if (!Controller.RunBehaviorTree(BehaviorTree)) { Error = TEXT("RunBehaviorTree failed"); }
					break;
				case EADSKBenchSystem::StateTree:
				{
					UStateTreeComponent* Component = NewObject<UStateTreeComponent>(&Controller);
					Component->SetStartLogicAutomatically(false);
					Component->SetStateTree(StateTree);
					Controller.AddInstanceComponent(Component);
					Component->RegisterComponent();
					Component->StartLogic();
					if (!Component->IsRunning()) { Error = TEXT("a StateTree component did not start"); }
					StateTrees.Add(Component);
					break;
				}
				default:
					break;
				}
			}
		}

		~FWorldFleet()
		{
			if (Wrapper.GetTestWorld()) { Wrapper.DestroyTestWorld(true); }
		}

		void Publish(int32 Frame)
		{
			for (int32 Agent = 0; Agent < Agents; ++Agent)
			{
				if (!IsFlipFrame(Agent, Frame)) { continue; }
				const bool bAlert = Alert[Agent] = !Alert[Agent];
				switch (System)
				{
				case EADSKBenchSystem::AdamskAI: Brains[Agent]->SetInput(TEXT("Alert"), FADSKValue::MakeBool(bAlert)); break;
				case EADSKBenchSystem::BehaviorTree: Controllers[Agent]->GetBlackboardComponent()->SetValueAsBool(TEXT("Alert"), bAlert); break;
				case EADSKBenchSystem::StateTree: StateTrees[Agent]->SendStateTreeEvent(bAlert ? AlertOn() : AlertOff()); break;
				default: break;
				}
			}
		}

		/** Milliseconds of each measured frame's world tick; publishing is not timed. */
		TArray<double> Run()
		{
			TArray<double> FrameMs;
			FrameMs.Reserve(MeasuredFrames);
			for (int32 Frame = 0; Frame < WarmupFrames + MeasuredFrames; ++Frame)
			{
				if (Frame == WarmupFrames) { ResetEntries(); }
				Publish(Frame);
				const double Start = FPlatformTime::Seconds();
				Wrapper.TickTestWorld(FrameDelta);
				if (Frame >= WarmupFrames) { FrameMs.Add((FPlatformTime::Seconds() - Start) * 1000.0); }
			}
			return FrameMs;
		}
	};

	/** One column of the world table. */
	struct FBenchVariant
	{
		EADSKBenchSystem System;
		bool bManaged;
		const TCHAR* Name;
	};

	const FBenchVariant Variants[] = {
		{EADSKBenchSystem::Count, false, TEXT("baseline")},
		{EADSKBenchSystem::AdamskAI, false, TEXT("adamskai")},
		{EADSKBenchSystem::AdamskAI, true, TEXT("adamskai_managed")},
		{EADSKBenchSystem::BehaviorTree, false, TEXT("behavior_tree")},
		{EADSKBenchSystem::StateTree, false, TEXT("state_tree")},
	};
}

using namespace FVersusSpecPrivate;

TEST_CLASS_WITH_FLAGS(FVersusSpec, "AdamskAIBench.Versus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	/** 1000 AI controllers in a game world, each with one system's component; the world tick against a baseline of bare controllers. */
	TEST_METHOD(World)
	{
		FString Error;
		const TSharedPtr<const FADSKDefinition> Definition = MakeDefinition(Error);
		ASSERT_THAT(IsTrue(Definition.IsValid(), Error));
		TStrongObjectPtr<UBehaviorTree> BehaviorTree(MakeBehaviorTree());
		TStrongObjectPtr<UStateTree> StateTree(MakeStateTree(NewObject<UStateTreeComponentSchema>(), Error));
		ASSERT_THAT(IsTrue(StateTree.IsValid(), Error));

		const TSharedRef<FJsonObject> Numbers = MakeShared<FJsonObject>();
		Numbers->SetNumberField(TEXT("agents"), Agents);
		Numbers->SetNumberField(TEXT("frames"), MeasuredFrames);
		double BaselineMs = 0.0;
		for (const FBenchVariant& Variant : Variants)
		{
			const EADSKBenchSystem System = Variant.System;
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			TArray<double> FrameMs;
			{
				FWorldFleet Fleet(System, Variant.bManaged, Definition, BehaviorTree.Get(), StateTree.Get());
				ASSERT_THAT(IsTrue(Fleet.Error.IsEmpty(), Fleet.Error));
				FrameMs = Fleet.Run();
			}
			const FTiming Timing = Summarize(FrameMs);
			const FString Name = Variant.Name;
			const double PerFrame = Timing.TotalMs / MeasuredFrames;
			Numbers->SetNumberField(Name + TEXT("_frame_ms"), PerFrame);
			Numbers->SetNumberField(Name + TEXT("_median_frame_ms"), Timing.MedianMs);
			if (System == EADSKBenchSystem::Count)
			{
				BaselineMs = PerFrame;
				continue;
			}
			Numbers->SetNumberField(Name + TEXT("_cost_ms"), PerFrame - BaselineMs);
			Numbers->SetNumberField(Name + TEXT("_cost_us_per_agent"), (PerFrame - BaselineMs) * 1000.0 / Agents);
			Numbers->SetNumberField(Name + TEXT("_entries"), static_cast<double>(EntriesOf(System)));
			ASSERT_THAT(IsTrue(EntriesOf(System) > Agents, FString::Printf(TEXT("%s ran %lld branches"), *Name, EntriesOf(System))));
		}
		Write(*TestRunner, TEXT("versus_world"), Numbers);
	}

	/** The cores alone, no world or component: 1000 AdamskAI instances against 1000 StateTree instance data, ticked every frame. */
	TEST_METHOD(Core)
	{
		FString Error;
		const TSharedPtr<const FADSKDefinition> Definition = MakeDefinition(Error);
		ASSERT_THAT(IsTrue(Definition.IsValid(), Error));
		TStrongObjectPtr<UStateTree> StateTree(MakeStateTree(NewObject<UADSKBenchSchema>(), Error));
		ASSERT_THAT(IsTrue(StateTree.IsValid(), Error));
		TStrongObjectPtr<UObject> Owner(NewObject<UADSKBenchSchema>());
		const TSharedRef<FJsonObject> Numbers = MakeShared<FJsonObject>();
		Numbers->SetNumberField(TEXT("agents"), Agents);
		Numbers->SetNumberField(TEXT("frames"), MeasuredFrames);

		// AdamskAI.
		{
			TArray<TUniquePtr<FADSKInstance>> Instances;
			for (int32 Agent = 0; Agent < Agents; ++Agent)
			{
				Instances.Add(MakeUnique<FADSKInstance>(Definition.ToSharedRef()));
				Instances.Last()->Start();
			}
			TArray<bool> Alert;
			Alert.Init(false, Agents);
			TArray<double> FrameMs;
			for (int32 Frame = 0; Frame < WarmupFrames + MeasuredFrames; ++Frame)
			{
				if (Frame == WarmupFrames) { ResetEntries(); }
				for (int32 Agent = 0; Agent < Agents; ++Agent)
				{
					if (IsFlipFrame(Agent, Frame)) { Alert[Agent] = !Alert[Agent]; Instances[Agent]->SetInput(TEXT("Alert"), FADSKValue::MakeBool(Alert[Agent])); }
				}
				const double Start = FPlatformTime::Seconds();
				for (const TUniquePtr<FADSKInstance>& Instance : Instances) { Instance->Tick(FrameDelta); }
				if (Frame >= WarmupFrames) { FrameMs.Add((FPlatformTime::Seconds() - Start) * 1000.0); }
			}
			const FTiming Timing = Summarize(FrameMs);
			Numbers->SetNumberField(TEXT("adamskai_frame_ms"), Timing.TotalMs / MeasuredFrames);
			Numbers->SetNumberField(TEXT("adamskai_us_per_agent"), Timing.TotalMs * 1000.0 / MeasuredFrames / Agents);
			Numbers->SetNumberField(TEXT("adamskai_entries"), static_cast<double>(EntriesOf(EADSKBenchSystem::AdamskAI)));
		}

		// StateTree.
		{
			TArray<FStateTreeInstanceData> Data;
			Data.SetNum(Agents);
			for (FStateTreeInstanceData& One : Data)
			{
				FStateTreeExecutionContext Context(*Owner, *StateTree, One);
				ASSERT_THAT(IsTrue(Context.Start() == EStateTreeRunStatus::Running, TEXT("a StateTree instance did not start")));
			}
			TArray<bool> Alert;
			Alert.Init(false, Agents);
			TArray<double> FrameMs;
			for (int32 Frame = 0; Frame < WarmupFrames + MeasuredFrames; ++Frame)
			{
				if (Frame == WarmupFrames) { ResetEntries(); }
				for (int32 Agent = 0; Agent < Agents; ++Agent)
				{
					if (!IsFlipFrame(Agent, Frame)) { continue; }
					Alert[Agent] = !Alert[Agent];
					FStateTreeMinimalExecutionContext Context(*Owner, *StateTree, Data[Agent]);
					Context.SendEvent(Alert[Agent] ? AlertOn() : AlertOff());
				}
				const double Start = FPlatformTime::Seconds();
				for (FStateTreeInstanceData& One : Data)
				{
					FStateTreeExecutionContext Context(*Owner, *StateTree, One);
					Context.Tick(FrameDelta);
				}
				if (Frame >= WarmupFrames) { FrameMs.Add((FPlatformTime::Seconds() - Start) * 1000.0); }
			}
			const FTiming Timing = Summarize(FrameMs);
			Numbers->SetNumberField(TEXT("state_tree_frame_ms"), Timing.TotalMs / MeasuredFrames);
			Numbers->SetNumberField(TEXT("state_tree_us_per_agent"), Timing.TotalMs * 1000.0 / MeasuredFrames / Agents);
			Numbers->SetNumberField(TEXT("state_tree_entries"), static_cast<double>(EntriesOf(EADSKBenchSystem::StateTree)));
			Numbers->SetNumberField(TEXT("state_tree_instance_bytes"), Data[0].GetEstimatedMemoryUsage());
			for (FStateTreeInstanceData& One : Data)
			{
				FStateTreeExecutionContext Context(*Owner, *StateTree, One);
				Context.Stop();
			}
		}
		ASSERT_THAT(IsTrue(EntriesOf(EADSKBenchSystem::StateTree) > Agents, TEXT("the StateTree instances ran no branches")));
		Write(*TestRunner, TEXT("versus_core"), Numbers);
	}
};

#endif
