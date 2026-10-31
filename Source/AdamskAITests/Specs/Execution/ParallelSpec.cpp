// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;

namespace FParallelSpecPrivate
{
	FString ParallelXml(const FString& Tree)
	{
		return TEXT("<Agent version=\"1\" id=\"p\"><Inputs><Input name=\"Goal\" type=\"Vector\" default=\"0,0,0\"/><Input name=\"Ok\" type=\"Bool\" default=\"true\"/></Inputs>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"><On event=\"tree.success\" goto=\"won\"/><On event=\"tree.failure\" goto=\"lost\"/></State>")
			TEXT("<State id=\"won\"/><State id=\"lost\"/></StateMachine><Tree id=\"t\">") + Tree + TEXT("</Tree></Agent>");
	}

	const TCHAR* MoveAndLook = TEXT("<SimpleParallel id=\"par\"><MoveTo id=\"main\" in-goal=\"Goal\"/><LookAtPoint id=\"bg\" in-target=\"Goal\"/></SimpleParallel>");

	TArray<FString> ActiveIds(const FADSKInstance& Instance)
	{
		TArray<FName> Ids;
		TArray<FADSKActivationHandle> Handles;
		Instance.GetActiveNodes(Ids, Handles);
		TArray<FString> Out;
		for (const FName Id : Ids) { Out.Add(Id.ToString()); }
		return Out;
	}

	FADSKActivationHandle LeafHandle(const FADSKInstance& Instance, const TCHAR* NodeId)
	{
		TArray<FName> Ids;
		TArray<FADSKActivationHandle> Handles;
		Instance.GetActiveNodes(Ids, Handles);
		const int32 Index = Ids.IndexOfByKey(FName(NodeId));
		return Index == INDEX_NONE ? FADSKActivationHandle() : Handles[Index];
	}

	int32 Lines(const FADSKTestRuntime& Runtime, const TCHAR* Line)
	{
		return Runtime.Log->Lines.FilterByPredicate([Line](const FString& Candidate) { return Candidate == Line; }).Num();
	}

	FString Leaf(const FADSKTestRuntime& Runtime)
	{
		return Runtime.Instance->GetActiveStatePath().Last();
	}
}

using namespace FParallelSpecPrivate;

TEST_CLASS_WITH_FLAGS(FParallelCompilerSpec, "AdamskAI.Execution.Parallel.Compiler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(TakesExactlyTwoChildren)
	{
		const FADSKCompileResult Two = Load(ParallelXml(MoveAndLook));
		ASSERT_THAT(IsTrue(Two.IsSuccess(), ADSKTest::FormatDiagnostics(Two)));
		const FADSKCompileResult One = Load(ParallelXml(TEXT("<SimpleParallel id=\"par\"><MoveTo id=\"main\" in-goal=\"Goal\"/></SimpleParallel>")));
		ASSERT_THAT(IsTrue(One.HasError() && One.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D) { return D.Code == EADSKDiagnosticCode::BadArity; }), ADSKTest::FormatDiagnostics(One)));
		const FADSKCompileResult Three = Load(ParallelXml(TEXT("<SimpleParallel id=\"par\"><MoveTo id=\"a\" in-goal=\"Goal\"/><MoveTo id=\"b\" in-goal=\"Goal\"/><MoveTo id=\"c\" in-goal=\"Goal\"/></SimpleParallel>")));
		ASSERT_THAT(IsTrue(Three.HasError() && Three.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D) { return D.Code == EADSKDiagnosticCode::BadArity; }), ADSKTest::FormatDiagnostics(Three)));
	}
};

TEST_CLASS_WITH_FLAGS(FParallelExecutionSpec, "AdamskAI.Execution.Parallel.Execution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(BothChildrenRunAndTheMainResultEndsTheParallel)
	{
		FADSKTestRuntime Runtime(ParallelXml(MoveAndLook));
		Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("main"), TEXT("bg")}), ActiveIds(*Runtime.Instance)));
		ASSERT_THAT(AreEqual(FString(TEXT("main")), Runtime.Instance->GetActiveNodeId().ToString()));
		Runtime.Instance->Tick(0.25f);
		ASSERT_THAT(AreEqual(1, Lines(Runtime, TEXT("Update:MoveTo:main:0.250"))));
		ASSERT_THAT(AreEqual(1, Lines(Runtime, TEXT("Update:LookAtPoint:bg:0.250"))));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->PostActionResult(LeafHandle(*Runtime.Instance, TEXT("main")), EADSKNodeStatus::Succeeded)));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(1, Lines(Runtime, TEXT("End:LookAtPoint:bg:Cancelled"))));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("won")), Leaf(Runtime)));
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(TheMainResultIsTheParallelsResult)
	{
		FADSKTestRuntime Runtime(ParallelXml(MoveAndLook));
		Runtime.Instance->Start();
		Runtime.Instance->PostActionResult(LeafHandle(*Runtime.Instance, TEXT("main")), EADSKNodeStatus::Failed);
		Runtime.Instance->Tick(0.f);
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), Leaf(Runtime)));
	}

	TEST_METHOD(AFinishedBackgroundRestartsOnTheNextTickAndNeverEndsTheParallel)
	{
		FADSKTestRuntime Runtime(ParallelXml(MoveAndLook));
		Runtime.Instance->Start();
		const FADSKActivationHandle First = LeafHandle(*Runtime.Instance, TEXT("bg"));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->PostActionResult(First, EADSKNodeStatus::Failed)));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(1, Lines(Runtime, TEXT("End:LookAtPoint:bg:Failed"))));
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("main")}), ActiveIds(*Runtime.Instance)));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(2, Lines(Runtime, TEXT("Start:LookAtPoint:bg"))));
		const FADSKActivationHandle Second = LeafHandle(*Runtime.Instance, TEXT("bg"));
		ASSERT_THAT(IsTrue(Second.IsValid(), TEXT("bg did not restart")));
		ASSERT_THAT(IsTrue(Second != First, TEXT("bg kept its old activation")));
		// The old activation's late result is stale; the restarted one keeps running.
		Runtime.Instance->PostActionResult(First, EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsTrue(LeafHandle(*Runtime.Instance, TEXT("bg")) == Second, TEXT("the stale result changed bg's activation")));
		ASSERT_THAT(AreEqual(FString(TEXT("s")), Leaf(Runtime)));
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(ABackgroundThatEndsAtOnceRunsOncePerTick)
	{
		FADSKTestRuntime Runtime(ParallelXml(TEXT("<SimpleParallel id=\"par\"><MoveTo id=\"main\" in-goal=\"Goal\"/><Sense id=\"bg\" out-point=\"Goal\"/></SimpleParallel>")
			).Replace(TEXT("<Input name=\"Goal\" type=\"Vector\" default=\"0,0,0\"/>"), TEXT("")).Replace(TEXT("<Inputs>"), TEXT("<Inputs>")).Replace(TEXT("</Inputs>"), TEXT("</Inputs><Locals><Local name=\"Goal\" type=\"Vector\"/></Locals>")));
		Runtime.Instance->Start();
		for (int32 Tick = 0; Tick < 5; ++Tick)
		{
			Runtime.Instance->Tick(0.1f);
		}
		ASSERT_THAT(AreEqual(6, Lines(Runtime, TEXT("Start:Sense:bg"))));
		ASSERT_THAT(AreEqual(1, Lines(Runtime, TEXT("Start:MoveTo:main"))));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(AMainChildThatEndsAtOnceNeverStartsTheBackground)
	{
		FADSKTestRuntime Runtime(ParallelXml(TEXT("<SimpleParallel id=\"par\"><Wait id=\"main\" duration=\"0\"/><LookAtPoint id=\"bg\" in-target=\"Goal\"/></SimpleParallel>")));
		Runtime.Instance->Start();
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("won")), Leaf(Runtime)));
		ASSERT_THAT(AreEqual(0, Lines(Runtime, TEXT("Start:LookAtPoint:bg"))));
	}

	TEST_METHOD(AGuardAboveCancelsBothLanes)
	{
		FADSKTestRuntime Runtime(ParallelXml(FString(TEXT("<Guard id=\"g\" in-condition=\"Ok\">")) + MoveAndLook + TEXT("</Guard>")));
		Runtime.Instance->Start();
		Runtime.Instance->SetInput(TEXT("Ok"), FADSKValue::MakeBool(false));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(1, Lines(Runtime, TEXT("End:MoveTo:main:Cancelled"))));
		ASSERT_THAT(AreEqual(1, Lines(Runtime, TEXT("End:LookAtPoint:bg:Cancelled"))));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), Leaf(Runtime)));
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(AGuardInTheBackgroundStopsOnlyTheBackground)
	{
		FADSKTestRuntime Runtime(ParallelXml(TEXT("<SimpleParallel id=\"par\"><MoveTo id=\"main\" in-goal=\"Goal\"/><Guard id=\"bgg\" in-condition=\"Ok\"><LookAtPoint id=\"bg\" in-target=\"Goal\"/></Guard></SimpleParallel>")));
		Runtime.Instance->Start();
		Runtime.Instance->SetInput(TEXT("Ok"), FADSKValue::MakeBool(false));
		for (int32 Tick = 0; Tick < 4; ++Tick)
		{
			Runtime.Instance->Tick(0.1f);
		}
		ASSERT_THAT(AreEqual(1, Lines(Runtime, TEXT("End:LookAtPoint:bg:Cancelled"))));
		ASSERT_THAT(AreEqual(0, Lines(Runtime, TEXT("End:MoveTo:main:Cancelled"))));
		Runtime.Instance->SetInput(TEXT("Ok"), FADSKValue::MakeBool(true));
		Runtime.Instance->Tick(0.1f);
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(2, Lines(Runtime, TEXT("Start:LookAtPoint:bg"))));
		ASSERT_THAT(AreEqual(FString(TEXT("s")), Leaf(Runtime)));
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(NestedParallelsRunThreeLanesAndStopCancelsEveryOne)
	{
		FADSKTestRuntime Runtime(ParallelXml(TEXT("<SimpleParallel id=\"par\"><MoveTo id=\"main\" in-goal=\"Goal\"/>")
			TEXT("<SimpleParallel id=\"inner\"><LookAtPoint id=\"bg1\" in-target=\"Goal\"/><LookAtPoint id=\"bg2\" in-target=\"Goal\"/></SimpleParallel></SimpleParallel>")));
		Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("main"), TEXT("bg1"), TEXT("bg2")}), ActiveIds(*Runtime.Instance)));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(3, Runtime.Log->Updates));
		Runtime.Instance->Stop();
		ASSERT_THAT(AreEqual(3, Runtime.Log->Ends));
		ADSK_ASSERT(Runtime.Balanced());
		ASSERT_THAT(IsTrue(ActiveIds(*Runtime.Instance).IsEmpty(), TEXT("still active: ") + FString::Join(ActiveIds(*Runtime.Instance), TEXT(", "))));
	}

	TEST_METHOD(ResultsForBothLanesInOneTickResolveInPostOrder)
	{
		FADSKTestRuntime Runtime(ParallelXml(MoveAndLook));
		Runtime.Instance->Start();
		Runtime.Instance->PostActionResult(LeafHandle(*Runtime.Instance, TEXT("main")), EADSKNodeStatus::Failed);
		Runtime.Instance->PostActionResult(LeafHandle(*Runtime.Instance, TEXT("bg")), EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.f);
		// The main result ends the parallel first, so the background was cancelled and its result is stale.
		ASSERT_THAT(AreEqual(1, Lines(Runtime, TEXT("End:LookAtPoint:bg:Cancelled"))));
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::ResultStale)));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), Leaf(Runtime)));
	}

	TEST_METHOD(ABackgroundStartOverBudgetIsDeferredToTheNextTick)
	{
		FString Steps;
		for (int32 Index = 0; Index < 70; ++Index)
		{
			Steps += FString::Printf(TEXT("<Wait id=\"w%d\" duration=\"0\"/>"), Index);
		}
		FADSKTestRuntime Runtime(ParallelXml(TEXT("<SimpleParallel id=\"par\"><Sequence id=\"steps\">") + Steps
			+ TEXT("<MoveTo id=\"main\" in-goal=\"Goal\"/></Sequence><LookAtPoint id=\"bg\" in-target=\"Goal\"/></SimpleParallel>")), 1024);
		Runtime.Instance->Start();
		ASSERT_THAT(IsTrue(Runtime.Count(EADSKRecordKind::BudgetDeferred) >= 1, TEXT("no budget-deferred record") + Runtime.Trace()));
		ASSERT_THAT(AreEqual(0, Lines(Runtime, TEXT("Start:LookAtPoint:bg"))));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("main"), TEXT("bg")}), ActiveIds(*Runtime.Instance)));
		ASSERT_THAT(AreEqual(1, Lines(Runtime, TEXT("Start:LookAtPoint:bg"))));
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(AFullResultQueueStillTakesOneResultPerRunningLeaf)
	{
		FADSKTestRuntime Runtime(ParallelXml(MoveAndLook), 1024);
		Runtime.Instance->Start();
		for (int32 Index = 0; Index < ADSK::ActionResultCapacity; ++Index)
		{
			Runtime.Instance->PostActionResult({99, 1u + Index}, EADSKNodeStatus::Succeeded);
		}
		const FADSKActivationHandle Background = LeafHandle(*Runtime.Instance, TEXT("bg"));
		const FADSKActivationHandle Main = LeafHandle(*Runtime.Instance, TEXT("main"));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->PostActionResult(Background, EADSKNodeStatus::Succeeded)));
		ASSERT_THAT(AreEqual(EADSKInputResult::QueueFull, Runtime.Instance->PostActionResult(Background, EADSKNodeStatus::Succeeded)));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->PostActionResult(Main, EADSKNodeStatus::Succeeded)));
		ASSERT_THAT(AreEqual(EADSKInputResult::QueueFull, Runtime.Instance->PostActionResult({98, 1}, EADSKNodeStatus::Succeeded)));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(2, Runtime.Count(EADSKRecordKind::ResultAccepted)));
	}

	TEST_METHOD(AStateExitCancelsTheBackground)
	{
		FADSKTestRuntime Runtime(ParallelXml(MoveAndLook).Replace(TEXT("<Inputs>"), TEXT("<Inputs>"))
			.Replace(TEXT("</Inputs>"), TEXT("</Inputs><Events><Event name=\"Go\"/></Events>"))
			.Replace(TEXT("<On event=\"tree.success\" goto=\"won\"/>"), TEXT("<On event=\"tree.success\" goto=\"won\"/><On event=\"Go\" goto=\"won\"/>")));
		Runtime.Instance->Start();
		Runtime.Instance->EnqueueEvent(TEXT("Go"));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("won")), Leaf(Runtime)));
		ASSERT_THAT(AreEqual(1, Lines(Runtime, TEXT("End:LookAtPoint:bg:Cancelled"))));
		ADSK_ASSERT(Runtime.Balanced());
	}
};

namespace FParallelPolicySpecPrivate
{
	FString ThreeMoves(const TCHAR* Attributes)
	{
		return FString::Printf(TEXT("<Parallel id=\"par\" %s><MoveTo id=\"a\" in-goal=\"Goal\"/><MoveTo id=\"b\" in-goal=\"Goal\"/><MoveTo id=\"c\" in-goal=\"Goal\"/></Parallel>"), Attributes);
	}

	void Post(FADSKTestRuntime& Runtime, const TCHAR* Leaf, EADSKNodeStatus Status)
	{
		ADSKTest::Expect(Runtime.Instance->PostActionResult(LeafHandle(*Runtime.Instance, Leaf), Status) == EADSKInputResult::Accepted, FString(TEXT("result refused for ")) + Leaf);
	}

	int32 IndexOf(const FADSKTestRuntime& Runtime, const TCHAR* Line)
	{
		return Runtime.Log->Lines.IndexOfByKey(FString(Line));
	}
}

using namespace FParallelPolicySpecPrivate;

TEST_CLASS_WITH_FLAGS(FParallelPolicySpec, "AdamskAI.Execution.Parallel.Policy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(EveryChildRunsInItsOwnLaneInDocumentOrder)
	{
		FADSKTestRuntime Runtime(ParallelXml(ThreeMoves(TEXT(""))));
		Runtime.Instance->Start();
		// The Parallel is its own lane's top; each child is the top of a lane of its own.
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("par"), TEXT("a"), TEXT("b"), TEXT("c")}), ActiveIds(*Runtime.Instance)));
		ASSERT_THAT(IsTrue(IndexOf(Runtime, TEXT("Start:MoveTo:a")) < IndexOf(Runtime, TEXT("Start:MoveTo:b")) && IndexOf(Runtime, TEXT("Start:MoveTo:b")) < IndexOf(Runtime, TEXT("Start:MoveTo:c"))));
		Runtime.Instance->Tick(0.25f);
		ASSERT_THAT(AreEqual(1, Lines(Runtime, TEXT("Update:MoveTo:c:0.250"))));
	}

	TEST_METHOD(AllEndsWithTheLastSuccess)
	{
		FADSKTestRuntime Runtime(ParallelXml(ThreeMoves(TEXT("success=\"all\""))));
		Runtime.Instance->Start();
		Post(Runtime, TEXT("b"), EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.1f);
		Post(Runtime, TEXT("a"), EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("par"), TEXT("c")}), ActiveIds(*Runtime.Instance)));
		Post(Runtime, TEXT("c"), EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.1f);
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("won")), Leaf(Runtime)));
	}

	TEST_METHOD(AnyCancelsTheOthersInDocumentOrder)
	{
		FADSKTestRuntime Runtime(ParallelXml(ThreeMoves(TEXT("success=\"any\""))));
		Runtime.Instance->Start();
		Post(Runtime, TEXT("b"), EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.1f);
		const int32 CancelA = IndexOf(Runtime, TEXT("End:MoveTo:a:Cancelled"));
		const int32 CancelC = IndexOf(Runtime, TEXT("End:MoveTo:c:Cancelled"));
		ASSERT_THAT(IsTrue(CancelA != INDEX_NONE && CancelC != INDEX_NONE && CancelA < CancelC, FString::Join(Runtime.Log->Lines, TEXT("\n"))));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("won")), Leaf(Runtime)));
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(ResultsOfSeveralLanesInOneTickCountInPostOrder)
	{
		FADSKTestRuntime Runtime(ParallelXml(ThreeMoves(TEXT("failure=\"2\""))));
		Runtime.Instance->Start();
		Post(Runtime, TEXT("a"), EADSKNodeStatus::Failed);
		Post(Runtime, TEXT("c"), EADSKNodeStatus::Failed);
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(1, Lines(Runtime, TEXT("End:MoveTo:b:Cancelled"))));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), Leaf(Runtime)));
	}

	TEST_METHOD(ASuccessOutOfReachFailsEvenWhenFailuresMayGoOn)
	{
		FADSKTestRuntime Runtime(ParallelXml(ThreeMoves(TEXT("success=\"all\" failure=\"all\""))));
		Runtime.Instance->Start();
		Post(Runtime, TEXT("a"), EADSKNodeStatus::Failed);
		Runtime.Instance->Tick(0.1f);
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), Leaf(Runtime)));
	}

	TEST_METHOD(AStopCancelsEveryLane)
	{
		FADSKTestRuntime Runtime(ParallelXml(ThreeMoves(TEXT(""))));
		Runtime.Instance->Start();
		Runtime.Instance->Stop();
		ASSERT_THAT(AreEqual(3, Lines(Runtime, TEXT("End:MoveTo:a:Cancelled")) + Lines(Runtime, TEXT("End:MoveTo:b:Cancelled")) + Lines(Runtime, TEXT("End:MoveTo:c:Cancelled"))));
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(TheCompilerChecksChildrenAndPolicies)
	{
		const auto Code = [](const FString& Tree, EADSKDiagnosticCode Expected) { return Load(ParallelXml(Tree)).Count(Expected) >= 1; };
		ASSERT_THAT(IsTrue(Code(TEXT("<Parallel id=\"par\"><MoveTo id=\"a\" in-goal=\"Goal\"/></Parallel>"), EADSKDiagnosticCode::BadArity)));
		FString Nine;
		for (int32 Index = 0; Index < 9; ++Index) { Nine += FString::Printf(TEXT("<MoveTo id=\"m%d\" in-goal=\"Goal\"/>"), Index); }
		ASSERT_THAT(IsTrue(Code(TEXT("<Parallel id=\"par\">") + Nine + TEXT("</Parallel>"), EADSKDiagnosticCode::BadArity)));
		ASSERT_THAT(IsTrue(Load(ParallelXml(ThreeMoves(TEXT("success=\"some\"")))).HasError()));
		ASSERT_THAT(IsTrue(Load(ParallelXml(ThreeMoves(TEXT("failure=\"0\"")))).HasError()));
		const FADSKCompileResult Counted = Load(ParallelXml(ThreeMoves(TEXT("success=\"2\" failure=\"all\""))));
		ASSERT_THAT(IsTrue(Counted.IsSuccess(), ADSKTest::FormatDiagnostics(Counted)));
	}
};

#endif
