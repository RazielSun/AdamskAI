// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Nodes/ADSKBuiltinNodes.h"
#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

namespace FUtilitySelectorSpecPrivate
{
	FString Agent(const FString& Attributes, const FString& LastOption = TEXT("<MoveTo id=\"idle\" in-goal=\"P\"/>"))
	{
		return TEXT("<Agent version=\"1\" id=\"utility\"><Inputs><Input name=\"Fear\" type=\"Float\" default=\"0\"/>"
			"<Input name=\"Hunger\" type=\"Float\" default=\"0\"/><Input name=\"Zero\" type=\"Float\" default=\"0\"/></Inputs>"
			"<Locals><Local name=\"P\" type=\"Vector\"/></Locals><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">"
			"<On event=\"tree.failure\" goto=\"out\"/></State><State id=\"out\"/></StateMachine><Tree id=\"t\"><UtilitySelector id=\"u\" ")
			+ Attributes + TEXT(">"
			"<Score id=\"flee-score\" in-value=\"Fear\"><MoveTo id=\"flee\" in-goal=\"P\"/></Score>"
			"<Score id=\"eat-score\" in-value=\"Hunger\" weight=\"2\"><AcquireResource id=\"eat\"/></Score>"
			"<Score id=\"idle-score\" in-value=\"Zero\" bias=\"0.05\">") + LastOption + TEXT("</Score></UtilitySelector></Tree></Agent>");
	}

	void Set(FADSKTestRuntime& Runtime, const TCHAR* Input, float Value)
	{
		ADSKTest::Expect(Runtime.Instance->SetInput(Input, FADSKValue::MakeFloat(Value)) == EADSKInputResult::Accepted, FString::Printf(TEXT("input %s refused"), *FString(Input)));
	}

	FName Active(const FADSKTestRuntime& Runtime)
	{
		return Runtime.Instance->GetActiveNodeId();
	}
}

using namespace FUtilitySelectorSpecPrivate;

TEST_CLASS_WITH_FLAGS(FUtilitySelectorSpec, "AdamskAI.Execution.UtilitySelector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(TheBestScoringOptionRunsAndAFailedOneFallsBackDownTheScores)
	{
		FADSKTestRuntime Runtime(Agent(FString()));
		Set(Runtime, TEXT("Hunger"), 0.4f);
		Set(Runtime, TEXT("Fear"), 0.3f);
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		// eat (0.8) fails at once, so the next best, flee (0.3), runs; idle (0.05) waits behind it.
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:AcquireResource:eat")));
		ASSERT_THAT(AreEqual(FName(TEXT("flee")), Active(Runtime)));
	}

	TEST_METHOD(RescoringSwitchesOnlyForAClearlyBetterOption)
	{
		FADSKTestRuntime Runtime(Agent(TEXT("reevaluate=\"0.5\" margin=\"0.1\""), TEXT("<MoveTo id=\"idle\" in-goal=\"P\"/>")));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ASSERT_THAT(AreEqual(FName(TEXT("idle")), Active(Runtime)));
		Set(Runtime, TEXT("Fear"), 0.12f);
		Runtime.Instance->Tick(0.25f);
		ASSERT_THAT(AreEqual(FName(TEXT("idle")), Active(Runtime)));
		// Due at 0.5 s, but 0.12 does not beat 0.05 by more than the 0.1 margin.
		Runtime.Instance->Tick(0.25f);
		ASSERT_THAT(AreEqual(FName(TEXT("idle")), Active(Runtime)));
		Set(Runtime, TEXT("Fear"), 0.5f);
		Runtime.Instance->Tick(0.25f);
		ASSERT_THAT(AreEqual(FName(TEXT("idle")), Active(Runtime)));
		Runtime.Instance->Tick(0.25f);
		ASSERT_THAT(AreEqual(FName(TEXT("flee")), Active(Runtime)));
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:MoveTo:idle:Cancelled")));
	}

	TEST_METHOD(WithoutReevaluateTheChoiceStandsAndNoPositiveScoreFails)
	{
		FADSKTestRuntime Runtime(Agent(FString()));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Set(Runtime, TEXT("Fear"), 1.f);
		for (int32 Tick = 0; Tick < 5; ++Tick) Runtime.Instance->Tick(0.5f);
		ASSERT_THAT(AreEqual(FName(TEXT("idle")), Active(Runtime)));

		FADSKTestRuntime Empty(Agent(FString()).Replace(TEXT("bias=\"0.05\""), TEXT("bias=\"0\"")));
		ADSK_ASSERT(ADSKTest::Start(*Empty.Instance));
		Empty.Instance->Tick(0.1f);
		ADSK_ASSERT(Empty.PathIs({TEXT("m"), TEXT("out")}));
		ADSK_ASSERT(Empty.LacksLine(TEXT("Start:MoveTo:idle")));
	}

	TEST_METHOD(DescriptorsAndConfigAreChecked)
	{
		ASSERT_THAT(AreEqual(1, FCompilerSpecSupport::Load(Agent(TEXT("reevaluate=\"-1\""))).Count(EADSKDiagnosticCode::OutOfRange)));
		ASSERT_THAT(AreEqual(1, FCompilerSpecSupport::Load(Agent(TEXT("margin=\"-0.5\""))).Count(EADSKDiagnosticCode::OutOfRange)));
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKNodeDescriptor Bare;
		Bare.Name = TEXT("BareUtility");
		Bare.Kind = EADSKNodeKind::Selector;
		Bare.MinChildren = 1;
		Bare.MaxChildren = 8;
		Bare.SelectorPolicy = EADSKSelectorPolicy::Utility;
		ASSERT_THAT(AreEqual(EADSKRegisterResult::InvalidArity, Registry->Register(Bare)));
		Bare.ConfigStruct = FADSKUtilityConfig::StaticStruct();
		// The order it tries its options lives in its memory, which it must declare.
		ASSERT_THAT(AreEqual(EADSKRegisterResult::InvalidArity, Registry->Register(Bare)));
		Bare.MemoryStruct = FADSKUtilitySelectorMemory::StaticStruct();
		ASSERT_THAT(AreEqual(EADSKRegisterResult::Accepted, Registry->Register(Bare)));

		const TSharedRef<FADSKNodeRegistry> Builtins = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Builtins);
		Builtins->Freeze();
		const FString Schema = Builtins->ExportSchemaJson();
		ASSERT_THAT(IsTrue(Schema.Contains(TEXT("\"name\":\"UtilitySelector\",\"version\":1,\"kind\":\"selector\",\"utility\":true")), Schema));
		ASSERT_THAT(IsTrue(Schema.Contains(TEXT("\"name\":\"Score\",\"version\":1,\"kind\":\"scope\",\"scores\":true")), Schema));
	}
};

#endif
