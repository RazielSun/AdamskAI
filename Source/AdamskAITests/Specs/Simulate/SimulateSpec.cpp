// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "Simulate/ADSKSimulator.h"
#include "CQTest.h"
#include "HAL/FileManager.h"
#include "Support/TestChecks.h"
#include "Support/TempTree.h"

namespace FSimulateSpecPrivate
{
	/** Two states that hand over to each other whenever their move ends: a loop that never waits once the move fails at once. */
	const TCHAR* PingPongXml = TEXT(
		"<Agent version=\"1\" id=\"ping_pong\">"
		"<Inputs><Input name=\"Goal\" type=\"Vector\" default=\"0,0,0\"/></Inputs>"
		"<StateMachine id=\"m\" initial=\"a\">"
		"<State id=\"a\" tree=\"go\"><On event=\"tree.success\" goto=\"b\"/><On event=\"tree.failure\" goto=\"b\"/></State>"
		"<State id=\"b\" tree=\"go\"><On event=\"tree.success\" goto=\"a\"/><On event=\"tree.failure\" goto=\"a\"/></State>"
		"<State id=\"never\"/>"
		"</StateMachine>"
		"<Tree id=\"go\"><MoveTo id=\"go_move\" in-goal=\"Goal\"/></Tree></Agent>");

	/** A tree whose success no handler takes. */
	const TCHAR* HoldXml = TEXT(
		"<Agent version=\"1\" id=\"holder\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>"
		"<Tree id=\"t\"><Wait id=\"w\" duration=\"0.5\"/></Tree></Agent>");

	struct FRun
	{
		FADSKTempTree Tree;

		FADSKSimulationReport Run(const FString& Xml, const FString& Scenario)
		{
			Tree.Write(TEXT("b.xml"), Xml);
			return FADSKSimulator::Run(Tree.Write(TEXT("s.json"), Scenario), TEXT("s.json"), ADSKTest::PluginCatalogue());
		}
	};

	TArray<FString> Codes(const FADSKSimulationReport& Report)
	{
		TArray<FString> Out;
		for (const FADSKReportRow& Row : Report.Rows) Out.Add(Row.Severity + TEXT(" ") + Row.Code);
		return Out;
	}

	FString Describe(const FADSKSimulationReport& Report)
	{
		TArray<FString> Rows;
		for (const FADSKReportRow& Row : Report.Rows) Rows.Add(FADSKValidateReport::FormatRow(Row));
		return FString::Printf(TEXT("error [%s]\nrows:\n  %s\nlines:\n  %s"), *Report.Error, *FString::Join(Rows, TEXT("\n  ")), *FString::Join(Report.Lines, TEXT("\n  ")));
	}
}

using namespace FSimulateSpecPrivate;

TEST_CLASS_WITH_FLAGS(FSimulateSpec, "AdamskAI.Simulate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ALoopThatNeverWaitsIsALivelock)
	{
		FRun Run;
		const FADSKSimulationReport Report = Run.Run(PingPongXml, TEXT("{\"behavior\":\"b.xml\",\"duration\":3,\"leaves\":{\"go_move\":{\"result\":\"Failed\",\"after\":0}}}"));
		ASSERT_THAT(IsTrue(Report.Error.IsEmpty() && Codes(Report) == TArray<FString>{TEXT("error simulate-livelock")}, Describe(Report)));
		ASSERT_THAT(IsTrue(Report.Rows[0].Message.Contains(TEXT("-> a -> b")) || Report.Rows[0].Message.Contains(TEXT("-> b -> a")), Describe(Report)));
		ASSERT_THAT(IsTrue(Report.Trace.Contains(TEXT("transition")), TEXT("the trace has no transition records")));
	}

	TEST_METHOD(AMoveThatTakesTimeIsNoLivelockAndMeetsTheExpectations)
	{
		FRun Run;
		const FADSKSimulationReport Report = Run.Run(PingPongXml,
			TEXT("{\"behavior\":\"b.xml\",\"duration\":3,\"leaves\":{\"MoveTo\":{\"after\":0.5}},\"expect\":{\"reach\":[\"a\",\"b\"],\"never\":[\"never\"]}}"));
		ASSERT_THAT(IsTrue(Report.Error.IsEmpty() && Report.Rows.IsEmpty(), Describe(Report)));
		ASSERT_THAT(IsTrue(Report.Lines.Last().Contains(TEXT("at most 2 in one second")) || Report.Lines.Last().Contains(TEXT("at most 3 in one second")), Describe(Report)));
	}

	TEST_METHOD(UnmetExpectationsHoldsAndDefaultedLeavesAreReported)
	{
		FRun Run;
		FADSKSimulationReport Report = Run.Run(PingPongXml, TEXT("{\"behavior\":\"b.xml\",\"duration\":2,\"expect\":{\"reach\":[\"never\"],\"never\":[\"b\"]}}"));
		ASSERT_THAT(IsTrue(Codes(Report) == TArray<FString>{TEXT("error simulate-expect"), TEXT("error simulate-expect"), TEXT("note simulate-default")}, Describe(Report)));
		Report = Run.Run(HoldXml, TEXT("{\"behavior\":\"b.xml\",\"duration\":2}"));
		ASSERT_THAT(IsTrue(Codes(Report) == TArray<FString>{TEXT("error simulate-hold")}, Describe(Report)));
		Report = Run.Run(HoldXml, TEXT("{\"behavior\":\"b.xml\",\"duration\":2,\"expect\":{\"hold\":true}}"));
		ASSERT_THAT(IsTrue(Report.Rows.IsEmpty(), Describe(Report)));
	}

	TEST_METHOD(InputsEventsAndOutPortsFollowTheTimeline)
	{
		// The plugin's own scenarios: an example driven by inputs and an event with an Object parameter.
		const FString Root = FADSKTestPaths::PluginBase() / TEXT("Fixtures/scenarios");
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Root / TEXT("*.json")), true, false);
		ASSERT_THAT(IsTrue(Files.Num() >= 2, TEXT("no scenarios in ") + Root));
		for (const FString& File : Files)
		{
			const FADSKSimulationReport Report = FADSKSimulator::Run(Root / File, File, ADSKTest::PluginCatalogue());
			ASSERT_THAT(IsTrue(Report.Error.IsEmpty() && Report.Rows.IsEmpty(), File + TEXT(": ") + Describe(Report)));
		}
	}

	TEST_METHOD(AMalformedScenarioIsASetupError)
	{
		FRun Run;
		for (const TCHAR* Bad : {
				 TEXT("[]"),
				 TEXT("{\"duration\":1}"),
				 TEXT("{\"behavior\":\"b.xml\",\"speed\":2}"),
				 TEXT("{\"behavior\":\"b.xml\",\"leaves\":{\"nope\":{}}}"),
				 TEXT("{\"behavior\":\"b.xml\",\"leaves\":{\"go_move\":{\"result\":\"Maybe\"}}}"),
				 TEXT("{\"behavior\":\"b.xml\",\"leaves\":{\"go_move\":{\"out\":{\"goal\":\"1,2,3\"}}}}"),
				 TEXT("{\"behavior\":\"b.xml\",\"inputs\":{\"Goal\":true}}"),
				 TEXT("{\"behavior\":\"b.xml\",\"inputs\":{\"Nope\":1}}"),
				 TEXT("{\"behavior\":\"b.xml\",\"timeline\":[{\"at\":1,\"event\":\"tree.success\"}]}"),
				 TEXT("{\"behavior\":\"b.xml\",\"expect\":{\"reach\":[\"z\"]}}"),
				 TEXT("{\"behavior\":\"missing.xml\"}")})
		{
			const FADSKSimulationReport Report = Run.Run(PingPongXml, Bad);
			ASSERT_THAT(IsFalse(Report.Error.IsEmpty(), FString(Bad) + TEXT(" ran: ") + Describe(Report)));
		}
	}

	TEST_METHOD(ABehaviorThatDoesNotCompileReportsItsDiagnostics)
	{
		FRun Run;
		const FADSKSimulationReport Report = Run.Run(TEXT("<Agent version=\"1\" id=\"x\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><Nope id=\"n\"/></Tree></Agent>"),
			TEXT("{\"behavior\":\"b.xml\"}"));
		ASSERT_THAT(IsTrue(Report.Error.IsEmpty() && Codes(Report) == TArray<FString>{TEXT("error unknown-node")}, Describe(Report)));
	}
};

#endif
