// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "Support/CompilerSpecSupport.h"
#include "Validate/ADSKAssetValidation.h"
#include "Validate/ADSKValidateReport.h"
#include "Validate/AdamskAIValidateCommandlet.h"
#include "CQTest.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Support/TempTree.h"
#include "UObject/Package.h"

namespace FValidateSpecPrivate
{
	const TCHAR* BrokenXml = TEXT("<Agent version=\"1\" id=\"a\">\n<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>\n<Tree id=\"t\"><Nope id=\"w\"/></Tree>\n</Agent>");

	struct FHarness
	{
		FHarness()
		{
			// Loops on its own completion, so it never draws a hold-risk note into the expected output.
			Tree.Write(TEXT("plugin/Examples/ok.xml"), FADSKTestPaths::ValidAgentXml().Replace(TEXT("tree=\"t\"/>"), TEXT("tree=\"t\"><On event=\"tree.success\" goto=\"s\"/></State>")));
			Environment.PluginBase = Tree.Path(TEXT("plugin"));
			Environment.ProjectDir = Tree.Path(TEXT("project"));
			Environment.SavedDir = Tree.Path(TEXT("project/Saved"));
			Environment.Print = [this](const FString& Line) { Lines.Add(Line); };
			UAdamskAIValidateCommandlet::SetEnvironmentOverride(&Environment);
		}

		~FHarness() { UAdamskAIValidateCommandlet::SetEnvironmentOverride(nullptr); }

		int32 Run(const FString& Params)
		{
			Lines.Reset();
			return NewObject<UAdamskAIValidateCommandlet>(GetTransientPackage())->Main(Params);
		}

		FADSKCheck EndsWithMarker(int32 Exit) const
		{
			if (!Lines.IsEmpty() && Lines.Last() == FString::Printf(TEXT("ADSK_VALIDATE_COMPLETE exit=%d"), Exit))
			{
				return FADSKCheck::Pass();
			}
			return FADSKCheck::Fail(FString::Printf(TEXT("expected the exit=%d marker last; output:\n  "), Exit) + Output());
		}

		FString Output() const
		{
			return FString::Join(Lines, TEXT("\n  "));
		}

		FADSKTempTree Tree;
		FADSKValidateEnvironment Environment;
		TArray<FString> Lines;
	};

	TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}
}

using namespace FValidateSpecPrivate;

TEST_CLASS_WITH_FLAGS(FValidateCommandletSpec, "AdamskAI.Validate.Commandlet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AssetValidationWarnsOnlyForAbsentPaths)
	{
		TArray<FString> Lines = FCompilerSpecSupport::BaseLines();
		Lines[14] = TEXT("<PathProbe id=\"present\" query=\"/Engine/EngineResources/DefaultTexture.DefaultTexture\"/><PathProbe id=\"absent\" query=\"/Game/Missing/EQS_Nope.EQS_Nope\"/>");
		Lines[14] = FString::Printf(TEXT("<Sequence id=\"q\">%s</Sequence>"), *Lines[14]);
		FADSKCompileResult Result = FCompilerSpecSupport::LoadPathProbe(FString::Join(Lines, TEXT("\n")));
		ASSERT_THAT(IsTrue(Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
		FADSKAssetValidation::AppendMissingAssets(*Result.Definition, Result);
		ASSERT_THAT(AreEqual(1, Result.Count(EADSKDiagnosticCode::MissingAsset)));
		const FADSKDiagnostic* Diagnostic = Result.FindFirst(EADSKDiagnosticCode::MissingAsset);
		ASSERT_THAT(AreEqual(EADSKSeverity::Warning, Diagnostic->Severity, FString::Printf(TEXT("severity is %s"), LexToString(Diagnostic->Severity))));
		ASSERT_THAT(AreEqual(FName(TEXT("absent")), Diagnostic->NodeId));
		ASSERT_THAT(AreEqual(15, Diagnostic->Line));
		ASSERT_THAT(AreEqual(FString(TEXT("spec.xml")), Diagnostic->Source));
		ASSERT_THAT(AreEqual(FString(TEXT("object '/Game/Missing/EQS_Nope.EQS_Nope' not found in Asset Registry")), Diagnostic->Message));
	}

	TEST_METHOD(CleanDefaultRunExitsZeroWithSummaryThenMarker)
	{
		FHarness Harness;
		ASSERT_THAT(AreEqual(0, Harness.Run(TEXT(""))));
		ADSK_ASSERT(Harness.EndsWithMarker(0));
		ASSERT_THAT(IsTrue(Harness.Lines.Num() >= 2 && Harness.Lines[Harness.Lines.Num() - 2].StartsWith(TEXT("summary: mode=default files=1 errors=0")), Harness.Output()));
	}

	TEST_METHOD(AHoldRiskIsANoteThatKeepsTheExitAtZero)
	{
		FHarness Harness;
		Harness.Tree.Write(TEXT("project/Content/AI/hold.xml"), TEXT("<Agent version=\"1\" id=\"h\">\n<StateMachine id=\"m\" initial=\"s\">\n<State id=\"s\" tree=\"t\"/>\n</StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"1\"/></Tree></Agent>"));
		ASSERT_THAT(AreEqual(0, Harness.Run(TEXT(""))));
		ASSERT_THAT(IsTrue(Harness.Lines.Contains(TEXT("Content/AI/hold.xml:3: note hold-risk: state 's': tree.success has no handler on its state path; the brain holds there when it does")), Harness.Output()));
		ASSERT_THAT(IsTrue(Harness.Lines[Harness.Lines.Num() - 2].Contains(TEXT("errors=0 warnings=0 notes=")), Harness.Output()));
		ADSK_ASSERT(Harness.EndsWithMarker(0));
	}

	TEST_METHOD(BrokenFileExitsOneWithAFormattedLine)
	{
		FHarness Harness;
		Harness.Tree.Write(TEXT("project/Content/AI/bad.xml"), BrokenXml);
		ASSERT_THAT(AreEqual(1, Harness.Run(TEXT(""))));
		ASSERT_THAT(IsTrue(Harness.Lines[0].StartsWith(TEXT("Content/AI/bad.xml:3: error unknown-node: ")), Harness.Output()));
		ADSK_ASSERT(Harness.EndsWithMarker(1));
	}

	TEST_METHOD(NodeIdIsBracketedOnlyWhenPresent)
	{
		ASSERT_THAT(AreEqual(FString(TEXT("a.xml:4: error unknown-port [w]: m")), FADSKValidateReport::FormatRow(FADSKValidateReport::MakeRow(TEXT("a.xml"), TEXT("error"), TEXT("unknown-port"), TEXT("m"), 4, TEXT("w")))));
		ASSERT_THAT(AreEqual(FString(TEXT("a.xml:2: warning unreferenced-tree: m")), FADSKValidateReport::FormatRow(FADSKValidateReport::MakeRow(TEXT("a.xml"), TEXT("warning"), TEXT("unreferenced-tree"), TEXT("m"), 2))));
	}

	TEST_METHOD(MissingPathedFileExitsOneAsSourceRead)
	{
		FHarness Harness;
		ASSERT_THAT(AreEqual(1, Harness.Run(FString::Printf(TEXT("-paths=%s"), *Harness.Tree.Path(TEXT("missing.xml"))))));
		ASSERT_THAT(IsTrue(Harness.Lines[0].Contains(TEXT(":0: error source-read: no such file: ")) && Harness.Lines[0].Contains(TEXT("missing.xml")), Harness.Output()));
		ADSK_ASSERT(Harness.EndsWithMarker(1));
	}

	TEST_METHOD(PathsModeAcceptsFilesDirectoriesAndPlusLists)
	{
		FHarness Harness;
		const FString Bad = Harness.Tree.Write(TEXT("extra/bad.xml"), BrokenXml);
		ASSERT_THAT(AreEqual(0, Harness.Run(FString::Printf(TEXT("-paths=%s"), *Harness.Tree.Path(TEXT("plugin/Examples"))))));
		ASSERT_THAT(AreEqual(1, Harness.Run(FString::Printf(TEXT("-paths=%s+%s"), *Harness.Tree.Path(TEXT("plugin/Examples/ok.xml")), *Bad))));
	}

	TEST_METHOD(MissingQueryAssetIsOneFormattedWarningAndExitOne)
	{
		FHarness Harness;
		const FString Template = TEXT("<Agent version=\"1\" id=\"a\">\n<Inputs/><Locals><Local name=\"P\" type=\"Vector\"/></Locals><Events/>\n<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>\n<Tree id=\"t\">\n<RunEQS id=\"e\" query=\"@PATH@\" out-point=\"P\"/>\n</Tree>\n</Agent>");
		const FString Missing = Harness.Tree.Write(TEXT("extra/eqs_missing.xml"), Template.Replace(TEXT("@PATH@"), TEXT("/Game/Missing/EQS_Nope.EQS_Nope")));
		ASSERT_THAT(AreEqual(1, Harness.Run(FString::Printf(TEXT("-paths=%s"), *Missing))));
		ASSERT_THAT(IsTrue(Harness.Lines[0].EndsWith(TEXT("eqs_missing.xml:5: warning missing-asset [e]: object '/Game/Missing/EQS_Nope.EQS_Nope' not found in Asset Registry")), Harness.Output()));
		// The file holds after its query too, which is a note printed after the warning.
		ASSERT_THAT(IsTrue(Harness.Lines[1].Contains(TEXT("note hold-risk")), Harness.Output()));
		ASSERT_THAT(IsTrue(Harness.Lines[Harness.Lines.Num() - 2].StartsWith(TEXT("summary:")), Harness.Output()));
		ADSK_ASSERT(Harness.EndsWithMarker(1));
		const FString Present = Harness.Tree.Write(TEXT("extra/eqs_present.xml"), Template.Replace(TEXT("@PATH@"), TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture")));
		ASSERT_THAT(AreEqual(0, Harness.Run(FString::Printf(TEXT("-paths=%s"), *Present))));
	}

	TEST_METHOD(UsageErrorsExitTwo)
	{
		FHarness Harness;
		for (const TCHAR* Bad : {TEXT("nonsense"), TEXT("-paths"), TEXT("-paths= -fixtures"), TEXT("-fixtures -describe=x.json"), TEXT("-describe=a.json -catalog=b.md"),
				 TEXT("-describe=a.json -registry=everything"), TEXT("-fixtures -registry=plugin"), TEXT("-fixtures=1"), TEXT("-paths=a.xml -check"),
				 TEXT("-xsd=a.xsd -catalog=b.md"), TEXT("-projectxsd=1"), TEXT("-projectxsd -registry=plugin"), TEXT("-simulate"), TEXT("-simulate=a.json -check"),
				 TEXT("-simulate=a.json -registry=full")})
		{
			ASSERT_THAT(AreEqual(2, Harness.Run(Bad)));
			ADSK_ASSERT(Harness.EndsWithMarker(2));
		}
		ASSERT_THAT(IsFalse(Harness.Tree.Exists(TEXT("project/Saved/AdamskAI/validate.json")), TEXT("a usage error wrote validate.json")));
	}

	TEST_METHOD(SetupFailuresExitTwo)
	{
		FHarness Harness;
		IFileManager::Get().DeleteDirectory(*Harness.Tree.Path(TEXT("plugin/Examples")), false, true);
		ASSERT_THAT(AreEqual(2, Harness.Run(TEXT(""))));
		ASSERT_THAT(AreEqual(2, Harness.Run(TEXT("-fixtures"))));
		IFileManager::Get().MakeDirectory(*Harness.Tree.Path(TEXT("empty")), true);
		ASSERT_THAT(AreEqual(2, Harness.Run(FString::Printf(TEXT("-paths=%s"), *Harness.Tree.Path(TEXT("empty"))))));
	}

	TEST_METHOD(MachineReportIsVersionedParseableJson)
	{
		FHarness Harness;
		Harness.Tree.Write(TEXT("project/Content/AI/bad.xml"), BrokenXml);
		ASSERT_THAT(AreEqual(1, Harness.Run(TEXT(""))));
		const TSharedPtr<FJsonObject> Report = ParseJson(Harness.Tree.Read(TEXT("project/Saved/AdamskAI/validate.json")));
		ASSERT_THAT(IsTrue(Report.IsValid(), TEXT("validate.json is not JSON: ") + Harness.Tree.Read(TEXT("project/Saved/AdamskAI/validate.json"))));
		ASSERT_THAT(AreEqual(FADSKValidateReport::JsonVersion, static_cast<int32>(Report->GetNumberField(TEXT("version")))));
		ASSERT_THAT(AreEqual(1, static_cast<int32>(Report->GetNumberField(TEXT("exit")))));
		ASSERT_THAT(AreEqual(1, static_cast<int32>(Report->GetNumberField(TEXT("errors")))));
		const TArray<TSharedPtr<FJsonValue>>& Rows = Report->GetArrayField(TEXT("diagnostics"));
		ASSERT_THAT(AreEqual(1, Rows.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("unknown-node")), Rows[0]->AsObject()->GetStringField(TEXT("code"))));
		ASSERT_THAT(AreEqual(3, static_cast<int32>(Rows[0]->AsObject()->GetNumberField(TEXT("line")))));
	}

	TEST_METHOD(FailedReportWriteKeepsTheOldFileAndExitsTwo)
	{
		FHarness Harness;
		Harness.Tree.Write(TEXT("project/Saved/AdamskAI/validate.json"), TEXT("{\"old\":true}"));
		Harness.Environment.WriteHooks.Replace = [](const FString&, const FString&) { return false; };
		ASSERT_THAT(AreEqual(2, Harness.Run(TEXT(""))));
		ASSERT_THAT(AreEqual(FString(TEXT("{\"old\":true}")), Harness.Tree.Read(TEXT("project/Saved/AdamskAI/validate.json"))));
		ASSERT_THAT(AreEqual(1, Harness.Tree.CountFiles(TEXT("project/Saved/AdamskAI"))));
		ADSK_ASSERT(Harness.EndsWithMarker(2));
	}

	TEST_METHOD(FixturesModeValidatesThePluginFixtures)
	{
		FHarness Harness;
		Harness.Environment.PluginBase = FADSKTestPaths::PluginBase();
		ASSERT_THAT(AreEqual(0, Harness.Run(TEXT("-fixtures"))));
		ADSK_ASSERT(Harness.EndsWithMarker(0));
	}

	TEST_METHOD(DescribeAndCatalogWriteThenCheckDetectsStaleness)
	{
		FHarness Harness;
		const FString Describe = Harness.Tree.Path(TEXT("out/schema.json"));
		const FString Catalog = Harness.Tree.Path(TEXT("out/catalog.md"));
		ASSERT_THAT(AreEqual(0, Harness.Run(FString::Printf(TEXT("-describe=%s -registry=plugin"), *Describe))));
		ASSERT_THAT(IsTrue(ParseJson(Harness.Tree.Read(TEXT("out/schema.json"))).IsValid(), TEXT("schema.json is not JSON")));
		ASSERT_THAT(AreEqual(1, Harness.Run(FString::Printf(TEXT("-catalog=%s -check"), *Catalog))));
		ASSERT_THAT(IsTrue(Harness.Lines[0].Contains(TEXT("catalogue-stale")), Harness.Output()));
		ASSERT_THAT(IsFalse(Harness.Tree.Exists(TEXT("out/catalog.md")), TEXT("-check wrote the catalogue")));
		ASSERT_THAT(AreEqual(0, Harness.Run(FString::Printf(TEXT("-catalog=%s"), *Catalog))));
		ASSERT_THAT(AreEqual(0, Harness.Run(FString::Printf(TEXT("-catalog=%s -check"), *Catalog))));
		Harness.Tree.Write(TEXT("out/catalog.md"), Harness.Tree.Read(TEXT("out/catalog.md")).Replace(TEXT("`1.0`"), TEXT("`9.0`")));
		const FString Stale = Harness.Tree.Read(TEXT("out/catalog.md"));
		ASSERT_THAT(AreEqual(1, Harness.Run(FString::Printf(TEXT("-catalog=%s -check"), *Catalog))));
		ASSERT_THAT(AreEqual(Stale, Harness.Tree.Read(TEXT("out/catalog.md"))));
		ASSERT_THAT(AreEqual(2, Harness.Run(FString::Printf(TEXT("-catalog=%s -registry=nope"), *Catalog))));
	}

	TEST_METHOD(XsdWritesThenCheckDetectsStaleness)
	{
		FHarness Harness;
		const FString Schema = Harness.Tree.Path(TEXT("out/adamskai.xsd"));
		ASSERT_THAT(AreEqual(1, Harness.Run(FString::Printf(TEXT("-xsd=%s -registry=plugin -check"), *Schema))));
		ASSERT_THAT(IsTrue(Harness.Lines[0].Contains(TEXT("catalogue-stale")) && Harness.Lines[0].Contains(TEXT("-xsd")), Harness.Output()));
		ASSERT_THAT(AreEqual(0, Harness.Run(FString::Printf(TEXT("-xsd=%s -registry=plugin"), *Schema))));
		ASSERT_THAT(IsTrue(Harness.Tree.Read(TEXT("out/adamskai.xsd")).StartsWith(TEXT("<?xml")), TEXT("the schema was not written")));
		ASSERT_THAT(AreEqual(0, Harness.Run(FString::Printf(TEXT("-xsd=%s -registry=plugin -check"), *Schema))));
		Harness.Tree.Write(TEXT("out/adamskai.xsd"), Harness.Tree.Read(TEXT("out/adamskai.xsd")).Replace(TEXT("name=\"MoveTo\""), TEXT("name=\"MoveTwo\"")));
		ASSERT_THAT(AreEqual(1, Harness.Run(FString::Printf(TEXT("-xsd=%s -registry=plugin -check"), *Schema))));
		ADSK_ASSERT(Harness.EndsWithMarker(1));
	}

	TEST_METHOD(SimulateModePrintsTheRunAndExitsOneOnAFinding)
	{
		FHarness Harness;
		Harness.Tree.Write(TEXT("sim/b.xml"), TEXT("<Agent version=\"1\" id=\"h\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"0.5\"/></Tree></Agent>"));
		const FString Holds = Harness.Tree.Write(TEXT("sim/holds.json"), TEXT("{\"behavior\":\"b.xml\",\"duration\":1}"));
		ASSERT_THAT(AreEqual(1, Harness.Run(FString::Printf(TEXT("-simulate=%s"), *Holds))));
		ASSERT_THAT(IsTrue(Harness.Lines.ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("error simulate-hold")); }), Harness.Output()));
		ASSERT_THAT(IsTrue(Harness.Lines.ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("b.xml, ")) && Line.Contains(TEXT(" steps of ")); }), Harness.Output()));
		ASSERT_THAT(IsTrue(Harness.Tree.Exists(TEXT("project/Saved/AdamskAI/simulate/holds.trace")), TEXT("no trace written")));
		ADSK_ASSERT(Harness.EndsWithMarker(1));
		Harness.Tree.Write(TEXT("sim/held.json"), TEXT("{\"behavior\":\"b.xml\",\"duration\":1,\"expect\":{\"hold\":true}}"));
		ASSERT_THAT(AreEqual(1, Harness.Run(FString::Printf(TEXT("-simulate=%s"), *Harness.Tree.Path(TEXT("sim"))))));
		IFileManager::Get().Delete(*Holds);
		ASSERT_THAT(AreEqual(0, Harness.Run(FString::Printf(TEXT("-simulate=%s"), *Harness.Tree.Path(TEXT("sim"))))));
		ASSERT_THAT(AreEqual(2, Harness.Run(FString::Printf(TEXT("-simulate=%s"), *Harness.Tree.Path(TEXT("missing.json"))))));
		Harness.Tree.Write(TEXT("sim/bad.json"), TEXT("{\"behavior\":\"b.xml\",\"nope\":1}"));
		ASSERT_THAT(AreEqual(2, Harness.Run(FString::Printf(TEXT("-simulate=%s"), *Harness.Tree.Path(TEXT("sim/bad.json"))))));
	}
};

TEST_CLASS_WITH_FLAGS(FValidateAtomicWriteSpec, "AdamskAI.Validate.AtomicWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ReplacesAnExistingFile)
	{
		FADSKTempTree Tree;
		Tree.Write(TEXT("a/out.json"), TEXT("old"));
		FString Error;
		ASSERT_THAT(IsTrue(FADSKValidateReport::WriteAtomic(Tree.Path(TEXT("a/out.json")), TEXT("new"), FADSKAtomicWriteHooks(), Error), Error));
		ASSERT_THAT(AreEqual(FString(TEXT("new")), Tree.Read(TEXT("a/out.json"))));
		ASSERT_THAT(AreEqual(1, Tree.CountFiles(TEXT("a"))));
	}

	TEST_METHOD(AnyStepFailurePreservesTheFinalFileAndRemovesTheTemp)
	{
		FADSKTempTree Tree;
		const FString Target = Tree.Write(TEXT("a/out.json"), TEXT("old"));
		FString Error;
		FADSKAtomicWriteHooks SaveFails;
		SaveFails.Save = [](const FString&, const FString&) { return false; };
		ASSERT_THAT(IsFalse(FADSKValidateReport::WriteAtomic(Target, TEXT("new"), SaveFails, Error), TEXT("the write succeeded although the save failed")));
		FADSKAtomicWriteHooks PartialSave;
		PartialSave.Save = [](const FString& Temp, const FString&) { FFileHelper::SaveStringToFile(TEXT("par"), *Temp); return false; };
		ASSERT_THAT(IsFalse(FADSKValidateReport::WriteAtomic(Target, TEXT("new"), PartialSave, Error), TEXT("the write succeeded although the save was partial")));
		FADSKAtomicWriteHooks MoveFails;
		MoveFails.Replace = [](const FString&, const FString&) { return false; };
		ASSERT_THAT(IsFalse(FADSKValidateReport::WriteAtomic(Target, TEXT("new"), MoveFails, Error), TEXT("the write succeeded although the replace failed")));
		ASSERT_THAT(AreEqual(FString(TEXT("old")), Tree.Read(TEXT("a/out.json"))));
		ASSERT_THAT(AreEqual(1, Tree.CountFiles(TEXT("a"))));
	}
};

#endif
