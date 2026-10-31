// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Fixtures/ADSKFixtures.h"

#include "Program/ADSKDefinition.h"
#include "Compiler/ADSKCompiler.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace ADSKFixturesPrivate
{
	FString RowText(const FADSKFixtureRow& Row)
	{
		const FString Node = Row.NodeId.IsNone() ? FString() : TEXT(" ") + Row.NodeId.ToString();
		return FString::Printf(TEXT("%d %s %s%s"), Row.Line, LexToString(Row.Severity), LexToString(Row.Code), *Node);
	}

	FString RowsText(const TArray<FADSKFixtureRow>& Rows)
	{
		TArray<FString> Parts;
		for (const FADSKFixtureRow& Row : Rows) { Parts.Add(RowText(Row)); }
		return Parts.IsEmpty() ? FString(TEXT("none")) : FString::Join(Parts, TEXT("; "));
	}

	TArray<FADSKFixtureRow> ActualRows(const FADSKCompileResult& Result)
	{
		TArray<FADSKFixtureRow> Rows;
		for (const FADSKDiagnostic& Diagnostic : Result.Diagnostics)
		{
			Rows.Add({Diagnostic.Line, Diagnostic.Severity, Diagnostic.Code, Diagnostic.NodeId});
		}
		return Rows;
	}

	bool SameRow(const FADSKFixtureRow& A, const FADSKFixtureRow& B)
	{
		return A.Line == B.Line && A.Severity == B.Severity && A.Code == B.Code && A.NodeId.IsEqual(B.NodeId, ENameCase::CaseSensitive);
	}

	/** Pads with trailing spaces so the UTF-8 encoding is MaxSourceBytes + 1 long. */
	bool PadToLimit(FString& Text)
	{
		const int32 Have = FTCHARToUTF8(*Text).Length();
		if (Have > FADSKCompiler::MaxSourceBytes + 1) { return false; }
		Text += FString::ChrN(FADSKCompiler::MaxSourceBytes + 1 - Have, TEXT(' '));
		return true;
	}

	TArray<FString> Collect(const FString& Dir, const FString& Prefix, FADSKFixtureReport& Report)
	{
		TArray<FString> Files, Relative;
		IFileManager::Get().FindFilesRecursive(Files, *Dir, TEXT("*"), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			FString Rel = File;
			FPaths::MakePathRelativeTo(Rel, *(FPaths::ConvertRelativePathToFull(Dir) / TEXT("")));
			Rel = Prefix + Rel;
			if (!Rel.EndsWith(TEXT(".xml")) && !Rel.EndsWith(TEXT(".expect")))
			{
				Report.Failures.Add({Rel, TEXT("unexpected file; fixtures are only .xml and .expect")});
				continue;
			}
			Relative.Add(Rel);
		}
		return Relative;
	}

	void CheckBasenames(const TArray<FString>& Xml, FADSKFixtureReport& Report)
	{
		TMap<FString, FString> Seen;
		for (const FString& Path : Xml)
		{
			const FString Base = FPaths::GetBaseFilename(Path);
			if (const FString* Prior = Seen.Find(Base))
			{
				Report.Failures.Add({Path, FString::Printf(TEXT("duplicate fixture basename '%s' (also %s)"), *Base, **Prior)});
				continue;
			}
			Seen.Add(Base, Path);
		}
	}

	void RunValid(const FString& Root, const FString& Path, const TSharedRef<const FADSKNodeRegistry>& Registry, FADSKFixtureReport& Report)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(Root / Path))) { Report.Failures.Add({Path, TEXT("cannot read fixture")}); return; }
		++Report.ValidCount;
		const FADSKCompileResult Result = FADSKCompiler::Compile(Path, Text, Registry);
		if (!Result.Diagnostics.IsEmpty() || !Result.IsSuccess())
		{
			Report.Failures.Add({Path, TEXT("valid fixture produced diagnostics: ") + RowsText(ActualRows(Result))});
		}
	}

	void RunInvalid(const FString& Root, const FString& Path, const TSharedRef<const FADSKNodeRegistry>& Registry, FADSKFixtureReport& Report)
	{
		const FString ExpectPath = FPaths::ChangeExtension(Path, TEXT("expect"));
		FString Text, ExpectText;
		if (!FFileHelper::LoadFileToString(ExpectText, *(Root / ExpectPath))) { Report.Failures.Add({Path, TEXT("missing .expect file")}); return; }
		if (!FFileHelper::LoadFileToString(Text, *(Root / Path))) { Report.Failures.Add({Path, TEXT("cannot read fixture")}); return; }
		TArray<FADSKFixtureRow> Expected;
		bool bPad = false;
		FString Error;
		if (!FADSKFixtures::ParseExpect(ExpectText, Expected, bPad, Error)) { Report.Failures.Add({ExpectPath, Error}); return; }
		if (bPad && !PadToLimit(Text)) { Report.Failures.Add({Path, TEXT("pad-to-limit fixture already exceeds the source limit")}); return; }
		++Report.InvalidCount;
		const TArray<FADSKFixtureRow> Actual = ActualRows(FADSKCompiler::Compile(Path, Text, Registry));
		bool bSame = Actual.Num() == Expected.Num();
		for (int32 Index = 0; bSame && Index < Actual.Num(); ++Index) { bSame = SameRow(Actual[Index], Expected[Index]); }
		if (!bSame)
		{
			Report.Failures.Add({Path, FString::Printf(TEXT("expected [%s] got [%s]"), *RowsText(Expected), *RowsText(Actual))});
			return;
		}
		for (const FADSKFixtureRow& Row : Expected) { Report.CoveredCodes.Add(Row.Code); }
	}
}

TArray<EADSKDiagnosticCode> FADSKFixtures::AllCodes()
{
	TArray<EADSKDiagnosticCode> Codes;
	for (int32 Value = 0; Value < 256; ++Value)
	{
		const EADSKDiagnosticCode Code = static_cast<EADSKDiagnosticCode>(Value);
		if (FCString::Strcmp(LexToString(Code), TEXT("unknown")) != 0) { Codes.Add(Code); }
	}
	return Codes;
}

bool FADSKFixtures::CodeFromString(const FString& Name, EADSKDiagnosticCode& OutCode)
{
	for (const EADSKDiagnosticCode Code : AllCodes())
	{
		if (Name.Equals(LexToString(Code), ESearchCase::CaseSensitive)) { OutCode = Code; return true; }
	}
	return false;
}

bool FADSKFixtures::ParseExpect(const FString& Text, TArray<FADSKFixtureRow>& OutRows, bool& bOutPadToLimit, FString& OutError)
{
	OutRows.Reset();
	bOutPadToLimit = false;
	TArray<FString> Lines;
	Text.Replace(TEXT("\r\n"), TEXT("\n")).ParseIntoArray(Lines, TEXT("\n"), false);
	if (!Lines.IsEmpty() && Lines.Last().IsEmpty()) { Lines.Pop(); }
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FString& Line = Lines[Index];
		if (Line.StartsWith(TEXT("#")))
		{
			if (Index != 0 || Line != PadToLimitDirective) { OutError = FString::Printf(TEXT("line %d: only a first-line '%s' directive is allowed"), Index + 1, PadToLimitDirective); return false; }
			bOutPadToLimit = true;
			continue;
		}
		TArray<FString> Tokens;
		Line.ParseIntoArrayWS(Tokens);
		FADSKFixtureRow Row;
		if (Tokens.Num() < 3 || Tokens.Num() > 4 || !Tokens[0].IsNumeric() || Tokens[0].Contains(TEXT("-")) || Tokens[0].Contains(TEXT(".")))
		{
			OutError = FString::Printf(TEXT("line %d: expected 'line severity code [node-id]'"), Index + 1);
			return false;
		}
		Row.Line = FCString::Atoi(*Tokens[0]);
		if (Tokens[1] == TEXT("error")) { Row.Severity = EADSKSeverity::Error; }
		else if (Tokens[1] == TEXT("warning")) { Row.Severity = EADSKSeverity::Warning; }
		else { OutError = FString::Printf(TEXT("line %d: severity must be error or warning"), Index + 1); return false; }
		if (!CodeFromString(Tokens[2], Row.Code)) { OutError = FString::Printf(TEXT("line %d: unknown diagnostic code '%s'"), Index + 1, *Tokens[2]); return false; }
		if (Tokens.Num() == 4) { Row.NodeId = FName(*Tokens[3]); }
		OutRows.Add(Row);
	}
	if (OutRows.IsEmpty()) { OutError = TEXT("expectation has no rows"); return false; }
	return true;
}

FADSKFixtureReport FADSKFixtures::Run(const FString& Root, const TSharedRef<const FADSKNodeRegistry>& Registry)
{
	FADSKFixtureReport Report;
	if (!IFileManager::Get().DirectoryExists(*Root)) { Report.Failures.Add({TEXT("."), TEXT("fixtures root is missing")}); return Report; }
	TArray<FString> Valid = ADSKFixturesPrivate::Collect(Root / TEXT("valid"), TEXT("valid/"), Report);
	TArray<FString> Invalid = ADSKFixturesPrivate::Collect(Root / TEXT("invalid"), TEXT("invalid/"), Report);
	const auto IsXml = [](const FString& Path) { return Path.EndsWith(TEXT(".xml")); };
	TArray<FString> ValidXml = Valid.FilterByPredicate(IsXml), InvalidXml = Invalid.FilterByPredicate(IsXml);
	for (const FString& Path : Valid)
	{
		if (!IsXml(Path)) { Report.Failures.Add({Path, TEXT("orphan .expect: valid fixtures have no .expect file")}); }
	}
	for (const FString& Path : Invalid)
	{
		if (!IsXml(Path) && !InvalidXml.Contains(FPaths::ChangeExtension(Path, TEXT("xml")))) { Report.Failures.Add({Path, TEXT("orphan .expect without a matching .xml")}); }
	}
	TArray<FString> AllXml(ValidXml);
	AllXml.Append(InvalidXml);
	ADSKFixturesPrivate::CheckBasenames(AllXml, Report);
	for (const FString& Path : ValidXml) { ADSKFixturesPrivate::RunValid(Root, Path, Registry, Report); }
	for (const FString& Path : InvalidXml) { ADSKFixturesPrivate::RunInvalid(Root, Path, Registry, Report); }
	return Report;
}
