// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Support/TempTree.h"
#include "Support/TestChecks.h"
#include "Trace/ADSKTrace.h"

namespace ADSKTest
{
	/** Up to MaxShown differing lines, numbered from 1; empty when the texts match line for line. */
	inline FString LineDifferences(const FString& Expected, const FString& Actual, int32 MaxShown = 20)
	{
		TArray<FString> ExpectedLines, ActualLines;
		Expected.ParseIntoArrayLines(ExpectedLines, false);
		Actual.ParseIntoArrayLines(ActualLines, false);
		const int32 Count = FMath::Max(ExpectedLines.Num(), ActualLines.Num());
		FString Text;
		int32 Found = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FString E = ExpectedLines.IsValidIndex(Index) ? ExpectedLines[Index] : TEXT("<end of file>");
			const FString A = ActualLines.IsValidIndex(Index) ? ActualLines[Index] : TEXT("<end of file>");
			if (!E.Equals(A, ESearchCase::CaseSensitive) && ++Found <= MaxShown)
			{
				Text += FString::Printf(TEXT("\n  line %d: expected [%s] actual [%s]"), Index + 1, *E, *A);
			}
		}
		if (Found > MaxShown)
		{
			Text += FString::Printf(TEXT("\n  ... and %d more differing lines"), Found - MaxShown);
		}
		return Text;
	}

	/** Reads a text file with its line endings normalized to '\n'; empty when it is missing. */
	inline FString ReadNormalized(const FString& Path)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		return Text.Replace(TEXT("\r\n"), TEXT("\n"));
	}

	/** A golden file's `# key value` header lines, and its body without them. */
	inline void SplitGolden(const FString& Text, TMap<FString, FString>& OutHeader, FString& OutBody)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);
		for (const FString& Line : Lines)
		{
			FString Key, Value;
			if (Line.StartsWith(TEXT("# ")) && Line.Mid(2).Split(TEXT(" "), &Key, &Value)) { OutHeader.Add(Key, Value); }
			else if (!Line.IsEmpty()) { OutBody += Line + TEXT("\n"); }
		}
	}

	/** Records against Fixtures/golden/<Name>.trace; ADSK_CAPTURE_GOLDEN=1 rewrites it. Only the body is compared. */
	inline FADSKCheck MatchesGolden(const TCHAR* Name, const FADSKRecorder& Recorder, const FADSKDefinition& Definition)
	{
		const FString Source = Definition.GetSourceHash().ToString().ToLower();
		const FString Catalogue = Definition.GetCatalogueFingerprintHex();
		FString Trace;
		for (const FADSKRecord& Record : Recorder.Snapshot())
		{
			Trace += ADSK::FormatRecord(Definition, Record) + TEXT("\n");
		}
		const FString Path = FADSKTestPaths::PluginBase() / TEXT("Fixtures/golden") / FString(Name) + TEXT(".trace");
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("ADSK_CAPTURE_GOLDEN")) == TEXT("1"))
		{
			const FString Header = FString::Printf(TEXT("# source %s\n# catalogue %s\n"), *Source, *Catalogue);
			return FFileHelper::SaveStringToFile(Header + Trace, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
				? FADSKCheck::Pass() : FADSKCheck::Fail(TEXT("cannot write ") + Path);
		}
		if (!FPaths::FileExists(Path))
		{
			return FADSKCheck::Fail(FString::Printf(TEXT("%s is missing; capture it with ADSK_CAPTURE_GOLDEN=1"), *Path));
		}
		TMap<FString, FString> Header;
		FString Expected;
		SplitGolden(ReadNormalized(Path), Header, Expected);
		const FString Differences = LineDifferences(Expected, Trace);
		if (Differences.IsEmpty())
		{
			return FADSKCheck::Pass();
		}
		TArray<FString> Moved;
		if (Header.FindRef(TEXT("source")) != Source) { Moved.Add(TEXT("the behavior file")); }
		if (Header.FindRef(TEXT("catalogue")) != Catalogue) { Moved.Add(TEXT("the node catalogue")); }
		const FString Cause = Moved.IsEmpty() ? FString(TEXT("same file and catalogue as the capture, so the runtime behaves differently"))
			: FString::Join(Moved, TEXT(" and ")) + TEXT(" changed since the capture");
		return FADSKCheck::Fail(FString::Printf(TEXT("the trace differs from %s (%s; recapture with ADSK_CAPTURE_GOLDEN=1 if intended):%s"), *Path, *Cause, *Differences));
	}
}
