// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Compiler/ADSKCompiler.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Program/ADSKHoldAnalysis.h"
#include "Program/ADSKLint.h"
#include "Runtime/ADSKInstance.h"
#include "Support/ScriptedRun.h"
#include "Support/TempTree.h"
#include "CQTest.h"

// Seeded mutants of every example and fixture, valid and invalid; some mutations draw node names, fields and ports from
// the catalogue and the grammar. Each must compile to a definition or to errors on lines the file has, never crash; a
// definition must also pass the hold and lint analyses, start and tick, restart from its own snapshot, and start from a
// snapshot of the unmutated file, as a live reload does. The file being compiled is written to
// Saved/AdamskAI/fuzz/current.xml first, so a crash leaves its input behind; a mutant that breaks a rule is kept there
// under its file and seed. ADSK_FUZZ_MUTANTS sets mutants per file and round (default 24). By default the run makes one
// round within 20 s; ADSK_FUZZ_SECONDS sets a longer budget, and the run then repeats rounds with new seeds until it is
// spent (a soak). ADSK_FUZZ_SEED moves every seed, so two soaks cover different mutants.
namespace FFuzzSpecPrivate
{
	constexpr int32 DefaultMutantsPerFile = 24;
	constexpr double DefaultBudgetSeconds = 20.0;

	int32 EnvInt(const TCHAR* Name, int32 Default)
	{
		const FString Value = FPlatformMisc::GetEnvironmentVariable(Name);
		return Value.IsEmpty() ? Default : FCString::Atoi(*Value);
	}
	constexpr int32 BaseSeed = 20261029;

	FString FuzzDirectory() { return FPaths::ProjectSavedDir() / TEXT("AdamskAI/fuzz"); }

	TArray<FString> Corpus()
	{
		TArray<FString> Files;
		for (const TCHAR* Directory : {TEXT("Examples"), TEXT("Fixtures/valid"), TEXT("Fixtures/invalid")})
		{
			const FString Root = FADSKTestPaths::PluginBase() / Directory;
			TArray<FString> Names;
			IFileManager::Get().FindFiles(Names, *(Root / TEXT("*.xml")), true, false);
			Names.Sort();
			for (const FString& Name : Names) { Files.Add(Root / Name); }
		}
		return Files;
	}

	/** Offsets of every `="` that opens an attribute value. */
	TArray<int32> ValueStarts(const FString& Text)
	{
		TArray<int32> Starts;
		for (int32 Index = Text.Find(TEXT("=\"")); Index != INDEX_NONE; Index = Text.Find(TEXT("=\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 2))
		{
			Starts.Add(Index + 2);
		}
		return Starts;
	}

	/** The attribute ` name="value"` around the value starting at ValueStart, as [Begin, End). */
	bool AttributeAt(const FString& Text, int32 ValueStart, int32& OutBegin, int32& OutEnd)
	{
		const int32 Close = Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
		int32 Begin = ValueStart - 2;
		while (Begin > 0 && !FChar::IsWhitespace(Text[Begin - 1])) { --Begin; }
		if (Close == INDEX_NONE || Begin <= 0) return false;
		OutBegin = Begin - 1;
		OutEnd = Close + 1;
		return true;
	}

	/** The catalogue's node names and every field and port attribute they take: what an author could plausibly type. */
	struct FVocabulary
	{
		TArray<const FADSKNodeDescriptor*> Nodes;
		TArray<FString> Attributes;
	};

	FVocabulary MakeVocabulary(const FADSKNodeRegistry& Catalogue)
	{
		FVocabulary Out;
		Out.Nodes = Catalogue.GetDescriptors();
		// The grammar's own attributes, so a renamed one lands on state selection, chance and conditions too.
		for (const TCHAR* Name : {TEXT("chance"), TEXT("select"), TEXT("score"), TEXT("weight"), TEXT("bias"), TEXT("initial"), TEXT("goto"),
			TEXT("if"), TEXT("unless"), TEXT("is"), TEXT("below"), TEXT("above"), TEXT("tags"), TEXT("tree"), TEXT("change"), TEXT("priority")})
		{
			Out.Attributes.AddUnique(Name);
		}
		for (const FADSKNodeDescriptor* Node : Out.Nodes)
		{
			for (const FADSKFieldMap& Field : Node->Fields) { Out.Attributes.AddUnique(Field.XmlName); }
			for (const FADSKPortDescriptor& Port : Node->Ports) { Out.Attributes.AddUnique((Port.Direction == EADSKPortDirection::In ? TEXT("in-") : TEXT("out-")) + Port.Name); }
		}
		return Out;
	}

	/** Every `name="..."` value: the file's slots, events and parameters, so generated ports bind to something real. */
	TArray<FString> DeclaredNames(const FString& Text)
	{
		TArray<FString> Names;
		for (int32 Index = Text.Find(TEXT(" name=\"")); Index != INDEX_NONE; Index = Text.Find(TEXT(" name=\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 7))
		{
			const int32 Close = Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 7);
			if (Close != INDEX_NONE) { Names.AddUnique(Text.Mid(Index + 7, Close - Index - 7)); }
		}
		return Names;
	}

	/** Lines that open with a self-closing catalogue node, `<Name .../>`, and that node's name. */
	TArray<TPair<int32, FString>> LeafLines(const TArray<FString>& Lines, const FVocabulary& Vocabulary)
	{
		TArray<TPair<int32, FString>> Found;
		for (int32 Line = 0; Line < Lines.Num(); ++Line)
		{
			const FString Trimmed = Lines[Line].TrimStartAndEnd();
			if (!Trimmed.StartsWith(TEXT("<")) || !Trimmed.EndsWith(TEXT("/>"))) { continue; }
			int32 End = 1;
			while (End < Trimmed.Len() && FChar::IsAlnum(Trimmed[End])) { ++End; }
			const FString Name = Trimmed.Mid(1, End - 1);
			if (Vocabulary.Nodes.ContainsByPredicate([&Name](const FADSKNodeDescriptor* Node) { return Node->Name == Name; })) { Found.Emplace(Line, Name); }
		}
		return Found;
	}

	FString MakeNode(const FADSKNodeDescriptor& Node, const TArray<FString>& Names, FRandomStream& Random)
	{
		static const TCHAR* const Values[] = {TEXT("0"), TEXT("1"), TEXT("0.5"), TEXT("-1"), TEXT("true"), TEXT("none"), TEXT("1,2,3")};
		const auto Pick = [&Random, &Names]() { return Names.IsEmpty() ? FString(TEXT("Missing")) : Names[Random.RandRange(0, Names.Num() - 1)]; };
		FString Out = FString::Printf(TEXT("<%s id=\"fz%d\""), *Node.Name, Random.RandRange(0, 99999));
		for (const FADSKFieldMap& Field : Node.Fields)
		{
			if (Random.FRand() < 0.7f) { Out += FString::Printf(TEXT(" %s=\"%s\""), *Field.XmlName, Random.FRand() < 0.8f ? Values[Random.RandRange(0, UE_ARRAY_COUNT(Values) - 1)] : *Pick()); }
		}
		for (const FADSKPortDescriptor& Port : Node.Ports)
		{
			if (Random.FRand() < 0.85f) { Out += FString::Printf(TEXT(" %s-%s=\"%s\""), Port.Direction == EADSKPortDirection::In ? TEXT("in") : TEXT("out"), *Port.Name, *Pick()); }
		}
		return Out + TEXT("/>");
	}

	FString Mutate(const FString& Original, const FVocabulary& Vocabulary, FRandomStream& Random, FString& OutWhat)
	{
		FString Text = Original;
		const TArray<int32> Values = ValueStarts(Text);
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);
		const int32 Kind = Random.RandRange(0, 9);
		if (Values.IsEmpty() || Lines.Num() < 3) { return Text.Left(Random.RandRange(0, Text.Len())); }
		const int32 Value = Values[Random.RandRange(0, Values.Num() - 1)];
		int32 Begin = 0, End = 0;
		switch (Kind)
		{
		case 0:
			if (AttributeAt(Text, Value, Begin, End)) { OutWhat = TEXT("drop attribute ") + Text.Mid(Begin, End - Begin); Text.RemoveAt(Begin, End - Begin); }
			break;
		case 1:
			if (AttributeAt(Text, Value, Begin, End)) { OutWhat = TEXT("duplicate attribute ") + Text.Mid(Begin, End - Begin); Text.InsertAt(End, Text.Mid(Begin, End - Begin)); }
			break;
		case 2:
		{
			static const TCHAR* const Odd[] = {TEXT(""), TEXT("-1"), TEXT("0"), TEXT("1e39"), TEXT("-1e39"), TEXT("nan"), TEXT("inf"), TEXT("2147483648"),
				TEXT("-0.0"), TEXT("0x10"), TEXT("1,2"), TEXT("none"), TEXT("true"), TEXT("abc"), TEXT("&amp;"), TEXT("é"), TEXT(" 1 "),
				TEXT("best"), TEXT("weighted"), TEXT("0.0001"), TEXT("1.0")};
			const int32 Close = Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Value);
			if (Close != INDEX_NONE)
			{
				const FString Replacement = Odd[Random.RandRange(0, UE_ARRAY_COUNT(Odd) - 1)];
				OutWhat = FString::Printf(TEXT("value [%s] -> [%s]"), *Text.Mid(Value, Close - Value), *Replacement);
				Text = Text.Left(Value) + Replacement + Text.Mid(Close);
			}
			break;
		}
		case 3:
		{
			// Another value of the same file: ids, slot names and events land where they mean something else.
			const int32 Other = Values[Random.RandRange(0, Values.Num() - 1)];
			const int32 Close = Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Value);
			const int32 OtherClose = Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Other);
			if (Close != INDEX_NONE && OtherClose != INDEX_NONE)
			{
				const FString Replacement = Text.Mid(Other, OtherClose - Other);
				OutWhat = FString::Printf(TEXT("value [%s] -> [%s]"), *Text.Mid(Value, Close - Value), *Replacement);
				Text = Text.Left(Value) + Replacement + Text.Mid(Close);
			}
			break;
		}
		case 4:
		{
			const int32 Line = Random.RandRange(0, Lines.Num() - 1);
			OutWhat = FString::Printf(TEXT("drop line %d"), Line + 1);
			Lines.RemoveAt(Line);
			Text = FString::Join(Lines, TEXT("\n"));
			break;
		}
		case 5:
		{
			const int32 Line = Random.RandRange(0, Lines.Num() - 1);
			OutWhat = FString::Printf(TEXT("duplicate line %d"), Line + 1);
			const FString Copy = Lines[Line];
			Lines.Insert(Copy, Line);
			Text = FString::Join(Lines, TEXT("\n"));
			break;
		}
		case 7:
		case 8:
		{
			const TArray<TPair<int32, FString>> Leaves = LeafLines(Lines, Vocabulary);
			if (Leaves.IsEmpty()) { break; }
			const TPair<int32, FString>& Leaf = Leaves[Random.RandRange(0, Leaves.Num() - 1)];
			const FADSKNodeDescriptor& Other = *Vocabulary.Nodes[Random.RandRange(0, Vocabulary.Nodes.Num() - 1)];
			if (Kind == 7)
			{
				// Another node kind with the same attributes and no children: arity, fields and ports all mismatch.
				OutWhat = FString::Printf(TEXT("line %d: %s -> %s"), Leaf.Key + 1, *Leaf.Value, *Other.Name);
				Lines[Leaf.Key] = Lines[Leaf.Key].Replace(*(TEXT("<") + Leaf.Value), *(TEXT("<") + Other.Name), ESearchCase::CaseSensitive);
			}
			else
			{
				const FString Node = MakeNode(Other, DeclaredNames(Text), Random);
				OutWhat = FString::Printf(TEXT("after line %d insert %s"), Leaf.Key + 1, *Node);
				Lines.Insert(Node, Leaf.Key + 1);
			}
			Text = FString::Join(Lines, TEXT("\n"));
			break;
		}
		case 9:
		{
			const int32 Equals = Value - 2;
			if (Vocabulary.Attributes.IsEmpty() || !AttributeAt(Text, Value, Begin, End)) { break; }
			const FString Name = Vocabulary.Attributes[Random.RandRange(0, Vocabulary.Attributes.Num() - 1)];
			OutWhat = FString::Printf(TEXT("attribute %s -> %s"), *Text.Mid(Begin + 1, Equals - Begin - 1), *Name);
			Text = Text.Left(Begin + 1) + Name + Text.Mid(Equals);
			break;
		}
		default:
		{
			const int32 At = Random.RandRange(0, Text.Len() - 1);
			const TCHAR Byte = static_cast<TCHAR>(Random.RandRange(1, 127));
			OutWhat = FString::Printf(TEXT("char %d -> %d"), At, static_cast<int32>(Byte));
			Text[At] = Byte;
			break;
		}
		}
		return Text;
	}

	int32 LineCount(const FString& Text)
	{
		int32 Count = 1;
		for (const TCHAR Char : Text) { Count += Char == TEXT('\n') ? 1 : 0; }
		return Count;
	}

	/** Empty when the result keeps every rule; otherwise which one it broke. */
	FString Broken(const FString& Text, const FADSKCompileResult& Result)
	{
		const bool bHasError = Result.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D) { return D.Severity == EADSKSeverity::Error; });
		if (bHasError == Result.Definition.IsValid())
		{
			return bHasError ? TEXT("a definition despite an error") : TEXT("no definition and no error");
		}
		const int32 Lines = LineCount(Text);
		for (const FADSKDiagnostic& Diagnostic : Result.Diagnostics)
		{
			if (Diagnostic.Line < 0 || Diagnostic.Line > Lines + 1)
			{
				return FString::Printf(TEXT("diagnostic on line %d of a %d-line file: %s"), Diagnostic.Line, Lines, *Diagnostic.Message);
			}
		}
		return FString();
	}
}

using namespace FFuzzSpecPrivate;

TEST_CLASS_WITH_FLAGS(FFuzzSpec, "AdamskAI.Fuzz.Compiler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(MutantsCompileToADefinitionOrToErrorsAndRun)
	{
		const int32 PerFile = FMath::Max(1, EnvInt(TEXT("ADSK_FUZZ_MUTANTS"), DefaultMutantsPerFile));
		const int32 SecondsSet = EnvInt(TEXT("ADSK_FUZZ_SECONDS"), 0);
		const double BudgetSeconds = SecondsSet > 0 ? SecondsSet : DefaultBudgetSeconds;
		const bool bSoak = SecondsSet > 0;
		const int32 SeedBase = BaseSeed + EnvInt(TEXT("ADSK_FUZZ_SEED"), 0);
		const TSharedRef<const FADSKNodeRegistry> Catalogue = FADSKScriptedCatalogue::Make(*ADSKTest::PluginCatalogue());
		const FVocabulary Vocabulary = MakeVocabulary(*Catalogue);
		const TArray<FString> Files = Corpus();
		ASSERT_THAT(IsTrue(Files.Num() > 10, TEXT("the fuzz corpus (Examples, Fixtures/valid) is missing")));
		IFileManager::Get().MakeDirectory(*FuzzDirectory(), true);
		const FString Current = FuzzDirectory() / TEXT("current.xml");

		const double Start = FPlatformTime::Seconds();
		int32 Mutants = 0, Compiled = 0, Failures = 0, Rounds = 0;
		bool bSpent = false;
		for (int32 Round = 0; !bSpent && (Round == 0 || bSoak); ++Round)
		{
			++Rounds;
			for (int32 FileIndex = 0; FileIndex < Files.Num() && !bSpent; ++FileIndex)
			{
				FString Original;
				ASSERT_THAT(IsTrue(FFileHelper::LoadFileToString(Original, *Files[FileIndex]), *Files[FileIndex]));
				Original.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
				// What a live reload does: the unmutated file runs, and its snapshot starts every mutant that compiles.
				FADSKSnapshot Before;
				const FADSKCompileResult Unmutated = FADSKCompiler::Compile(TEXT("original.xml"), Original, Catalogue);
				if (Unmutated.Definition)
				{
					FADSKInstance Running(Unmutated.Definition.ToSharedRef());
					Running.SetRandomSeed(SeedBase + FileIndex);
					if (Running.Start().IsSuccess())
					{
						for (int32 Tick = 0; Tick < 8; ++Tick) { Running.Tick(0.25f); }
						Running.CaptureSnapshot(Before);
					}
				}
				for (int32 Index = 0; Index < PerFile; ++Index)
				{
					if (FPlatformTime::Seconds() - Start > BudgetSeconds)
					{
						// A soak is meant to run out of time; a default run that does has not covered its round.
						if (!bSoak) { TestRunner->AddWarning(FString::Printf(TEXT("stopped at the %.0f s budget after %d mutants"), BudgetSeconds, Mutants)); }
						bSpent = true;
						break;
					}
					const int32 Seed = SeedBase + (Round * Files.Num() + FileIndex) * PerFile + Index;
					FRandomStream Random(Seed);
					FString What;
					FString Text = Mutate(Original, Vocabulary, Random, What);
					// A second mutation now and then reaches what one change alone cannot.
					if (Random.FRand() < 0.3f) { FString More; Text = Mutate(Text, Vocabulary, Random, More); What += TEXT("; ") + More; }
					FFileHelper::SaveStringToFile(Text, *Current, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
					++Mutants;

					const FADSKCompileResult Result = FADSKCompiler::Compile(TEXT("mutant.xml"), Text, Catalogue);
					FString Why = Broken(Text, Result);
					if (Why.IsEmpty() && Result.Definition)
					{
						++Compiled;
						// The static analyses walk the definition too; they must not trip on anything that compiled.
						ADSK::FindHoldRisks(*Result.Definition);
						ADSK::FindLintNotes(*Result.Definition);
						FADSKInstance Instance(Result.Definition.ToSharedRef(), FADSKRecorder::Create(64));
						Instance.SetRandomSeed(Seed);
						// Missing required inputs refuse the start; that is a report, not a failure.
						if (Instance.Start().IsSuccess())
						{
							for (int32 Tick = 0; Tick < 8; ++Tick) { Instance.Tick(0.25f); }
							// A snapshot of itself restarts a second instance of the same definition.
							FADSKSnapshot Own;
							if (Instance.CaptureSnapshot(Own))
							{
								FADSKInstance Again(Result.Definition.ToSharedRef());
								if (Again.Start(Own).IsSuccess()) { for (int32 Tick = 0; Tick < 4; ++Tick) { Again.Tick(0.25f); } }
							}
							Instance.Stop();
						}
						if (!Before.StatePath.IsEmpty())
						{
							FADSKInstance Reloaded(Result.Definition.ToSharedRef(), FADSKRecorder::Create(64));
							if (Reloaded.Start(Before).IsSuccess()) { for (int32 Tick = 0; Tick < 4; ++Tick) { Reloaded.Tick(0.25f); } }
						}
					}
					if (!Why.IsEmpty())
					{
						++Failures;
						const FString Kept = FuzzDirectory() / FString::Printf(TEXT("%s_%d.xml"), *FPaths::GetBaseFilename(Files[FileIndex]), Seed);
						FFileHelper::SaveStringToFile(Text, *Kept, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
						TestRunner->AddError(FString::Printf(TEXT("%s, seed %d (%s): %s; kept as %s"), *FPaths::GetCleanFilename(Files[FileIndex]), Seed, *What, *Why, *Kept));
					}
				}
			}
		}
		IFileManager::Get().Delete(*Current);
		TestRunner->AddInfo(FString::Printf(TEXT("%d mutants in %d round(s), %d compiled and ran, %d broke a rule, %.1f s"), Mutants, Rounds, Compiled, Failures, FPlatformTime::Seconds() - Start));
		ASSERT_THAT(AreEqual(0, Failures));
	}
};

#endif
