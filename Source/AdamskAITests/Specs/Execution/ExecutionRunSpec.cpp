// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "CQTest.h"

namespace FExecutionRunSpecPrivate
{
	FString Doc(const FString& Trees)
	{
		return TEXT("<Agent version=\"1\" id=\"run\">\n<Locals><Local name=\"L\" type=\"Vector\"/></Locals>\n")
			TEXT("<StateMachine id=\"m\" initial=\"s\">\n<State id=\"s\" tree=\"main\"/>\n</StateMachine>\n") + Trees + TEXT("\n</Agent>");
	}

	FString Wait(const FString& Id) { return FString::Printf(TEXT("<Wait id=\"%s\" duration=\"0\"/>"), *Id); }
	FString Run(const FString& Id, const FString& Tree) { return FString::Printf(TEXT("<Run id=\"%s\" tree=\"%s\"/>"), *Id, *Tree); }
	FString Tree(const FString& Id, const FString& Body) { return FString::Printf(TEXT("<Tree id=\"%s\">%s</Tree>"), *Id, *Body); }
	FString Seq(const FString& Id, const FString& Body) { return FString::Printf(TEXT("<Sequence id=\"%s\">%s</Sequence>"), *Id, *Body); }
}

using namespace FExecutionRunSpecPrivate;

TEST_CLASS_WITH_FLAGS(FCompilerRunExecutionSpec, "AdamskAI.Execution.Run",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(CallSitesShareTheSlotsAndTraceUsesPrefixedIds)
	{
		const FString Put = Tree(TEXT("put"), Seq(TEXT("ps"), TEXT("<Sense id=\"sense\" out-point=\"L\"/>") + Wait(TEXT("pw"))));
		const FString Use = Tree(TEXT("use"), TEXT("<MoveTo id=\"mv\" in-goal=\"L\" acceptanceRadius=\"1\"/>"));
		FADSKTestRuntime Runtime(Doc(Tree(TEXT("main"), Seq(TEXT("top"), Run(TEXT("a"), TEXT("put")) + Run(TEXT("b"), TEXT("put")) + Run(TEXT("c"), TEXT("use")))) + Put + Use));
		Runtime.Instance->Start();
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:Sense:a/sense")));
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:Sense:b/sense")));
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:MoveTo:c/mv")));
		ADSK_ASSERT(Runtime.HasLine(TEXT("Goal:c/mv:X=1.000 Y=2.000 Z=3.000")));
		ADSK_ASSERT(Runtime.LacksLine(TEXT("Start:Sense:sense")));
		FADSKValue Value;
		ASSERT_THAT(IsTrue(Runtime.Instance->GetValue(TEXT("L"), Value), TEXT("no slot L")));
		ASSERT_THAT(AreEqual(FVector(1.0, 2.0, 3.0), Value.Vector));
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}
};

#endif
