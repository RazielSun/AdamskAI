// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Debug/ADSKDebugSnapshot.h"
#include "Debug/ADSKTraceExport.h"
#include "Compiler/ADSKCompiler.h"
#include "Support/TempTree.h"
#include "AIController.h"
#include "CQTest.h"
#include "Support/TestBrain.h"
#include "Support/TestChecks.h"
#include "Components/ActorTestSpawner.h"
#include "GameFramework/Pawn.h"

namespace FEnumHostSpecPrivate
{
	FADSKCompileResult Load(const FString& Enums, const FString& Input)
	{
		const TSharedRef<const FADSKNodeRegistry> Registry = ADSKTest::PluginCatalogue();
		const FString Xml = TEXT("<Agent version=\"1\" id=\"enum-host\"><Enums>") + Enums
			+ TEXT("</Enums><Inputs>") + Input + TEXT("</Inputs><StateMachine id=\"m\" initial=\"s\">"
			"<State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>");
		return FADSKCompiler::Compile(TEXT("enum-host.xml"), Xml, Registry);
	}

	const FString OriginalEnums = TEXT("<Enum name=\"Intent\"><Value name=\"None\"/><Value name=\"Engage\"/></Enum>");
	const FString OriginalInput = TEXT("<Input name=\"Mode\" type=\"Intent\" required=\"true\"/>");

	bool SameValueMap(const FADSKInputValues& A, const FADSKInputValues& B)
	{
		if (A.Num() != B.Num()) return false;
		for (const TPair<FString, FADSKValue>& Entry : A)
		{
			const FADSKValue* Other = B.Find(Entry.Key);
			if (!Other || *Other != Entry.Value) return false;
		}
		return true;
	}

	bool SameSymbolMap(const FADSKEnumInputValues& A,
		const FADSKEnumInputValues& B)
	{
		if (A.Num() != B.Num()) return false;
		for (const TPair<FString, TPair<FString, FString>>& Entry : A)
		{
			const TPair<FString, FString>* Other = B.Find(Entry.Key);
			if (!Other || Other->Key != Entry.Value.Key || Other->Value != Entry.Value.Value) return false;
		}
		return true;
	}
}

using namespace FEnumHostSpecPrivate;

TEST_CLASS_WITH_FLAGS(FEnumBrainSpec, "AdamskAI.Host.Enum.Brain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(SymbolicWriteAndRawWriteCanonicalizeBothCaches)
	{
		const FADSKCompileResult Loaded = Load(OriginalEnums, OriginalInput);
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		FADSKTestBrain Brain;
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef(), FADSKRecorder::Create(64));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Brain->SetEnumInput(TEXT("Mode"), TEXT("Engage"))));
		ASSERT_THAT(IsTrue(Brain->GetCachedInputs()[TEXT("Mode")] == FADSKValue::MakeEnum(0, 1), LexToString(Brain->GetCachedInputs()[TEXT("Mode")])));
		ASSERT_THAT(AreEqual(FString(TEXT("Intent")), Brain->GetCachedEnumInputs()[TEXT("Mode")].Key));
		ASSERT_THAT(AreEqual(FString(TEXT("Engage")), Brain->GetCachedEnumInputs()[TEXT("Mode")].Value));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Brain->SetInput(TEXT("Mode"), FADSKValue::MakeEnum(0, 0))));
		ASSERT_THAT(AreEqual(FString(TEXT("None")), Brain->GetCachedEnumInputs()[TEXT("Mode")].Value));
		const FADSKCompileResult Scalar = Load(FString(), TEXT("<Input name=\"Mode\" type=\"Bool\" required=\"true\"/>"));
		ASSERT_THAT(IsTrue(Scalar.IsSuccess(), ADSKTest::FormatDiagnostics(Scalar)));
		Brain->InitializeDefinition(Scalar.Definition.ToSharedRef(), FADSKRecorder::Create(64));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Brain->SetInput(TEXT("Mode"), FADSKValue::MakeBool(true))));
		ASSERT_THAT(IsFalse(Brain->GetCachedEnumInputs().Contains(TEXT("Mode")), TEXT("a Bool Mode kept its enum name")));
		ASSERT_THAT(IsTrue(Brain->GetCachedInputs()[TEXT("Mode")] == FADSKValue::MakeBool(true), LexToString(Brain->GetCachedInputs()[TEXT("Mode")])));
	}

	TEST_METHOD(ReorderedDeclarationsReplayFreshIndicesAndSatisfyRequiredInput)
	{
		const FADSKCompileResult Original = Load(OriginalEnums, OriginalInput);
		const FADSKCompileResult Reordered = Load(
			TEXT("<Enum name=\"Phase\"><Value name=\"Only\"/></Enum>"
			"<Enum name=\"Intent\"><Value name=\"Engage\"/><Value name=\"None\"/></Enum>"), OriginalInput);
		ASSERT_THAT(IsTrue(Original.IsSuccess(), ADSKTest::FormatDiagnostics(Original)));
		ASSERT_THAT(IsTrue(Reordered.IsSuccess(), ADSKTest::FormatDiagnostics(Reordered)));
		FADSKTestBrain Brain;
		Brain->InitializeDefinition(Original.Definition.ToSharedRef(), FADSKRecorder::Create(64));
		Brain->SetEnumInput(TEXT("Mode"), TEXT("Engage"));
		Brain->StartLogic();
		FString Error;
		ASSERT_THAT(IsTrue(Brain->ReplaceDefinition(Reordered.Definition.ToSharedRef(), true, Error), Error));
		ASSERT_THAT(IsTrue(Brain->IsADSKRunning(), Brain->GetLastError()));
		ASSERT_THAT(IsTrue(Brain->GetCachedInputs()[TEXT("Mode")] == FADSKValue::MakeEnum(1, 0), LexToString(Brain->GetCachedInputs()[TEXT("Mode")])));
		ASSERT_THAT(AreEqual(FString(TEXT("Intent")), Brain->GetCachedEnumInputs()[TEXT("Mode")].Key));
		ASSERT_THAT(AreEqual(FString(TEXT("Engage")), Brain->GetCachedEnumInputs()[TEXT("Mode")].Value));
		FADSKValue Value;
		ASSERT_THAT(IsTrue(Brain->GetInstance()->GetValue(TEXT("Mode"), Value), TEXT("no slot Mode")));
		ASSERT_THAT(IsTrue(Value == FADSKValue::MakeEnum(1, 0), LexToString(Value)));
	}

	TEST_METHOD(RejectedReloadLeavesInstanceRunningStateAndCachesUntouched)
	{
		const FADSKCompileResult Original = Load(OriginalEnums, OriginalInput);
		const TArray<FADSKCompileResult> Candidates = {
			Load(TEXT("<Enum name=\"AimIntent\"><Value name=\"None\"/><Value name=\"Engage\"/></Enum>"),
				TEXT("<Input name=\"Mode\" type=\"AimIntent\" required=\"true\"/>")),
			Load(TEXT("<Enum name=\"Intent\"><Value name=\"None\"/></Enum>"), OriginalInput),
			Load(TEXT("<Enum name=\"Intent\"><Value name=\"None\"/></Enum>"),
				TEXT("<Input name=\"Mode\" type=\"Bool\" required=\"true\"/>")),
		};
		ASSERT_THAT(IsTrue(Original.IsSuccess(), ADSKTest::FormatDiagnostics(Original)));
		FADSKTestBrain Brain;
		Brain->InitializeDefinition(Original.Definition.ToSharedRef(), FADSKRecorder::Create(64));
		Brain->SetEnumInput(TEXT("Mode"), TEXT("Engage"));
		Brain->StartLogic();
		for (const FADSKCompileResult& Candidate : Candidates)
		{
			ASSERT_THAT(IsTrue(Candidate.IsSuccess(), ADSKTest::FormatDiagnostics(Candidate)));
			const FADSKInstance* Instance = Brain->GetInstance();
			const FADSKDefinition* Definition = &Brain->GetDefinition().Get();
			const bool bRunning = Brain->IsADSKRunning();
			const FADSKInputValues Numeric = Brain->GetCachedInputs();
			const FADSKEnumInputValues Symbolic = Brain->GetCachedEnumInputs();
			FString Error;
			ASSERT_THAT(IsFalse(Brain->ReplaceDefinition(Candidate.Definition.ToSharedRef(), true, Error), TEXT("the changed input binding was accepted")));
			ASSERT_THAT(AreEqual(FString(TEXT("input binding changed: Mode")), Error));
			ASSERT_THAT(IsTrue(Brain->GetInstance() == Instance && &Brain->GetDefinition().Get() == Definition, TEXT("the rejected replacement changed the instance or the definition")));
			ASSERT_THAT(AreEqual(bRunning, Brain->IsADSKRunning()));
			ASSERT_THAT(IsTrue(SameValueMap(Numeric, Brain->GetCachedInputs()), TEXT("the cached inputs changed")));
			ASSERT_THAT(IsTrue(SameSymbolMap(Symbolic, Brain->GetCachedEnumInputs()), TEXT("the cached enum names changed")));
		}
	}
};

TEST_CLASS_WITH_FLAGS(FEnumDebugSpec, "AdamskAI.Host.Enum.Debug",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(SnapshotAndTraceUseNamesWithoutDuplicateJsonType)
	{
		FActorTestSpawner Spawner;
		AAIController& Controller = Spawner.SpawnActor<AAIController>();
		APawn& Pawn = Spawner.SpawnActor<APawn>();
		Controller.Possess(&Pawn);
		const FADSKCompileResult Loaded = Load(OriginalEnums, OriginalInput);
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Controller);
		Controller.AddInstanceComponent(Brain);
		Brain->RegisterComponent();
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef(), FADSKRecorder::Create(64));
		UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		Subsystem->RegisterBrain(TEXT("debug/enum.xml"), Brain);
		Brain->SetEnumInput(TEXT("Mode"), TEXT("Engage"));
		Brain->StartLogic();
		Brain->TickBrain(0.f);

		const FADSKDebugSnapshot Snapshot = FADSKDebugSnapshot::Collect(&Spawner.GetWorld());
		ASSERT_THAT(AreEqual(1, Snapshot.Rows.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("Intent:Engage")), Snapshot.Rows[0].Inputs[0].Value));
		ASSERT_THAT(AreEqual(FString(TEXT("Intent:Engage")), Snapshot.Rows[0].CachedInputs[0].Value));

		const FADSKTempTree Tree;
		const FADSKTraceExportResult Export = FADSKTraceExport::Write(*Brain, TEXT("EnumWorld"), Tree.Root);
		FString Jsonl;
		if (Export.IsSuccess()) FFileHelper::LoadFileToString(Jsonl, *Export.Path);
		ASSERT_THAT(IsTrue(Export.IsSuccess(), Export.Error));
		ASSERT_THAT(IsTrue(Jsonl.Contains(TEXT("\"value\":{\"type\":\"Intent\",\"value\":\"Engage\"}")), Jsonl));
		ASSERT_THAT(IsFalse(Jsonl.Contains(TEXT("\"type\":\"Enum\",\"type\":\"Intent\"")), Jsonl));

		Subsystem->UnregisterBrain(TEXT("debug/enum.xml"), Brain);
		Brain->Cleanup();
	}
};

#endif
