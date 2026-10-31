// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Nodes/ADSKBuiltinNodes.h"
#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

namespace FReactiveSelectorSpecPrivate
{
	FString Agent(const FString& Root)
	{
		return TEXT("<Agent version=\"1\" id=\"reactive\"><Inputs><Input name=\"Threat\" type=\"Bool\" default=\"false\"/></Inputs>"
			"<Locals><Local name=\"P\" type=\"Vector\"/></Locals><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>"
			"<Tree id=\"t\">") + Root + TEXT("</Tree></Agent>");
	}

	int32 CountLines(const FADSKTestRuntime& Runtime, const TCHAR* Line)
	{
		return Runtime.Log->Lines.FilterByPredicate([Line](const FString& Candidate) { return Candidate == Line; }).Num();
	}

	void SetThreat(FADSKTestRuntime& Runtime, bool bThreat)
	{
		ADSKTest::Expect(Runtime.Instance->SetInput(TEXT("Threat"), FADSKValue::MakeBool(bThreat)) == EADSKInputResult::Accepted, TEXT("Threat refused"));
	}
}

using namespace FReactiveSelectorSpecPrivate;

TEST_CLASS_WITH_FLAGS(FReactiveSelectorSpec, "AdamskAI.Execution.ReactiveSelector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AnEarlierGuardTurningTrueCancelsTheRunningOption)
	{
		FADSKTestRuntime Runtime(Agent(TEXT("<ReactiveSelector id=\"r\"><Guard id=\"g\" in-condition=\"Threat\"><MoveTo id=\"flee\" in-goal=\"P\"/></Guard>"
			"<MoveTo id=\"patrol\" in-goal=\"P\"/></ReactiveSelector>")));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FName(TEXT("patrol")), Runtime.Instance->GetActiveNodeId()));
		SetThreat(Runtime, true);
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FName(TEXT("flee")), Runtime.Instance->GetActiveNodeId()));
		ASSERT_THAT(AreEqual(1, CountLines(Runtime, TEXT("End:MoveTo:patrol:Cancelled"))));
		// The switch is the tick's work for this lane: the new option starts, it is not also updated.
		ASSERT_THAT(AreEqual(0, CountLines(Runtime, TEXT("Update:MoveTo:flee:0.100"))));
		// Once the threat is gone, the guard itself fails the option and the selector moves on.
		SetThreat(Runtime, false);
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FName(TEXT("patrol")), Runtime.Instance->GetActiveNodeId()));
		ASSERT_THAT(AreEqual(2, CountLines(Runtime, TEXT("Start:MoveTo:patrol"))));
	}

	TEST_METHOD(ACooldownBecomingReadyCancelsTheRunningOption)
	{
		// Attack once, patrol while the attack cools down, attack again the moment it is ready.
		FADSKTestRuntime Runtime(Agent(TEXT("<Repeat id=\"loop\"><ReactiveSelector id=\"r\"><Cooldown id=\"cd\" duration=\"1\"><Wait id=\"attack\" duration=\"0.2\"/></Cooldown>"
			"<MoveTo id=\"patrol\" in-goal=\"P\"/></ReactiveSelector></Repeat>")));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ASSERT_THAT(AreEqual(FName(TEXT("attack")), Runtime.Instance->GetActiveNodeId()));
		for (int32 Tick = 0; Tick < 3; ++Tick) { Runtime.Instance->Tick(0.1f); }
		ASSERT_THAT(AreEqual(FName(TEXT("patrol")), Runtime.Instance->GetActiveNodeId()));
		for (int32 Tick = 0; Tick < 6; ++Tick) { Runtime.Instance->Tick(0.1f); }
		// Still cooling down at 0.9 s.
		ASSERT_THAT(AreEqual(FName(TEXT("patrol")), Runtime.Instance->GetActiveNodeId()));
		for (int32 Tick = 0; Tick < 4; ++Tick) { Runtime.Instance->Tick(0.1f); }
		ASSERT_THAT(AreEqual(FName(TEXT("attack")), Runtime.Instance->GetActiveNodeId()));
		ASSERT_THAT(AreEqual(1, CountLines(Runtime, TEXT("End:MoveTo:patrol:Cancelled"))));
	}

	TEST_METHOD(AnOptionThatFailedWhileItsGuardHeldWaitsForTheNextRise)
	{
		FADSKTestRuntime Runtime(Agent(TEXT("<ReactiveSelector id=\"r\"><Guard id=\"g\" in-condition=\"Threat\"><AcquireResource id=\"grab\"/></Guard>"
			"<MoveTo id=\"patrol\" in-goal=\"P\"/></ReactiveSelector>")));
		SetThreat(Runtime, true);
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		for (int32 Tick = 0; Tick < 5; ++Tick)
		{
			Runtime.Instance->Tick(0.1f);
		}
		ASSERT_THAT(AreEqual(1, CountLines(Runtime, TEXT("Start:AcquireResource:grab"))));
		ASSERT_THAT(AreEqual(FName(TEXT("patrol")), Runtime.Instance->GetActiveNodeId()));
		SetThreat(Runtime, false);
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(1, CountLines(Runtime, TEXT("Start:AcquireResource:grab"))));
		SetThreat(Runtime, true);
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(2, CountLines(Runtime, TEXT("Start:AcquireResource:grab"))));
		// It failed again at once, so the selector fell through to patrol in the same tick.
		ASSERT_THAT(AreEqual(FName(TEXT("patrol")), Runtime.Instance->GetActiveNodeId()));
		ASSERT_THAT(AreEqual(2, CountLines(Runtime, TEXT("Start:MoveTo:patrol"))));
	}

	TEST_METHOD(OptionsWithoutACheckAreNotCheckedAgainAndAPlainSelectorNeverIs)
	{
		FADSKTestRuntime Reactive(Agent(TEXT("<ReactiveSelector id=\"r\"><AcquireResource id=\"grab\"/><MoveTo id=\"patrol\" in-goal=\"P\"/></ReactiveSelector>")));
		ADSK_ASSERT(ADSKTest::Start(*Reactive.Instance));
		for (int32 Tick = 0; Tick < 3; ++Tick)
		{
			Reactive.Instance->Tick(0.1f);
		}
		ASSERT_THAT(AreEqual(1, CountLines(Reactive, TEXT("Start:AcquireResource:grab"))));

		FADSKTestRuntime Plain(Agent(TEXT("<Selector id=\"r\"><Guard id=\"g\" in-condition=\"Threat\"><MoveTo id=\"flee\" in-goal=\"P\"/></Guard>"
			"<MoveTo id=\"patrol\" in-goal=\"P\"/></Selector>")));
		ADSK_ASSERT(ADSKTest::Start(*Plain.Instance));
		SetThreat(Plain, true);
		Plain.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FName(TEXT("patrol")), Plain.Instance->GetActiveNodeId()));
	}

	TEST_METHOD(RegistryKeepsReactivityToBoundedSelectors)
	{
		FString Children;
		for (int32 Index = 0; Index <= ADSK::MaxReactiveChildren; ++Index)
		{
			Children += FString::Printf(TEXT("<Wait id=\"w%d\"/>"), Index);
		}
		ASSERT_THAT(AreEqual(1, FCompilerSpecSupport::Load(Agent(TEXT("<ReactiveSelector id=\"r\">") + Children + TEXT("</ReactiveSelector>"))).Count(EADSKDiagnosticCode::BadArity)));

		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKNodeDescriptor Sequence;
		Sequence.Name = TEXT("ReactiveSequence");
		Sequence.Kind = EADSKNodeKind::Sequence;
		Sequence.MinChildren = 1;
		Sequence.MaxChildren = 4;
		Sequence.SelectorPolicy = EADSKSelectorPolicy::Reactive;
		ASSERT_THAT(AreEqual(EADSKRegisterResult::InvalidArity, Registry->Register(Sequence)));
		FADSKNodeDescriptor Unbounded = Sequence;
		Unbounded.Kind = EADSKNodeKind::Selector;
		Unbounded.MaxChildren = ADSK::UnboundedChildren;
		ASSERT_THAT(AreEqual(EADSKRegisterResult::InvalidArity, Registry->Register(Unbounded)));

		const TSharedRef<FADSKNodeRegistry> Builtins = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Builtins);
		Builtins->Freeze();
		ASSERT_THAT(IsTrue(Builtins->ExportSchemaJson().Contains(TEXT("{\"name\":\"ReactiveSelector\",\"version\":1,\"kind\":\"selector\",\"reactive\":true,\"minChildren\":1,\"maxChildren\":64")), TEXT("the builtin schema has no reactive selector entry")));
	}
};

#endif
