// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Bridges/ADSKPerceptionBridgeComponent.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "HAL/FileManager.h"
#include "Settings/ADSKSettings.h"
#include "Sources/ADSKValidation.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Components/ActorTestSpawner.h"
#include "Components/SceneComponent.h"
#include "CQTest.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Runtime/ADSKParallelTick.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Support/CompilerSpecSupport.h"
#include "Support/TestChecks.h"
#include "Support/FakeHostServices.h"
#include "Support/TempTree.h"

/** Benchmarks, not thresholds: timings vary by machine, so a test fails only when the workload itself breaks. */
namespace FPerfSpecPrivate
{
	/** A patrol, rest and alert loop of core nodes only, so the core is measured alone and may tick on workers. */
	const TCHAR* BenchXml = TEXT(
		"<Agent version=\"1\" id=\"bench\">"
		"<Inputs><Input name=\"Distance\" type=\"Float\" default=\"1000\"/><Input name=\"Reach\" type=\"Float\" default=\"300\"/>"
		"<Input name=\"Target\" type=\"Vector\" default=\"0,0,0\"/></Inputs>"
		"<Locals><Local name=\"Plan\" type=\"Int\"/></Locals>"
		"<Events><Event name=\"Noise\" priority=\"5\"/></Events>"
		"<StateMachine id=\"m\" initial=\"calm\">"
		"<State id=\"calm\" initial=\"patrol\"><On event=\"Noise\" goto=\"alert\"/>"
		"<State id=\"patrol\" tree=\"patrol\"><On event=\"tree.success\" goto=\"rest\" if=\"Plan\" is=\"0\"/>"
		"<On event=\"tree.success\" goto=\"scan\"/><On event=\"tree.failure\" goto=\"rest\"/></State>"
		"<State id=\"scan\" tree=\"scan\"><On event=\"tree.success\" goto=\"patrol\"/></State>"
		"<State id=\"rest\" tree=\"rest\"><On event=\"tree.success\" goto=\"patrol\"/></State>"
		"</State>"
		"<State id=\"alert\" tree=\"alert\"><On event=\"tree.success\" goto=\"calm\" if=\"Distance\" above=\"Reach\"/>"
		"<On event=\"tree.success\" goto=\"scan\"/><On event=\"tree.failure\" goto=\"calm\"/></State>"
		"</StateMachine>"
		"<Tree id=\"patrol\"><Sequence id=\"patrol_steps\"><Roll id=\"patrol_roll\" count=\"3\" out-value=\"Plan\"/>"
		"<Selector id=\"patrol_pick\"><Guard id=\"patrol_near\" in-condition=\"Distance\" below=\"Reach\"><Wait id=\"patrol_close\" duration=\"0.3\"/></Guard>"
		"<Wait id=\"patrol_walk\" duration=\"1\" deviation=\"0.5\"/></Selector></Sequence></Tree>"
		"<Tree id=\"scan\"><RandomSelector id=\"scan_pick\"><Wait id=\"scan_left\" duration=\"0.4\"/><Wait id=\"scan_right\" duration=\"0.6\"/></RandomSelector></Tree>"
		"<Tree id=\"rest\"><ForceSuccess id=\"rest_any\"><Sequence id=\"rest_steps\"><Chance id=\"rest_chance\" probability=\"0.5\"/>"
		"<Wait id=\"rest_wait\" duration=\"0.5\"/></Sequence></ForceSuccess></Tree>"
		"<Tree id=\"alert\"><TimeLimit id=\"alert_limit\" duration=\"3\"><Repeat id=\"alert_loop\"><Wait id=\"alert_look\" duration=\"0.25\"/></Repeat></TimeLimit></Tree>"
		"</Agent>");

	constexpr float FrameDelta = 1.f / 30.f;

	TSharedRef<const FADSKDefinition> LoadBench()
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		Registry->Freeze();
		return ADSKTest::LoadOrStandIn(TEXT("bench.xml"), BenchXml, Registry);
	}

	void PublishFrame(FADSKInstance& Instance, int32 Agent, int32 Frame)
	{
		const bool bMoved = (Agent + Frame) % 10 == 0;
		Instance.SetInput(TEXT("Distance"), FADSKValue::MakeFloat(bMoved ? static_cast<float>((Agent * 37 + Frame * 11) % 1200) : 1000.f));
		Instance.SetInput(TEXT("Target"), FADSKValue::MakeVector(FVector(100, 200, 0)));
		if ((Agent + Frame) % 90 == 0)
		{
			Instance.EnqueueEvent(TEXT("Noise"));
		}
	}

	struct FFleet
	{
		TArray<TSharedPtr<FADSKRecorder>> Recorders;
		TArray<TUniquePtr<FADSKInstance>> Instances;
		TArray<FADSKInstance*> Pointers;

		FFleet(const TSharedRef<const FADSKDefinition>& Definition, int32 Count, bool bRecord = true)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Recorders.Add(bRecord ? FADSKRecorder::Create(256) : nullptr);
				Instances.Add(MakeUnique<FADSKInstance>(Definition, Recorders.Last()));
				Instances.Last()->SetRandomSeed(Index + 1);
				Pointers.Add(Instances.Last().Get());
				ADSKTest::Expect(ADSKTest::Start(*Instances.Last()));
			}
		}

		void Publish(int32 Frame)
		{
			for (int32 Agent = 0; Agent < Instances.Num(); ++Agent)
			{
				PublishFrame(*Instances[Agent], Agent, Frame);
			}
		}

		int32 Running() const
		{
			int32 Count = 0;
			for (const TUniquePtr<FADSKInstance>& Instance : Instances)
			{
				Count += Instance->IsRunning() ? 1 : 0;
			}
			return Count;
		}

		uint64 Recorded() const
		{
			uint64 Total = 0;
			for (const TSharedPtr<FADSKRecorder>& Recorder : Recorders)
			{
				Total += Recorder ? Recorder->GetTotalRecorded() : 0;
			}
			return Total;
		}

		/** Seconds spent in the serial ticks of Frames frames; the publishing is not timed. */
		double TickSerially(int32 Frames)
		{
			double Seconds = 0.0;
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Publish(Frame);
				const double Start = FPlatformTime::Seconds();
				for (FADSKInstance* Instance : Pointers)
				{
					Instance->Tick(FrameDelta);
				}
				Seconds += FPlatformTime::Seconds() - Start;
			}
			return Seconds;
		}
	};

	UADSKBrainComponent* SpawnManagedBrain(FActorTestSpawner& Spawner, const TSharedRef<const FADSKDefinition>& Definition, int32 Seed)
	{
		AActor& Host = Spawner.SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(&Host);
		Host.AddInstanceComponent(Root);
		Root->RegisterComponent();
		Host.SetRootComponent(Root);
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Host);
		Brain->bManagedTick = true;
		Brain->ManagedTickInterval = 0.f;
		Brain->RandomSeed = Seed;
		Host.AddInstanceComponent(Brain);
		Brain->RegisterComponent();
		if (!Brain->HasBegunPlay()) Brain->BeginPlay();
		Brain->InitializeDefinition(Definition);
		Brain->StartLogic();
		return Brain;
	}

	/** A tree that is one SimpleParallel ending every few frames, so the fleet starts and cancels lanes all the time. */
	const TCHAR* ParallelXml = TEXT(
		"<Agent version=\"1\" id=\"parallel_bench\">"
		"<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"><On event=\"tree.success\" goto=\"s\"/></State></StateMachine>"
		"<Tree id=\"t\"><SimpleParallel id=\"both\"><Wait id=\"main\" duration=\"0.1\"/><Wait id=\"side\" duration=\"0.05\"/></SimpleParallel></Tree>"
		"</Agent>");

	/** What a perception-bridge binding writes; the tree only waits, so the tick is the bridge plus the core. */
	const TCHAR* BridgeXml = TEXT(
		"<Agent version=\"1\" id=\"bridge_bench\">"
		"<Inputs><Input name=\"Has\" type=\"Bool\" default=\"false\"/><Input name=\"Location\" type=\"Vector\" default=\"0,0,0\"/></Inputs>"
		"<Events><Event name=\"Seen\"/><Event name=\"Lost\"/></Events>"
		"<StateMachine id=\"m\" initial=\"idle\"><State id=\"idle\" tree=\"wait\"><On event=\"Seen\" goto=\"active\"/></State>"
		"<State id=\"active\" tree=\"wait\"><On event=\"Lost\" goto=\"idle\"/></State></StateMachine>"
		"<Tree id=\"wait\"><Wait id=\"wait_node\" duration=\"1000\"/></Tree></Agent>");

	AActor& SpawnPlaced(FActorTestSpawner& Spawner, const FVector& Location)
	{
		AActor& Actor = Spawner.SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(&Actor);
		Actor.AddInstanceComponent(Root);
		Root->RegisterComponent();
		Actor.SetRootComponent(Root);
		Actor.SetActorLocation(Location);
		return Actor;
	}

	void Report(FAutomationTestBase& Test, const FString& Name, const TSharedRef<FJsonObject>& Numbers)
	{
		for (const auto& Pair : Numbers->Values)
		{
			Test.AddInfo(FString::Printf(TEXT("%s %s=%s"), *Name, *Pair.Key, *Pair.Value->AsString()));
		}
		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Numbers, Writer);
		const FString Path = FPaths::ProjectSavedDir() / TEXT("AdamskAI/perf") / (Name + TEXT(".json"));
		ADSKTest::Expect(FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM), TEXT("cannot write ") + Path);
	}
}

using namespace FPerfSpecPrivate;

TEST_CLASS_WITH_FLAGS(FPerfSpec, "AdamskAI.Perf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(CoreFleet)
	{
		constexpr int32 Agents = 1000;
		constexpr int32 Frames = 300;
		const TSharedRef<const FADSKDefinition> Definition = LoadBench();
		ASSERT_THAT(IsTrue(Definition->IsThreadSafe(), TEXT("the bench definition must stay core-only")));

		// Only the ticks are timed; publishing the inputs is the host's cost and the same in both passes.
		FFleet Serial(Definition, Agents);
		const double SerialSeconds = Serial.TickSerially(Frames);
		ASSERT_THAT(AreEqual(Agents, Serial.Running()));

		// What Shipping pays: no recorder at all.
		FFleet Bare(Definition, Agents, false);
		const double BareSeconds = Bare.TickSerially(Frames);
		ASSERT_THAT(AreEqual(Agents, Bare.Running()));

		FFleet Parallel(Definition, Agents);
		TArray<float> Deltas;
		Deltas.Init(FrameDelta, Agents);
		TArray<EADSKTickResult> Results;
		Results.SetNumZeroed(Agents);
		double ParallelSeconds = 0.0;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Parallel.Publish(Frame);
			const double Start = FPlatformTime::Seconds();
			FADSKParallelTick::Tick(Parallel.Pointers, Deltas, Results);
			ParallelSeconds += FPlatformTime::Seconds() - Start;
		}
		ASSERT_THAT(AreEqual(Agents, Parallel.Running()));

		const uint64 InputSets = [&Serial]
		{
			uint64 Total = 0;
			for (const TSharedPtr<FADSKRecorder>& Recorder : Serial.Recorders) Total += ADSKTest::CountRecords(Recorder.Get(), EADSKRecordKind::InputSet);
			return Total;
		}();
		uint64 RetainedRecords = 0;
		for (const TSharedPtr<FADSKRecorder>& Recorder : Serial.Recorders) RetainedRecords += Recorder->Num();

		const TSharedRef<FJsonObject> Numbers = MakeShared<FJsonObject>();
		Numbers->SetNumberField(TEXT("agents"), Agents);
		Numbers->SetNumberField(TEXT("frames"), Frames);
		Numbers->SetNumberField(TEXT("serial_ms_per_frame"), SerialSeconds * 1000.0 / Frames);
		Numbers->SetNumberField(TEXT("serial_us_per_agent_tick"), SerialSeconds * 1e6 / (static_cast<double>(Frames) * Agents));
		Numbers->SetNumberField(TEXT("serial_ms_per_frame_no_recorder"), BareSeconds * 1000.0 / Frames);
		Numbers->SetNumberField(TEXT("serial_us_per_agent_tick_no_recorder"), BareSeconds * 1e6 / (static_cast<double>(Frames) * Agents));
		Numbers->SetNumberField(TEXT("parallel_ms_per_frame"), ParallelSeconds * 1000.0 / Frames);
		Numbers->SetNumberField(TEXT("node_memory_bytes_per_agent"), Serial.Instances[0]->GetNodeMemoryBytes());
		Numbers->SetNumberField(TEXT("instance_bytes_per_agent"), static_cast<double>(Bare.Instances[0]->GetAllocatedBytes()));
		Numbers->SetNumberField(TEXT("recorder_bytes_per_agent_at_256"), static_cast<double>(Serial.Recorders[0]->GetAllocatedBytes()));
		Numbers->SetNumberField(TEXT("recorder_bytes_per_agent_at_512"), static_cast<double>(FADSKRecorder::Create(512)->GetAllocatedBytes()));
		Numbers->SetNumberField(TEXT("record_bytes"), static_cast<double>(sizeof(FADSKRecord)));
		Numbers->SetNumberField(TEXT("records_per_agent_frame"), static_cast<double>(Serial.Recorded()) / (static_cast<double>(Frames) * Agents));
		Numbers->SetNumberField(TEXT("input_set_share_of_retained"), RetainedRecords ? static_cast<double>(InputSets) / RetainedRecords : 0.0);
		Report(*TestRunner, TEXT("CoreFleet"), Numbers);
	}

	TEST_METHOD(ParallelChurn)
	{
		constexpr int32 Agents = 1000;
		constexpr int32 Frames = 300;
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		Registry->Freeze();
		const TSharedRef<const FADSKDefinition> Definition = ADSKTest::LoadOrStandIn(TEXT("parallel_bench.xml"), ParallelXml, Registry);

		FFleet Fleet(Definition, Agents, false);
		double Seconds = 0.0;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			const double Start = FPlatformTime::Seconds();
			for (FADSKInstance* Instance : Fleet.Pointers) { Instance->Tick(FrameDelta); }
			Seconds += FPlatformTime::Seconds() - Start;
		}
		ASSERT_THAT(AreEqual(Agents, Fleet.Running()));

		// The same fleet on workers, where every lane allocation would also contend for the allocator.
		FFleet OnWorkers(Definition, Agents, false);
		TArray<float> Deltas;
		Deltas.Init(FrameDelta, Agents);
		TArray<EADSKTickResult> Results;
		Results.SetNumZeroed(Agents);
		double ParallelSeconds = 0.0;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			const double Start = FPlatformTime::Seconds();
			FADSKParallelTick::Tick(OnWorkers.Pointers, Deltas, Results);
			ParallelSeconds += FPlatformTime::Seconds() - Start;
		}
		ASSERT_THAT(AreEqual(Agents, OnWorkers.Running()));

		// One recorded agent counts how often the parallel starts in the same frames.
		const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(Frames * 32);
		FADSKInstance Counted(Definition, Recorder);
		ADSK_ASSERT(ADSKTest::Start(Counted));
		for (int32 Frame = 0; Frame < Frames; ++Frame) { Counted.Tick(FrameDelta); }
		const int32 Both = Definition->FindNode(TEXT("both"));
		const int32 Starts = Recorder->Snapshot().FilterByPredicate([Both](const FADSKRecord& Record) { return Record.Kind == EADSKRecordKind::NodeStarted && Record.Node == Both; }).Num();
		ASSERT_THAT(IsTrue(Starts > Frames / 10, FString::Printf(TEXT("only %d parallel starts"), Starts)));

		const TSharedRef<FJsonObject> Numbers = MakeShared<FJsonObject>();
		Numbers->SetNumberField(TEXT("agents"), Agents);
		Numbers->SetNumberField(TEXT("frames"), Frames);
		Numbers->SetNumberField(TEXT("parallel_starts_per_agent"), Starts);
		Numbers->SetNumberField(TEXT("serial_ms_per_frame"), Seconds * 1000.0 / Frames);
		Numbers->SetNumberField(TEXT("serial_us_per_agent_tick"), Seconds * 1e6 / (static_cast<double>(Frames) * Agents));
		Numbers->SetNumberField(TEXT("parallel_ms_per_frame"), ParallelSeconds * 1000.0 / Frames);
		Report(*TestRunner, TEXT("ParallelChurn"), Numbers);
	}

	/** Per-brain time growing with the fleet means superlinear work. */
	TEST_METHOD(ManagedTick)
	{
		constexpr int32 Frames = 100;
		const TSharedRef<const FADSKDefinition> Definition = LoadBench();
		const TSharedRef<FJsonObject> Numbers = MakeShared<FJsonObject>();
		for (const int32 Brains : {250, 1000})
		{
			FActorTestSpawner Spawner;
			for (int32 Index = 0; Index < Brains; ++Index)
			{
				SpawnManagedBrain(Spawner, Definition, Index + 1);
			}
			UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));
			ASSERT_THAT(AreEqual(Brains, Subsystem->GetManagedBrainCount()));
			// No viewers: every brain ticks every frame, so the pass does the most work it can.
			FADSKScopedFakeHostServices Services(Spawner.GetWorld());
			Services->Viewers.Emplace();
			const double Start = FPlatformTime::Seconds();
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Subsystem->TickManagedBrains(FrameDelta);
			}
			const double Seconds = FPlatformTime::Seconds() - Start;
			Numbers->SetNumberField(FString::Printf(TEXT("brains_%d_ms_per_frame"), Brains), Seconds * 1000.0 / Frames);
			Numbers->SetNumberField(FString::Printf(TEXT("brains_%d_us_per_brain_tick"), Brains), Seconds * 1e6 / (static_cast<double>(Frames) * Brains));
		}
		Report(*TestRunner, TEXT("ManagedTick"), Numbers);
	}

	/**
	 * What each Game/PIE world pays to compile the project's behavior files, since every world
	 * compiles its own (a dedicated server per map change, PIE once per client). Read plus compile, best of 10.
	 */
	TEST_METHOD(ProjectCompile)
	{
		TArray<FString> Errors;
		const TSharedPtr<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::TryGet(Errors);
		ASSERT_THAT(IsTrue(Registry.IsValid(), TEXT("the catalogue did not assemble: ") + FString::Join(Errors, TEXT("; "))));
		const FString Root = UADSKSettings::GetBehaviorRootDir();
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.xml"), true, false);
		Files.Sort();
		const TSharedRef<FJsonObject> Numbers = MakeShared<FJsonObject>();
		double Total = 0.0;
		for (const FString& File : Files)
		{
			FString Relative = File;
			FPaths::MakePathRelativeTo(Relative, *(Root / TEXT("")));
			double Best = TNumericLimits<double>::Max();
			for (int32 Round = 0; Round < 10; ++Round)
			{
				const double Start = FPlatformTime::Seconds();
				const FADSKSourceReadResult Read = FADSKValidation::ReadSource(Root, Relative, Registry.ToSharedRef());
				Best = FMath::Min(Best, FPlatformTime::Seconds() - Start);
				ASSERT_THAT(IsTrue(Read.Result.Definition.IsValid(), Relative + TEXT(" does not compile")));
			}
			Numbers->SetNumberField(Relative + TEXT("_compile_ms"), Best * 1000.0);
			Total += Best;
		}
		Numbers->SetNumberField(TEXT("files"), Files.Num());
		Numbers->SetNumberField(TEXT("all_files_compile_ms_per_world"), Total * 1000.0);
		Report(*TestRunner, TEXT("ProjectCompile"), Numbers);
	}

	/**
	 * What a Start and a Stop cost per instance (every restart, reload and re-possession pays them), and what one reload
	 * commit costs a world of brains on the same file: prepare, stop, publish, restart.
	 */
	TEST_METHOD(StartCost)
	{
		constexpr int32 Agents = 1000;
		constexpr int32 Rounds = 20;
		const TSharedRef<const FADSKDefinition> Definition = LoadBench();
		TArray<TUniquePtr<FADSKInstance>> Instances;
		for (int32 Index = 0; Index < Agents; ++Index)
		{
			Instances.Add(MakeUnique<FADSKInstance>(Definition));
			Instances.Last()->SetRandomSeed(Index + 1);
		}
		double StartSeconds = 0.0;
		double StopSeconds = 0.0;
		for (int32 Round = 0; Round < Rounds; ++Round)
		{
			double Begin = FPlatformTime::Seconds();
			for (const TUniquePtr<FADSKInstance>& Instance : Instances) Instance->Start();
			StartSeconds += FPlatformTime::Seconds() - Begin;
			// One untimed tick, so the stop ends running nodes as a live brain's does.
			for (const TUniquePtr<FADSKInstance>& Instance : Instances) Instance->Tick(FrameDelta);
			int32 Running = 0;
			for (const TUniquePtr<FADSKInstance>& Instance : Instances) Running += Instance->IsRunning() ? 1 : 0;
			ASSERT_THAT(AreEqual(Agents, Running));
			Begin = FPlatformTime::Seconds();
			for (const TUniquePtr<FADSKInstance>& Instance : Instances) Instance->Stop();
			StopSeconds += FPlatformTime::Seconds() - Begin;
		}

		constexpr int32 Brains = 200;
		constexpr int32 Commits = 10;
		const TCHAR* File = TEXT("start_cost.xml");
		FADSKTempTree Tree;
		FActorTestSpawner Spawner;
		UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Subsystem));
		Subsystem->SetTestRoot(Tree.Root);
		Subsystem->SetTestRegistry(FCompilerSpecSupport::MakePathProbeRegistry());
		Tree.Write(File, FString(BenchXml).Replace(TEXT("duration=\"0.3\""), TEXT("duration=\"0.30\"")));
		TArray<UADSKBrainComponent*> World;
		for (int32 Index = 0; Index < Brains; ++Index)
		{
			AActor& Host = Spawner.SpawnActor<AActor>();
			UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Host);
			Host.AddInstanceComponent(Brain);
			Brain->RegisterComponent();
			ASSERT_THAT(IsTrue(Brain->InitializeFromSource(File), Brain->GetLastError()));
			Brain->StartLogic();
			World.Add(Brain);
		}
		double CommitSeconds = 0.0;
		double BestCommit = TNumericLimits<double>::Max();
		for (int32 Commit = 0; Commit < Commits; ++Commit)
		{
			// Alternating texts, as an unchanged hash would make the reload a no-op.
			Tree.Write(File, FString(BenchXml).Replace(TEXT("duration=\"0.3\""), Commit % 2 ? TEXT("duration=\"0.30\"") : TEXT("duration=\"0.31\"")));
			FString Error;
			const EADSKReloadResult Queued = Subsystem->RequestReload(File, Error);
			ASSERT_THAT(IsTrue(Queued == EADSKReloadResult::Queued, FString::Printf(TEXT("reload %s: %s"), LexToString(Queued), *Error)));
			const double Begin = FPlatformTime::Seconds();
			Subsystem->CommitPendingReloads();
			const double Seconds = FPlatformTime::Seconds() - Begin;
			CommitSeconds += Seconds;
			BestCommit = FMath::Min(BestCommit, Seconds);
		}
		for (UADSKBrainComponent* Brain : World)
		{
			ASSERT_THAT(IsTrue(Brain->IsADSKRunning(), Brain->GetLastError()));
			ASSERT_THAT(IsTrue(&Brain->GetDefinition().Get() == Subsystem->GetActiveDefinition(File).Get(), TEXT("a brain does not run the last commit")));
			Brain->Cleanup();
		}

		const TSharedRef<FJsonObject> Numbers = MakeShared<FJsonObject>();
		Numbers->SetNumberField(TEXT("agents"), Agents);
		Numbers->SetNumberField(TEXT("start_us_per_instance"), StartSeconds * 1e6 / (static_cast<double>(Rounds) * Agents));
		Numbers->SetNumberField(TEXT("stop_us_per_instance"), StopSeconds * 1e6 / (static_cast<double>(Rounds) * Agents));
		Numbers->SetNumberField(TEXT("reload_brains"), Brains);
		Numbers->SetNumberField(TEXT("reload_commit_ms"), CommitSeconds * 1000.0 / Commits);
		Numbers->SetNumberField(TEXT("reload_commit_best_ms"), BestCommit * 1000.0);
		Numbers->SetNumberField(TEXT("reload_commit_us_per_brain"), CommitSeconds * 1e6 / (static_cast<double>(Commits) * Brains));
		Report(*TestRunner, TEXT("StartCost"), Numbers);
	}

	/** One brain whose perception bridge chooses among N sensed actors before every tick; 0 is the brain alone. */
	TEST_METHOD(PerceptionBridge)
	{
		constexpr int32 Ticks = 2000;
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		Registry->Freeze();
		const TSharedRef<const FADSKDefinition> Definition = ADSKTest::LoadOrStandIn(TEXT("bridge_bench.xml"), BridgeXml, Registry);
		FADSKPerceptionBinding Binding;
		Binding.Sense = UAISense_Sight::StaticClass();
		Binding.HasTargetInput = TEXT("Has");
		Binding.TargetLocationInput = TEXT("Location");
		Binding.TargetSeenEvent = TEXT("Seen");
		Binding.TargetLostEvent = TEXT("Lost");
		const TSharedRef<FJsonObject> Numbers = MakeShared<FJsonObject>();
		for (const int32 Count : {0, 4, 16, 64})
		{
			FActorTestSpawner Spawner;
			AActor& Host = SpawnPlaced(Spawner, FVector::ZeroVector);
			UAIPerceptionComponent* Perception = NewObject<UAIPerceptionComponent>(&Host);
			UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Host);
			Host.AddInstanceComponent(Perception);
			Host.AddInstanceComponent(Brain);
			Perception->RegisterComponent();
			Brain->RegisterComponent();
			Brain->InitializeDefinition(Definition);
			UADSKPerceptionBridgeComponent* Bridge = nullptr;
			if (Count > 0)
			{
				Bridge = NewObject<UADSKPerceptionBridgeComponent>(&Host);
				Bridge->Bindings = {Binding};
				Host.AddInstanceComponent(Bridge);
				Bridge->RegisterComponent();
				if (!Bridge->HasBegunPlay()) Bridge->BeginPlay();
			}
			Brain->StartLogic();
			ASSERT_THAT(IsTrue(Brain->IsADSKRunning(), TEXT("the bench brain did not start: ") + Brain->GetLastError()));
			for (int32 Index = 0; Index < Count; ++Index)
			{
				AActor& Seen = SpawnPlaced(Spawner, FVector(100.0 * (Index + 1), 50.0 * Index, 0.0));
				Bridge->InjectStimulusForTest(UAISense_Sight::StaticClass(), &Seen, Seen.GetActorLocation(), true);
			}
			const double Start = FPlatformTime::Seconds();
			for (int32 Tick = 0; Tick < Ticks; ++Tick)
			{
				Brain->TickBrain(FrameDelta);
			}
			const double Seconds = FPlatformTime::Seconds() - Start;
			if (Count > 0)
			{
				ASSERT_THAT(AreEqual(Count, Bridge->GetCandidateCountForTest(UAISense_Sight::StaticClass())));
				ASSERT_THAT(IsTrue(Brain->GetInstance()->GetActiveStatePath().Last() == TEXT("active"), TEXT("the bridge never published its target")));
			}
			Numbers->SetNumberField(FString::Printf(TEXT("candidates_%d_us_per_brain_tick"), Count), Seconds * 1e6 / Ticks);
			Brain->Cleanup();
		}
		Report(*TestRunner, TEXT("PerceptionBridge"), Numbers);
	}
};

#endif
