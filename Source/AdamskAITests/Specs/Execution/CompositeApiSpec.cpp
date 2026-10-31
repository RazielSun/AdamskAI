// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Program/ADSKHoldAnalysis.h"
#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;

namespace FCompositeApiSpecPrivate
{
	FString TreeXml(const FString& Tree, bool bLoop = false)
	{
		const FString Handlers = bLoop
			? TEXT("<On event=\"tree.success\" goto=\"s\"/><On event=\"tree.failure\" goto=\"s\"/>")
			: TEXT("<On event=\"tree.success\" goto=\"won\"/><On event=\"tree.failure\" goto=\"lost\"/>");
		return TEXT("<Agent version=\"1\" id=\"c\"><Inputs><Input name=\"Goal\" type=\"Vector\" default=\"0,0,0\"/></Inputs><Locals><Local name=\"P\" type=\"Vector\"/></Locals>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">") + Handlers + TEXT("</State><State id=\"won\"/><State id=\"lost\"/></StateMachine>")
			TEXT("<Tree id=\"t\">") + Tree + TEXT("</Tree></Agent>");
	}

	FString Moves(std::initializer_list<const TCHAR*> Ids)
	{
		FString Out;
		for (const TCHAR* Id : Ids) { Out += FString::Printf(TEXT("<MoveTo id=\"%s\" in-goal=\"Goal\"/>"), Id); }
		return Out;
	}

	int32 Lines(const FADSKTestRuntime& Runtime, const FString& Line)
	{
		return Runtime.Log->Lines.FilterByPredicate([&Line](const FString& Candidate) { return Candidate == Line; }).Num();
	}

	FString Leaf(const FADSKTestRuntime& Runtime) { return Runtime.Instance->GetActiveStatePath().Last(); }

	void Finish(FADSKTestRuntime& Runtime, EADSKNodeStatus Status)
	{
		Runtime.Instance->PostActionResult(Runtime.Instance->GetActiveLeafHandle(), Status);
		Runtime.Instance->Tick(0.f);
	}

	/** A provider composite: runs its children last to first, like a Sequence; `Wrong` picks a child that does not exist. */
	TSharedRef<const FADSKNodeRegistry> ProviderRegistry(const TSharedRef<FADSKTestLeafLog>& Log)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		FADSKTestNodes::RegisterTestNodes(*Registry, Log);
		FADSKNodeDescriptor Reverse;
		Reverse.Name = TEXT("Reverse");
		Reverse.Kind = EADSKNodeKind::Composite;
		Reverse.MinChildren = 1;
		Reverse.MaxChildren = ADSK::UnboundedChildren;
		Reverse.OnCompositeStart = [](FADSKNodeContext& Context) { return FADSKCompositeStep::Start(Context.GetChildCount() - 1); };
		Reverse.OnChildFinished = [](FADSKNodeContext&, int32 Child, EADSKNodeStatus Status)
		{
			return Status == EADSKNodeStatus::Failed ? FADSKCompositeStep::Fail() : Child == 0 ? FADSKCompositeStep::Succeed() : FADSKCompositeStep::Start(Child - 1);
		};
		ADSKTest::Register(*Registry, FADSKNodeDescriptor(Reverse));
		FADSKNodeDescriptor Wrong = Reverse;
		Wrong.Name = TEXT("Wrong");
		Wrong.OnCompositeStart = [](FADSKNodeContext& Context) { return FADSKCompositeStep::Start(Context.GetChildCount()); };
		ADSKTest::Register(*Registry, MoveTemp(Wrong));
		Registry->Freeze();
		return Registry;
	}
}

using namespace FCompositeApiSpecPrivate;

TEST_CLASS_WITH_FLAGS(FCompositeApiSpec, "AdamskAI.Execution.Composites.Api",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AProviderCompositeChoosesItsChildren)
	{
		const TSharedRef<FADSKTestLeafLog> Log = MakeShared<FADSKTestLeafLog>();
		const FADSKCompileResult Loaded = FADSKCompiler::Compile(TEXT("c.xml"), TreeXml(TEXT("<Reverse id=\"r\">") + Moves({TEXT("a"), TEXT("b"), TEXT("c")}) + TEXT("</Reverse>")), ProviderRegistry(Log));
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		FADSKInstance Instance(Loaded.Definition.ToSharedRef());
		Instance.Start();
		for (const TCHAR* Expected : {TEXT("c"), TEXT("b"), TEXT("a")})
		{
			ASSERT_THAT(AreEqual(FString(Expected), Instance.GetActiveNodeId().ToString()));
			Instance.PostActionResult(Instance.GetActiveLeafHandle(), EADSKNodeStatus::Succeeded);
			Instance.Tick(0.f);
		}
		Instance.Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("won")), Instance.GetActiveStatePath().Last()));
	}

	TEST_METHOD(AChildOutOfRangeFailsTheComposite)
	{
		const TSharedRef<FADSKTestLeafLog> Log = MakeShared<FADSKTestLeafLog>();
		const FADSKCompileResult Loaded = FADSKCompiler::Compile(TEXT("c.xml"), TreeXml(TEXT("<Wrong id=\"w\">") + Moves({TEXT("a")}) + TEXT("</Wrong>")), ProviderRegistry(Log));
		FADSKInstance Instance(Loaded.Definition.ToSharedRef());
		Instance.Start();
		Instance.Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), Instance.GetActiveStatePath().Last()));
		ASSERT_THAT(AreEqual(0, Log->Starts));
	}

	TEST_METHOD(ACompositeNeedsBothCallbacksAndAChild)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKNodeDescriptor Half;
		Half.Name = TEXT("Half");
		Half.Kind = EADSKNodeKind::Composite;
		Half.MinChildren = 1;
		Half.MaxChildren = 1;
		Half.OnCompositeStart = [](FADSKNodeContext&) { return FADSKCompositeStep::Start(0); };
		ASSERT_THAT(AreEqual(EADSKRegisterResult::InvalidArity, Registry->Register(FADSKNodeDescriptor(Half))));
		Half.OnChildFinished = [](FADSKNodeContext&, int32, EADSKNodeStatus Status) { return FADSKCompositeStep::Finish(Status); };
		Half.MinChildren = 0;
		ASSERT_THAT(AreEqual(EADSKRegisterResult::InvalidArity, Registry->Register(FADSKNodeDescriptor(Half))));
		Half.MinChildren = 1;
		ASSERT_THAT(AreEqual(EADSKRegisterResult::Accepted, Registry->Register(MoveTemp(Half))));
	}
};

TEST_CLASS_WITH_FLAGS(FBuiltinDecoratorsSpec, "AdamskAI.Execution.Composites.Builtins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(RandomSelectorTriesEveryChildOnceInASeededOrder)
	{
		FADSKTestRuntime Runtime(TreeXml(TEXT("<RandomSelector id=\"r\">") + Moves({TEXT("a"), TEXT("b"), TEXT("c")}) + TEXT("</RandomSelector>")));
		Runtime.Instance->SetRandomSeed(5);
		Runtime.Instance->Start();
		TSet<FString> Tried;
		for (int32 Attempt = 0; Attempt < 3; ++Attempt)
		{
			Tried.Add(Runtime.Instance->GetActiveNodeId().ToString());
			Finish(Runtime, EADSKNodeStatus::Failed);
		}
		ASSERT_THAT(AreEqual(3, Tried.Num()));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), Leaf(Runtime)));

		TSet<FString> Firsts;
		for (int32 Seed = 0; Seed < 20; ++Seed)
		{
			FADSKTestRuntime Each(TreeXml(TEXT("<RandomSelector id=\"r\">") + Moves({TEXT("a"), TEXT("b"), TEXT("c")}) + TEXT("</RandomSelector>")));
			Each.Instance->SetRandomSeed(Seed);
			Each.Instance->Start();
			Firsts.Add(Each.Instance->GetActiveNodeId().ToString());
			Each.Instance->Stop();
		}
		ASSERT_THAT(AreEqual(3, Firsts.Num()));
	}

	TEST_METHOD(RandomSelectorSucceedsOnTheFirstSuccess)
	{
		FADSKTestRuntime Runtime(TreeXml(TEXT("<RandomSelector id=\"r\">") + Moves({TEXT("a"), TEXT("b")}) + TEXT("</RandomSelector>")));
		Runtime.Instance->Start();
		Finish(Runtime, EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("won")), Leaf(Runtime)));
		ASSERT_THAT(AreEqual(1, Runtime.Log->Starts));
	}

	TEST_METHOD(RepeatCountsSuccessesAndFailsWithItsChild)
	{
		FADSKTestRuntime Counted(TreeXml(TEXT("<Repeat id=\"r\" count=\"3\"><Sense id=\"s1\" out-point=\"P\"/></Repeat>")));
		Counted.Instance->Start();
		Counted.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(3, Lines(Counted, TEXT("Start:Sense:s1"))));
		ASSERT_THAT(AreEqual(FString(TEXT("won")), Leaf(Counted)));

		FADSKTestRuntime Failing(TreeXml(TEXT("<Repeat id=\"r\" count=\"3\"><AcquireResource id=\"a\"/></Repeat>")));
		Failing.Instance->Start();
		Failing.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(1, Failing.Log->Starts));
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), Leaf(Failing)));
	}

	TEST_METHOD(RetryCountsFailuresAndSucceedsWithItsChild)
	{
		FADSKTestRuntime Failing(TreeXml(TEXT("<Retry id=\"r\" count=\"3\"><AcquireResource id=\"a\"/></Retry>")));
		Failing.Instance->Start();
		Failing.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(3, Failing.Log->Starts));
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), Leaf(Failing)));

		FADSKTestRuntime Succeeding(TreeXml(TEXT("<Retry id=\"r\" count=\"3\"><Sense id=\"s1\" out-point=\"P\"/></Retry>")));
		Succeeding.Instance->Start();
		Succeeding.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(1, Lines(Succeeding, TEXT("Start:Sense:s1"))));
		ASSERT_THAT(AreEqual(FString(TEXT("won")), Leaf(Succeeding)));
	}

	TEST_METHOD(AnEndlessRetryOfAFailingChildSpreadsOverTicksByBudget)
	{
		FADSKTestRuntime Runtime(TreeXml(TEXT("<Retry id=\"r\"><AcquireResource id=\"a\"/></Retry>")), 2048);
		Runtime.Instance->Start();
		const int32 AfterStart = Runtime.Log->Starts;
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsTrue(AfterStart > 0 && AfterStart <= ADSK::ProcedureOperationBudget, FString::Printf(TEXT("%d leaf starts in Start"), AfterStart)));
		ASSERT_THAT(IsTrue(Runtime.Log->Starts > AfterStart, TEXT("the tick retried nothing")));
		ASSERT_THAT(AreEqual(FString(TEXT("s")), Leaf(Runtime)));
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(AnEndlessRepeatOfAnInstantChildSpreadsOverTicksByBudget)
	{
		FADSKTestRuntime Runtime(TreeXml(TEXT("<Repeat id=\"r\"><Sense id=\"s1\" out-point=\"P\"/></Repeat>")), 2048);
		Runtime.Instance->Start();
		const int32 AfterStart = Runtime.Log->Starts;
		Runtime.Instance->Tick(0.f);
		const int32 AfterTick = Runtime.Log->Starts;
		ASSERT_THAT(IsTrue(AfterStart > 0 && AfterStart <= ADSK::ProcedureOperationBudget, FString::Printf(TEXT("%d leaf starts in Start"), AfterStart)));
		ASSERT_THAT(IsTrue(AfterTick > AfterStart && AfterTick - AfterStart <= ADSK::ProcedureOperationBudget, FString::Printf(TEXT("%d leaf starts after the tick, %d after Start"), AfterTick, AfterStart)));
		ASSERT_THAT(IsTrue(Runtime.Count(EADSKRecordKind::BudgetDeferred) >= 1, TEXT("no budget-deferred record") + Runtime.Trace()));
		ASSERT_THAT(AreEqual(FString(TEXT("s")), Leaf(Runtime)));
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(CooldownBlocksUntilItsTimeHasPassedAndARestartClearsIt)
	{
		// Each completion re-enters `s`: the cooled-down move first, the idle move while it cools down.
		const FString Xml = TreeXml(TEXT("<Selector id=\"sel\"><Cooldown id=\"cd\" duration=\"1\"><MoveTo id=\"hit\" in-goal=\"Goal\"/></Cooldown><MoveTo id=\"idle\" in-goal=\"Goal\"/></Selector>"), true);
		FADSKTestRuntime Runtime(Xml);
		Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(FString(TEXT("hit")), Runtime.Instance->GetActiveNodeId().ToString()));
		Finish(Runtime, EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Runtime.Instance->GetActiveNodeId().ToString()));
		Runtime.Instance->Tick(0.6f);
		Finish(Runtime, EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Runtime.Instance->GetActiveNodeId().ToString()));
		Runtime.Instance->Tick(0.5f);
		Finish(Runtime, EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("hit")), Runtime.Instance->GetActiveNodeId().ToString()));

		// A cancelled run also starts the cooldown; a restart forgets it.
		Runtime.Instance->Stop();
		Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(FString(TEXT("hit")), Runtime.Instance->GetActiveNodeId().ToString()));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(TimeLimitCancelsAChildThatRunsTooLong)
	{
		FADSKTestRuntime Slow(TreeXml(TEXT("<TimeLimit id=\"tl\" duration=\"0.5\"><MoveTo id=\"m\" in-goal=\"Goal\"/></TimeLimit>")));
		Slow.Instance->Start();
		Slow.Instance->Tick(0.3f);
		ASSERT_THAT(AreEqual(0, Slow.Log->Ends));
		Slow.Instance->Tick(0.3f);
		ASSERT_THAT(AreEqual(1, Lines(Slow, TEXT("End:MoveTo:m:Cancelled"))));
		Slow.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("lost")), Leaf(Slow)));

		FADSKTestRuntime Quick(TreeXml(TEXT("<TimeLimit id=\"tl\" duration=\"0.5\"><MoveTo id=\"m\" in-goal=\"Goal\"/></TimeLimit>")));
		Quick.Instance->Start();
		Quick.Instance->Tick(0.3f);
		Finish(Quick, EADSKNodeStatus::Succeeded);
		Quick.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("won")), Leaf(Quick)));
	}

	/** `deviation` draws the limit once per start from duration +/- deviation; the same seed draws the same limit. */
	TEST_METHOD(TimeLimitDeviationDrawsTheLimitOncePerStart)
	{
		const auto TicksUntilCancelled = [](int32 Seed)
		{
			FADSKTestRuntime Runtime(TreeXml(TEXT("<TimeLimit id=\"tl\" duration=\"1\" deviation=\"0.4\"><MoveTo id=\"m\" in-goal=\"Goal\"/></TimeLimit>")));
			Runtime.Instance->SetRandomSeed(Seed);
			Runtime.Instance->Start();
			int32 Ticks = 0;
			while (Lines(Runtime, TEXT("End:MoveTo:m:Cancelled")) == 0 && Ticks < 100)
			{
				Runtime.Instance->Tick(0.05f);
				++Ticks;
			}
			Runtime.Instance->Stop();
			return Ticks;
		};
		const int32 First = TicksUntilCancelled(7);
		// 0.6 to 1.4 s at 0.05 s a tick
		ASSERT_THAT(IsTrue(First >= 12 && First <= 28, FString::Printf(TEXT("cancelled after %d ticks"), First)));
		ASSERT_THAT(AreEqual(First, TicksUntilCancelled(7)));
	}

	TEST_METHOD(ForceSuccessTurnsFailureIntoSuccessAndNeverFails)
	{
		FADSKTestRuntime Runtime(TreeXml(TEXT("<ForceSuccess id=\"f\"><AcquireResource id=\"a\"/></ForceSuccess>")));
		Runtime.Instance->Start();
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("won")), Leaf(Runtime)));
		const FADSKCompileResult Loaded = Load(TreeXml(TEXT("<ForceSuccess id=\"f\"><AcquireResource id=\"a\"/></ForceSuccess>")).Replace(TEXT("<On event=\"tree.failure\" goto=\"lost\"/>"), TEXT("")));
		ASSERT_THAT(IsTrue(ADSK::FindHoldRisks(*Loaded.Definition).IsEmpty(), FString::Printf(TEXT("%d hold risks"), ADSK::FindHoldRisks(*Loaded.Definition).Num())));
	}

	TEST_METHOD(DecoratorsRejectNegativeSettings)
	{
		for (const TCHAR* Tree : {TEXT("<Repeat id=\"r\" count=\"-1\"><Wait id=\"w\" duration=\"1\"/></Repeat>"),
				 TEXT("<Retry id=\"r\" count=\"-1\"><Wait id=\"w\" duration=\"1\"/></Retry>"),
				 TEXT("<Cooldown id=\"c\" duration=\"-1\"><Wait id=\"w\" duration=\"1\"/></Cooldown>"),
				 TEXT("<TimeLimit id=\"t\" duration=\"-1\"><Wait id=\"w\" duration=\"1\"/></TimeLimit>"),
				 TEXT("<TimeLimit id=\"t\" duration=\"1\" deviation=\"-1\"><Wait id=\"w\" duration=\"1\"/></TimeLimit>")})
		{
			const FADSKCompileResult Result = Load(TreeXml(Tree));
			ASSERT_THAT(IsTrue(Result.HasError() && Result.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D) { return D.Code == EADSKDiagnosticCode::OutOfRange; }), ADSKTest::FormatDiagnostics(Result)));
		}
	}
};

#endif
