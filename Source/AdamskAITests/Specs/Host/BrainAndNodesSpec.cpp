// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Compiler/ADSKCompiler.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Trace/ADSKTrace.h"
#include "CQTest.h"
#include "Support/TestBrain.h"
#include "Support/TestChecks.h"

namespace FBrainAndNodesSpecPrivate
{
	FADSKCompileResult Load(const FString& Inputs, const FString& Leaf)
	{
		const TSharedRef<const FADSKNodeRegistry> Registry = ADSKTest::PluginCatalogue();
		const FString Xml = FString::Printf(TEXT("<Agent version=\"1\" id=\"a\"><Inputs>%s</Inputs><Locals/><Events/><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\">%s</Tree></Agent>"), *Inputs, *Leaf);
		return FADSKCompiler::Compile(TEXT("brain.xml"), Xml, Registry);
	}

	FADSKCompileResult LoadWithOutcome(const FString& Tree)
	{
		const TSharedRef<const FADSKNodeRegistry> Registry = ADSKTest::PluginCatalogue();
		const FString Xml = FString::Printf(TEXT("<Agent version=\"1\" id=\"a\"><Inputs><Input name=\"Goal\" type=\"Vector\" default=\"0,0,0\"/></Inputs><Locals><Local name=\"Plan\" type=\"Int\"/></Locals>")
			TEXT("<Events><Event name=\"Go\"/></Events><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"><On event=\"tree.success\" goto=\"won\"/><On event=\"tree.failure\" goto=\"lost\"/><On event=\"Go\" goto=\"won\"/></State>")
			TEXT("<State id=\"won\"/><State id=\"lost\"/></StateMachine><Tree id=\"t\">%s</Tree></Agent>"), *Tree);
		return FADSKCompiler::Compile(TEXT("brain.xml"), Xml, Registry);
	}

	FString LeafState(const UADSKBrainComponent& Brain)
	{
		return Brain.GetInstance()->GetActiveStatePath().Last();
	}
}

using namespace FBrainAndNodesSpecPrivate;

TEST_CLASS_WITH_FLAGS(FBrainLifecycleSpec, "AdamskAI.Host.Brain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(StartsExplicitlyAndObservesSynchronousCompletion)
	{
		const FADSKCompileResult Loaded = Load(TEXT(""), TEXT("<Wait id=\"w\" duration=\"0\"/>"));
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		FADSKTestBrain Brain;
		const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(32);
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef(), Recorder);
		ASSERT_THAT(IsFalse(Brain->IsADSKRunning(), TEXT("the brain is still running")));
		Brain->StartLogic();
		ASSERT_THAT(AreEqual(Brain->GetInstance()->IsRunning(), Brain->IsADSKRunning()));
		Brain->StopLogic(); Brain->StopLogic();
	}

	TEST_METHOD(CachesAndValidatesRequiredInputsAcrossReplacement)
	{
		const FString Inputs = TEXT("<Input name=\"Goal\" type=\"Vector\" required=\"true\"/>");
		const FADSKCompileResult Loaded = Load(Inputs, TEXT("<Wait id=\"w\" duration=\"1\"/>"));
		FADSKTestBrain Brain;
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef(), FADSKRecorder::Create(32));
		FString Error;
		ASSERT_THAT(IsFalse(Brain->ValidateBindings(*Loaded.Definition, Error), TEXT("bindings validated without the required Goal")));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Brain->SetInput(TEXT("Goal"), FADSKValue::MakeVector(FVector(1, 2, 3)))));
		ASSERT_THAT(IsTrue(Brain->ValidateBindings(*Loaded.Definition, Error), Error));
		ASSERT_THAT(IsTrue(Brain->ReplaceDefinition(Loaded.Definition.ToSharedRef(), false, Error), Error));
		ASSERT_THAT(AreEqual(1, Brain->GetCachedInputs().Num()));
	}

	TEST_METHOD(InputsDifferingOnlyInCaseKeepTheirOwnValuesAcrossReplacement)
	{
		const FADSKCompileResult Loaded = Load(TEXT("<Input name=\"Goal\" type=\"Vector\" default=\"0,0,0\"/><Input name=\"goal\" type=\"Vector\" default=\"0,0,0\"/>"), TEXT("<Wait id=\"w\" duration=\"1\"/>"));
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		FADSKTestBrain Brain;
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef(), FADSKRecorder::Create(32));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Brain->SetInput(TEXT("Goal"), FADSKValue::MakeVector(FVector(1, 0, 0)))));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Brain->SetInput(TEXT("goal"), FADSKValue::MakeVector(FVector(2, 0, 0)))));
		ASSERT_THAT(AreEqual(2, Brain->GetCachedInputs().Num()));
		FString Error;
		// Started, so the replayed writes are applied rather than pending.
		ASSERT_THAT(IsTrue(Brain->ReplaceDefinition(Loaded.Definition.ToSharedRef(), true, Error), Error));
		FADSKValue Upper, Lower;
		ASSERT_THAT(IsTrue(Brain->GetInstance()->GetValue(TEXT("Goal"), Upper), TEXT("no slot Goal")));
		ASSERT_THAT(IsTrue(Brain->GetInstance()->GetValue(TEXT("goal"), Lower), TEXT("no slot goal")));
		ASSERT_THAT(IsTrue(Upper.Vector == FVector(1, 0, 0) && Lower.Vector == FVector(2, 0, 0),
			FString::Printf(TEXT("Goal %s, goal %s"), *Upper.Vector.ToString(), *Lower.Vector.ToString())));
	}

	TEST_METHOD(MoveDispatcherRequiresExactRequestAndHandle)
	{
		const FADSKCompileResult Loaded = Load(TEXT(""), TEXT("<Wait id=\"w\" duration=\"1\"/>"));
		FADSKTestBrain Brain;
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef(), FADSKRecorder::Create(32));
		Brain->StartLogic();
		const FADSKActivationHandle Handle = Brain->GetInstance()->GetActiveLeafHandle();
		const FAIRequestID First(41), Second(42);
		ASSERT_THAT(IsFalse(Brain->BindMove(Handle, First), TEXT("a move was bound without an op")));
		ASSERT_THAT(IsTrue(Brain->GetAsyncOps().Begin(Handle, TEXT("Move"), TEXT("w"), 0.0), TEXT("the move op did not begin")));
		ASSERT_THAT(IsTrue(Brain->BindMove(Handle, First), TEXT("the move was not bound")));
		ASSERT_THAT(IsFalse(Brain->BindMove(Handle, Second), TEXT("a second move was bound to the leaf")));
		ASSERT_THAT(IsTrue(Brain->GetActiveMoveRequest() == First, FString::Printf(TEXT("the active move request is %u"), Brain->GetActiveMoveRequest().GetID())));
		Brain->CompleteMoveForTest(Second, EPathFollowingResult::Success);
		ASSERT_THAT(IsTrue(Brain->GetActiveMoveRequest() == First, FString::Printf(TEXT("the active move request is %u"), Brain->GetActiveMoveRequest().GetID())));
		ASSERT_THAT(IsTrue(Brain->GetAsyncOps().IsPending(Handle), TEXT("the move op is no longer pending")));
		// The tag clears on completion, not on the next tick; the entry stays until its node ends.
		Brain->CompleteMoveForTest(First, EPathFollowingResult::Success);
		ASSERT_THAT(IsFalse(Brain->GetActiveMoveRequest().IsValid(), TEXT("the completed move is still active")));
		ASSERT_THAT(IsFalse(Brain->GetAsyncOps().IsPending(Handle), TEXT("the completed move op is still pending")));
		ASSERT_THAT(AreEqual(1, Brain->GetAsyncOps().GetOps().Num()));
		Brain->CompleteMoveForTest(First, EPathFollowingResult::Success);
		ASSERT_THAT(AreEqual<uint32>(0, Brain->GetStaleAsyncCompletions()));
		Brain->GetAsyncOps().CancelAll();
	}

	TEST_METHOD(AnUnboundMoveIgnoresItsOldRequestAndTakesANewOne)
	{
		const FADSKCompileResult Loaded = Load(TEXT(""), TEXT("<Wait id=\"w\" duration=\"1\"/>"));
		FADSKTestBrain Brain;
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef(), FADSKRecorder::Create(32));
		Brain->StartLogic();
		const FADSKActivationHandle Handle = Brain->GetInstance()->GetActiveLeafHandle();
		const FAIRequestID Old(51), New(52);
		ASSERT_THAT(IsTrue(Brain->GetAsyncOps().Begin(Handle, TEXT("Move"), TEXT("w"), 0.0), TEXT("the move op did not begin")));
		ASSERT_THAT(IsTrue(Brain->BindMove(Handle, Old), TEXT("the move was not bound")));
		// MoveTo re-paths this way: the new request aborts the old one, and that abort must not finish the node.
		Brain->UnbindMove(Handle);
		Brain->CompleteMoveForTest(Old, EPathFollowingResult::Aborted);
		ASSERT_THAT(IsTrue(Brain->GetAsyncOps().IsPending(Handle), TEXT("the move op is no longer pending")));
		ASSERT_THAT(IsTrue(Brain->BindMove(Handle, New), TEXT("the new move was not bound")));
		Brain->CompleteMoveForTest(New, EPathFollowingResult::Success);
		ASSERT_THAT(IsFalse(Brain->GetAsyncOps().IsPending(Handle), TEXT("the completed move op is still pending")));
		Brain->GetAsyncOps().CancelAll();
	}

	TEST_METHOD(ReplacementNeverReusesAnActivationHandle)
	{
		const FADSKCompileResult Loaded = Load(TEXT(""), TEXT("<Wait id=\"w\" duration=\"100\"/>"));
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		FADSKTestBrain Brain;
		const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(64);
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef(), Recorder);
		Brain->StartLogic();
		const FADSKActivationHandle OldHandle = Brain->GetInstance()->GetActiveLeafHandle();
		FString Error;
		ASSERT_THAT(IsTrue(Brain->ReplaceDefinition(Loaded.Definition.ToSharedRef(), true, Error), Error));
		const FADSKActivationHandle NewHandle = Brain->GetInstance()->GetActiveLeafHandle();
		ASSERT_THAT(IsTrue(OldHandle != NewHandle, TEXT("the leaf did not restart")));
		Brain->GetInstance()->PostActionResult(OldHandle, EADSKNodeStatus::Succeeded);
		Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(Brain->GetInstance()->GetActiveLeafHandle() == NewHandle, TEXT("the stale result changed the leaf")));
		ASSERT_THAT(AreEqual(1, ADSKTest::CountRecords(Recorder.Get(), EADSKRecordKind::ResultStale)));
	}

	TEST_METHOD(ReinitializationNeverReusesAnActivationHandle)
	{
		const FADSKCompileResult Loaded = Load(TEXT(""), TEXT("<Wait id=\"w\" duration=\"100\"/>"));
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		FADSKTestBrain Brain;
		const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(64);
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef(), Recorder);
		Brain->StartLogic();
		const FADSKActivationHandle OldHandle = Brain->GetInstance()->GetActiveLeafHandle();
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef(), Recorder);
		Brain->StartLogic();
		const FADSKActivationHandle NewHandle = Brain->GetInstance()->GetActiveLeafHandle();
		ASSERT_THAT(IsTrue(OldHandle != NewHandle, TEXT("the leaf did not restart")));
		Brain->GetInstance()->PostActionResult(OldHandle, EADSKNodeStatus::Succeeded);
		Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(Brain->GetInstance()->GetActiveLeafHandle() == NewHandle, TEXT("the stale result changed the leaf")));
		ASSERT_THAT(AreEqual(1, ADSKTest::CountRecords(Recorder.Get(), EADSKRecordKind::ResultStale)));
	}
};

TEST_CLASS_WITH_FLAGS(FHostNodesSpec, "AdamskAI.Host.Nodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ExposeDefaultsAndRejectInvalidConfiguration)
	{
		const TSharedRef<const FADSKNodeRegistry> Registry = ADSKTest::PluginCatalogue();
		ASSERT_THAT(IsTrue(Registry->ExportSchemaJson().Contains(TEXT("\"MoveTo\"")), TEXT("the schema has no MoveTo")));
		ASSERT_THAT(IsTrue(Registry->ExportSchemaJson().Contains(TEXT("\"default\":\"50.0\"")), TEXT("the schema has no 50.0 default")));
		const FString Input = TEXT("<Input name=\"Goal\" type=\"Vector\" default=\"0,0,0\"/>");
		ASSERT_THAT(AreEqual(1, Load(Input, TEXT("<MoveTo id=\"m\" acceptanceRadius=\"-1\" in-goal=\"Goal\"/>")).Count(EADSKDiagnosticCode::OutOfRange)));
		ASSERT_THAT(AreEqual(1, Load(Input, TEXT("<LookAt id=\"l\" duration=\"-1\" in-point=\"Goal\"/>")).Count(EADSKDiagnosticCode::OutOfRange)));
		ASSERT_THAT(AreEqual(1, Load(Input, TEXT("<MoveSpeed id=\"s\" speed=\"-1\"><Wait id=\"w\"/></MoveSpeed>")).Count(EADSKDiagnosticCode::OutOfRange)));
		ASSERT_THAT(AreEqual(1, Load(Input, TEXT("<Focus id=\"f\" in-point=\"Goal\"/>")).Count(EADSKDiagnosticCode::BadArity)));
		ASSERT_THAT(IsTrue(Registry->ExportSchemaJson().Contains(TEXT("\"name\":\"Focus\",\"version\":1,\"kind\":\"scope\"")), TEXT("the schema has no Focus scope")));
		ASSERT_THAT(IsTrue(Registry->ExportSchemaJson().Contains(TEXT("\"name\":\"MoveSpeed\",\"version\":1,\"kind\":\"scope\"")), TEXT("the schema has no MoveSpeed scope")));
		ASSERT_THAT(IsTrue(Registry->ExportSchemaJson().Contains(TEXT("{\"name\":\"pick\",\"type\":\"Choice\",\"default\":\"Best\",\"values\":[\"Best\",\"RandomBest5\",\"RandomBest25\"]}")), TEXT("the schema has no pick choice")));
	}

	TEST_METHOD(ScopesFailWithoutTheirHost)
	{
		// A brain with no owner has neither an AI controller (Focus) nor a character (MoveSpeed).
		for (const TCHAR* Tree : {TEXT("<Focus id=\"f\" in-point=\"Goal\"><Wait id=\"w\" duration=\"100\"/></Focus>"), TEXT("<MoveSpeed id=\"s\" speed=\"300\"><Wait id=\"w\" duration=\"100\"/></MoveSpeed>")})
		{
			const FADSKCompileResult Loaded = LoadWithOutcome(Tree);
			ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
			FADSKTestBrain Brain;
			Brain->InitializeDefinition(Loaded.Definition.ToSharedRef());
			Brain->StartLogic();
			Brain->TickBrain(0.1f);
			ASSERT_THAT(AreEqual(FString(TEXT("lost")), LeafState(*Brain)));
		}
	}
};

TEST_CLASS_WITH_FLAGS(FBrainEventsAndSeedSpec, "AdamskAI.Host.BrainEventsAndSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(EnqueueEventNeedsARunningBrain)
	{
		const FADSKCompileResult Loaded = LoadWithOutcome(TEXT("<Wait id=\"w\" duration=\"100\"/>"));
		FADSKTestBrain Brain;
		ASSERT_THAT(AreEqual(EADSKInputResult::NotRunning, Brain->EnqueueEvent(TEXT("Go"))));
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef());
		ASSERT_THAT(AreEqual(EADSKInputResult::NotRunning, Brain->EnqueueEvent(TEXT("Go"))));
		Brain->StartLogic();
		TestRunner->AddExpectedError(TEXT("has no event named 'Nope'"), EAutomationExpectedErrorFlags::Contains, 1);
		ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Brain->EnqueueEvent(TEXT("Nope"))));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Brain->EnqueueEvent(TEXT("Go"))));
		Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("won")), LeafState(*Brain)));
	}

	/** A misspelled Input or event warns once per name, however often the host writes it. */
	TEST_METHOD(AnUnknownNameWarnsOncePerName)
	{
		const FADSKCompileResult Loaded = LoadWithOutcome(TEXT("<Wait id=\"w\" duration=\"100\"/>"));
		FADSKTestBrain Brain;
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef());
		Brain->StartLogic();
		TestRunner->AddExpectedError(TEXT("has no Input named 'Speeed'"), EAutomationExpectedErrorFlags::Contains, 1);
		TestRunner->AddExpectedError(TEXT("has no Input named 'Other'"), EAutomationExpectedErrorFlags::Contains, 1);
		TestRunner->AddExpectedError(TEXT("has no event named 'Goo'"), EAutomationExpectedErrorFlags::Contains, 1);
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Brain->SetInput(TEXT("Speeed"), FADSKValue::MakeFloat(1.f))));
			ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Brain->EnqueueEvent(TEXT("Goo"))));
		}
		ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Brain->SetInput(TEXT("Other"), FADSKValue::MakeFloat(1.f))));
	}

	/** An id resolved for a name the file lacks warns once at FindInput; writing it every tick leaves no trace record. */
	TEST_METHOD(AnUnresolvedIdWarnsOnceAndIsNotRecorded)
	{
		const FADSKCompileResult Loaded = LoadWithOutcome(TEXT("<Wait id=\"w\" duration=\"100\"/>"));
		const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(64);
		FADSKTestBrain Brain;
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef(), Recorder);
		Brain->StartLogic();
		TestRunner->AddExpectedError(TEXT("has no Input named 'Gone'"), EAutomationExpectedErrorFlags::Contains, 1);
		TestRunner->AddExpectedError(TEXT("has no event named 'Gone'"), EAutomationExpectedErrorFlags::Contains, 1);
		const FADSKInputId Input = Brain->FindInput(TEXT("Gone"));
		const FADSKEventId Event = Brain->FindEvent(TEXT("Gone"));
		ASSERT_THAT(IsFalse(Input.IsValid() || Event.IsValid(), TEXT("an unknown name resolved")));
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Brain->SetInput(Brain->FindInput(TEXT("Gone")), FADSKValue::MakeFloat(1.f))));
			ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Brain->EnqueueEvent(Event)));
			Brain->TickBrain(0.1f);
		}
		ASSERT_THAT(IsFalse(Recorder->Snapshot().ContainsByPredicate([](const FADSKRecord& R) { return R.Kind == EADSKRecordKind::InputRejected; }),
			TEXT("an unresolved id was recorded as a rejection")));
	}

	TEST_METHOD(AFixedSeedReplaysAcrossRestarts)
	{
		const FADSKCompileResult Loaded = LoadWithOutcome(TEXT("<Sequence id=\"q\"><Roll id=\"r\" count=\"100000\" out-value=\"Plan\"/><Wait id=\"w\" duration=\"100\"/></Sequence>"));
		FADSKTestBrain Brain;
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef());
		Brain->RandomSeed = 1234;
		Brain->StartLogic();
		ASSERT_THAT(AreEqual(1234, Brain->GetRandomSeed()));
		FADSKValue First, Second;
		Brain->GetInstance()->GetValue(TEXT("Plan"), First);
		Brain->RestartLogic();
		Brain->GetInstance()->GetValue(TEXT("Plan"), Second);
		ASSERT_THAT(AreEqual(First.Int, Second.Int));
	}
};

#endif
