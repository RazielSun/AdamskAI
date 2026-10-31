// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Nodes/ADSKBuiltinNodes.h"
#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKParallelTick.h"
#include "Support/CompilerSpecSupport.h"
#include "Trace/ADSKTrace.h"
#include "CQTest.h"
#include <atomic>

namespace FParallelTickSpecPrivate
{
	const TCHAR* PureXml = TEXT("<Agent version=\"1\" id=\"pure\"><Locals><Local name=\"N\" type=\"Int\"/></Locals>"
		"<Events><Event name=\"Rolled\" change=\"N\"/></Events><StateMachine id=\"m\" initial=\"a\">"
		"<State id=\"a\" tree=\"t\"><On event=\"Rolled\" goto=\"b\" if=\"N\" below=\"50\"/></State>"
		"<State id=\"b\" tree=\"t\"><On event=\"Rolled\" goto=\"a\" unless=\"N\" below=\"50\"/></State></StateMachine>"
		"<Tree id=\"t\"><Repeat id=\"loop\"><Sequence id=\"q\"><Roll id=\"r\" count=\"100\" out-value=\"N\"/>"
		"<Chance id=\"c\" probability=\"0.9\"/><Wait id=\"w\" duration=\"0.2\" deviation=\"0.15\"/></Sequence></Repeat></Tree></Agent>");

	struct FCounters
	{
		std::atomic<int32> SafeOnWorker{0};
		std::atomic<int32> SafeOnGameThread{0};
		std::atomic<int32> UnsafeOnWorker{0};
	};

	/** Builtins plus a thread-safe probe and an unflagged one, both counting the threads they run on. */
	TSharedRef<const FADSKNodeRegistry> MakeRegistry(const TSharedRef<FCounters>& Counters)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		FADSKNodeDescriptor Safe;
		Safe.Name = TEXT("SafeProbe");
		Safe.bThreadSafe = true;
		Safe.OnStart = [Counters](FADSKNodeContext&) { ++(IsInGameThread() ? Counters->SafeOnGameThread : Counters->SafeOnWorker); return EADSKNodeStatus::Succeeded; };
		ADSKTest::Register(*Registry, Safe);
		FADSKNodeDescriptor Unsafe;
		Unsafe.Name = TEXT("UnsafeProbe");
		Unsafe.OnStart = [Counters](FADSKNodeContext&) { Counters->UnsafeOnWorker += IsInGameThread() ? 0 : 1; return EADSKNodeStatus::Succeeded; };
		ADSKTest::Register(*Registry, Unsafe);
		Registry->Freeze();
		return Registry;
	}

	FString ProbeXml(const TCHAR* Probe)
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"probe\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>"
			"<Tree id=\"t\"><Repeat id=\"loop\"><Sequence id=\"q\"><%s id=\"p\"/><Wait id=\"w\" duration=\"0.05\"/></Sequence></Repeat></Tree></Agent>"), Probe);
	}

	struct FFleet
	{
		TArray<TSharedPtr<FADSKRecorder>> Recorders;
		TArray<TUniquePtr<FADSKInstance>> Instances;

		FFleet(const TSharedRef<const FADSKDefinition>& Definition, int32 Count)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Recorders.Add(FADSKRecorder::Create(256));
				Instances.Add(MakeUnique<FADSKInstance>(Definition, Recorders.Last()));
				Instances.Last()->SetRandomSeed(Index + 1);
				ADSKTest::Expect(ADSKTest::Start(*Instances.Last()));
			}
		}

		TArray<FADSKInstance*> Pointers() const
		{
			TArray<FADSKInstance*> Out;
			for (const TUniquePtr<FADSKInstance>& Instance : Instances) Out.Add(Instance.Get());
			return Out;
		}

		void TickParallel(float Delta)
		{
			const TArray<FADSKInstance*> All = Pointers();
			TArray<float> Deltas;
			Deltas.Init(Delta, All.Num());
			TArray<EADSKTickResult> Results;
			Results.SetNumZeroed(All.Num());
			FADSKParallelTick::Tick(All, Deltas, Results);
			ADSKTest::Expect(!Results.Contains(EADSKTickResult::NotRunning), TEXT("a fleet instance was not running"));
		}
	};

	/** What an instance did, independent of record sequence numbers. */
	FString Fingerprint(const FADSKInstance& Instance, const FADSKRecorder& Recorder)
	{
		FString Out = FString::Join(Instance.GetActiveStatePath(), TEXT("/"));
		FADSKValue N;
		Instance.GetValue(TEXT("N"), N);
		Out += FString::Printf(TEXT(" N=%d|"), N.Int);
		for (const FADSKRecord& Record : Recorder.Snapshot())
		{
			Out += FString::Printf(TEXT("%llu:%d:%d:%d,"), Record.Tick, static_cast<int32>(Record.Kind), Record.Node, Record.State);
		}
		return Out;
	}
}

using namespace FParallelTickSpecPrivate;

TEST_CLASS_WITH_FLAGS(FParallelTickSpec, "AdamskAI.Execution.ParallelTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(OnlyDefinitionsOfThreadSafeNodesAreThreadSafe)
	{
		const TSharedRef<FCounters> Counters = MakeShared<FCounters>();
		ASSERT_THAT(IsTrue(FCompilerSpecSupport::Load(PureXml).Definition->IsThreadSafe(), TEXT("the core-only definition is not thread-safe")));
		ASSERT_THAT(IsTrue(FADSKCompiler::Compile(TEXT("p.xml"), ProbeXml(TEXT("SafeProbe")), MakeRegistry(Counters)).Definition->IsThreadSafe(), TEXT("the SafeProbe definition is not thread-safe")));
		ASSERT_THAT(IsFalse(FADSKCompiler::Compile(TEXT("p.xml"), ProbeXml(TEXT("UnsafeProbe")), MakeRegistry(Counters)).Definition->IsThreadSafe(), TEXT("the UnsafeProbe definition is thread-safe")));
		// Test leaves are host doubles, never flagged.
		ASSERT_THAT(IsFalse(FCompilerSpecSupport::Load(TEXT("<Agent version=\"1\" id=\"a\"><Locals><Local name=\"P\" type=\"Vector\"/></Locals><StateMachine id=\"m\" initial=\"s\">"
			"<State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><MoveTo id=\"m\" in-goal=\"P\"/></Tree></Agent>")).Definition->IsThreadSafe(), TEXT("a definition with host test leaves is thread-safe")));
	}

	TEST_METHOD(AParallelPassEqualsTickingOneByOne)
	{
		const TSharedRef<const FADSKDefinition> Definition = FCompilerSpecSupport::Load(PureXml).Definition.ToSharedRef();
		FFleet Serial(Definition, 48);
		FFleet Parallel(Definition, 48);
		for (int32 Tick = 0; Tick < 60; ++Tick)
		{
			for (const TUniquePtr<FADSKInstance>& Instance : Serial.Instances) Instance->Tick(0.05f);
			Parallel.TickParallel(0.05f);
		}
		for (int32 Index = 0; Index < 48; ++Index)
		{
			ASSERT_THAT(AreEqual(Fingerprint(*Serial.Instances[Index], *Serial.Recorders[Index]), Fingerprint(*Parallel.Instances[Index], *Parallel.Recorders[Index])));
		}
		// Different seeds really made different agents, so the equality above is not trivial.
		ASSERT_THAT(IsFalse(Fingerprint(*Serial.Instances[0], *Serial.Recorders[0]) == Fingerprint(*Serial.Instances[1], *Serial.Recorders[1]), TEXT("the first two seeds made identical agents")));
	}

	TEST_METHOD(UnflaggedNodesStayOnTheGameThreadInAMixedPass)
	{
		const TSharedRef<FCounters> Counters = MakeShared<FCounters>();
		const TSharedRef<const FADSKNodeRegistry> Registry = MakeRegistry(Counters);
		FFleet Safe(FADSKCompiler::Compile(TEXT("p.xml"), ProbeXml(TEXT("SafeProbe")), Registry).Definition.ToSharedRef(), 32);
		FFleet Unsafe(FADSKCompiler::Compile(TEXT("p.xml"), ProbeXml(TEXT("UnsafeProbe")), Registry).Definition.ToSharedRef(), 8);
		TArray<FADSKInstance*> All = Safe.Pointers();
		All.Append(Unsafe.Pointers());
		const int32 StartsBefore = Counters->SafeOnWorker + Counters->SafeOnGameThread;
		for (int32 Tick = 0; Tick < 20; ++Tick)
		{
			TArray<float> Deltas;
			Deltas.Init(0.05f, All.Num());
			TArray<EADSKTickResult> Results;
			Results.SetNumZeroed(All.Num());
			FADSKParallelTick::Tick(All, Deltas, Results);
			ASSERT_THAT(IsFalse(Results.ContainsByPredicate([](EADSKTickResult Result) { return Result != EADSKTickResult::Advanced; }), FString::Printf(TEXT("an instance did not advance on pass %d"), Tick)));
		}
		ASSERT_THAT(AreEqual(0, Counters->UnsafeOnWorker.load()));
		ASSERT_THAT(IsTrue(Counters->SafeOnWorker + Counters->SafeOnGameThread > StartsBefore, FString::Printf(TEXT("%d safe probe starts, %d before"), Counters->SafeOnWorker + Counters->SafeOnGameThread, StartsBefore)));
		ASSERT_THAT(AreEqual(static_cast<uint64>(20), Unsafe.Instances[0]->GetTickCount()));
		ASSERT_THAT(AreEqual(static_cast<uint64>(20), Safe.Instances[31]->GetTickCount()));
	}

	TEST_METHOD(InstancesSharingARecorderNeverTickOnTwoWorkers)
	{
		const TSharedRef<FCounters> Counters = MakeShared<FCounters>();
		const TSharedRef<const FADSKDefinition> Definition = ADSKTest::LoadOrStandIn(TEXT("p.xml"), ProbeXml(TEXT("SafeProbe")), MakeRegistry(Counters));
		const TSharedPtr<FADSKRecorder> Shared = FADSKRecorder::Create(64);
		constexpr int32 Count = 64;
		constexpr int32 Ticks = 20;
		TArray<TUniquePtr<FADSKInstance>> Instances;
		TArray<FADSKInstance*> Pointers;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Instances.Add(MakeUnique<FADSKInstance>(Definition, Shared));
			ADSK_ASSERT(ADSKTest::Start(*Instances.Last()));
			Pointers.Add(Instances.Last().Get());
		}
		TArray<float> Deltas;
		Deltas.Init(0.05f, Count);
		TArray<EADSKTickResult> Results;
		Results.SetNumZeroed(Count);
		for (int32 Tick = 0; Tick < Ticks; ++Tick)
		{
			FADSKParallelTick::Tick(Pointers, Deltas, Results);
		}
		// Only the first instance of the group may leave the game thread, and it starts its probe at most once a tick.
		ASSERT_THAT(IsTrue(Counters->SafeOnWorker.load() <= Ticks, FString::Printf(TEXT("%d probe starts on workers"), Counters->SafeOnWorker.load())));
		ASSERT_THAT(AreEqual(static_cast<uint64>(Ticks), Instances.Last()->GetTickCount()));
	}
};

#endif
