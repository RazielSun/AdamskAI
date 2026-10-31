// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Compiler/ADSKCompiler.h"
#include "Support/ExecutionTestSupport.h"
#include "CQTest.h"

// `<State tags="...">`. A state's tags hold while it or a descendant is active; the instance keeps
// their union and a serial that moves only when the set changes.
namespace FStateTagsSpecPrivate
{
	const TCHAR* TaggedXml = TEXT(
		"<Agent version=\"1\" id=\"tags\"><Events><Event name=\"Go\"/><Event name=\"Calm\"/><Event name=\"Side\"/></Events>"
		"<StateMachine id=\"m\" initial=\"combat\">"
		"<State id=\"combat\" initial=\"search\" tags=\"AI.State.Combat\"><On event=\"Calm\" goto=\"idle\"/>"
		"<State id=\"search\" tree=\"t\" tags=\"AI.Action.Search\"><On event=\"Go\" goto=\"attack\"/><On event=\"Side\" goto=\"search2\"/></State>"
		"<State id=\"search2\" tree=\"t\" tags=\"AI.Action.Search\"/>"
		"<State id=\"attack\" tree=\"t\" tags=\"AI.Action.Attack, AI.State.Combat\"/>"
		"</State>"
		"<State id=\"idle\" tree=\"t\"/>"
		"</StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>");

	FString Tags(const FADSKInstance& Instance)
	{
		return FString::JoinBy(Instance.GetActiveTags(), TEXT(","), [](FName Tag) { return Tag.ToString(); });
	}

	FADSKCompileResult Compile(const TCHAR* StateAttributes)
	{
		const FString Xml = FString::Printf(TEXT("<Agent version=\"1\" id=\"tags\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\" %s/></StateMachine>")
			TEXT("<Tree id=\"t\"><Wait id=\"w\" duration=\"1\"/></Tree></Agent>"), StateAttributes);
		return FADSKCompiler::Compile(TEXT("tags.xml"), Xml, ADSKTest::PluginCatalogue());
	}
}

using namespace FStateTagsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FStateTagsSpec, "AdamskAI.Execution.StateTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ParentAndChildTagsMergeAndFollowTransitions)
	{
		FADSKTestRuntime Runtime(TaggedXml);
		ASSERT_THAT(AreEqual(FString(), Tags(*Runtime.Instance), TEXT("tags before the start")));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ASSERT_THAT(AreEqual(FString(TEXT("AI.State.Combat,AI.Action.Search")), Tags(*Runtime.Instance)));
		const uint64 Started = Runtime.Instance->GetActiveTagsSerial();

		// Another state with the same tags: the set and its serial stay.
		Runtime.Instance->EnqueueEvent(TEXT("Side"));
		Runtime.Instance->Tick(0.1f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("combat"), TEXT("search2")}));
		ASSERT_THAT(AreEqual(Started, Runtime.Instance->GetActiveTagsSerial(), TEXT("a transition that kept the set moved the serial")));
	}

	TEST_METHOD(ATransitionChangesTheSetAndStopEmptiesIt)
	{
		FADSKTestRuntime Runtime(TaggedXml);
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		const uint64 Started = Runtime.Instance->GetActiveTagsSerial();
		Runtime.Instance->EnqueueEvent(TEXT("Go"));
		Runtime.Instance->Tick(0.1f);
		// The parent's tag and the child's repeat of it count once.
		ASSERT_THAT(AreEqual(FString(TEXT("AI.State.Combat,AI.Action.Attack")), Tags(*Runtime.Instance)));
		ASSERT_THAT(AreEqual(Started + 1, Runtime.Instance->GetActiveTagsSerial()));
		Runtime.Instance->EnqueueEvent(TEXT("Calm"));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(), Tags(*Runtime.Instance), TEXT("tags in the untagged idle state")));
		Runtime.Instance->Stop();
		ASSERT_THAT(AreEqual(FString(), Tags(*Runtime.Instance), TEXT("tags after the stop")));
	}

	TEST_METHOD(MalformedAndDuplicateTagsAreRejected)
	{
		ASSERT_THAT(AreEqual(1, Compile(TEXT("tags=\"AI..State\"")).Count(EADSKDiagnosticCode::OutOfRange)));
		ASSERT_THAT(AreEqual(1, Compile(TEXT("tags=\"AI.1State\"")).Count(EADSKDiagnosticCode::OutOfRange)));
		ASSERT_THAT(AreEqual(1, Compile(TEXT("tags=\"AI.State, AI.State\"")).Count(EADSKDiagnosticCode::DuplicateId)));
		const FADSKCompileResult Fine = Compile(TEXT("tags=\" AI.State.Combat ,AI.Action_2 \""));
		ASSERT_THAT(IsTrue(Fine.IsSuccess(), TEXT("spaces around commas were refused")));
		ASSERT_THAT(AreEqual(2, Fine.Definition->GetStateTags().Num()));
	}

	TEST_METHOD(TheMachineItselfTakesNoTags)
	{
		const FString Xml = TEXT("<Agent version=\"1\" id=\"tags\"><StateMachine id=\"m\" initial=\"s\" tags=\"AI.X\"><State id=\"s\" tree=\"t\"/></StateMachine>")
			TEXT("<Tree id=\"t\"><Wait id=\"w\" duration=\"1\"/></Tree></Agent>");
		ASSERT_THAT(AreEqual(1, FADSKCompiler::Compile(TEXT("tags.xml"), Xml, ADSKTest::PluginCatalogue()).Count(EADSKDiagnosticCode::UnknownAttribute)));
	}
};

#endif
