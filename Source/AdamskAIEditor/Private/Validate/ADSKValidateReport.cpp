// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Validate/ADSKValidateReport.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Program/ADSKDefinition.h"
#include "Program/ADSKHoldAnalysis.h"
#include "Program/ADSKLint.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

FString FADSKValidateReport::FormatRow(const FADSKReportRow& Row)
{
	const FString Node = Row.Node.IsEmpty() ? FString() : FString::Printf(TEXT(" [%s]"), *Row.Node);
	return FString::Printf(TEXT("%s:%d: %s %s%s: %s"), *Row.Path, Row.Line, *Row.Severity, *Row.Code, *Node, *Row.Message);
}

FADSKReportRow FADSKValidateReport::MakeRow(const FString& Path, const FString& Severity, const FString& Code, const FString& Message, int32 Line, const FString& Node)
{
	FADSKReportRow Row;
	Row.Path = Path;
	Row.Line = Line;
	Row.Severity = Severity;
	Row.Code = Code;
	Row.Node = Node;
	Row.Message = Message;
	return Row;
}

void FADSKValidateReport::AppendResult(const FADSKSourceReadResult& Result, const FString& DisplayBase, TArray<FADSKReportRow>& OutRows)
{
	for (const FADSKDiagnostic& Diagnostic : Result.Result.Diagnostics)
	{
		const FString Path = Diagnostic.Source.IsEmpty() ? DisplayBase : DisplayBase / Diagnostic.Source;
		const FString Code = Result.ReportCode.IsEmpty() ? FString(LexToString(Diagnostic.Code)) : Result.ReportCode;
		OutRows.Add(MakeRow(Path, LexToString(Diagnostic.Severity), Code, Diagnostic.Message, Diagnostic.Line,
			Diagnostic.NodeId.IsNone() ? FString() : Diagnostic.NodeId.ToString()));
	}
}

void FADSKValidateReport::AppendNotes(const FADSKSourceReadResult& Result, const FString& DisplayBase, TArray<FADSKReportRow>& OutRows)
{
	if (!Result.Result.Definition.IsValid())
	{
		return;
	}
	const FADSKDefinition& Definition = *Result.Result.Definition;
	for (const FADSKHoldRisk& Risk : ADSK::FindHoldRisks(Definition))
	{
		OutRows.Add(MakeRow(DisplayBase / Definition.GetSourceName(), TEXT("note"), TEXT("hold-risk"), ADSK::DescribeHoldRisk(Definition, Risk), Risk.Line));
	}
	for (const FADSKLintNote& Note : ADSK::FindLintNotes(Definition))
	{
		OutRows.Add(MakeRow(DisplayBase / Definition.GetSourceName(), TEXT("note"), Note.Code, Note.Message, Note.Line));
	}
}

int32 FADSKValidateReport::ExitCodeFor(const TArray<FADSKReportRow>& Rows)
{
	return Rows.ContainsByPredicate([](const FADSKReportRow& Row) { return !Row.IsNote(); }) ? 1 : 0;
}

FString FADSKValidateReport::ToJson(const TArray<FADSKReportRow>& Rows, const FString& Mode, int32 Files, int32 ExitCode)
{
	int32 Errors = 0, Notes = 0;
	for (const FADSKReportRow& Row : Rows) { Errors += Row.IsError() ? 1 : 0; Notes += Row.IsNote() ? 1 : 0; }
	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("version"), JsonVersion);
	Writer->WriteValue(TEXT("mode"), Mode);
	Writer->WriteValue(TEXT("exit"), ExitCode);
	Writer->WriteValue(TEXT("files"), Files);
	Writer->WriteValue(TEXT("errors"), Errors);
	Writer->WriteValue(TEXT("warnings"), Rows.Num() - Errors - Notes);
	Writer->WriteValue(TEXT("notes"), Notes);
	Writer->WriteArrayStart(TEXT("diagnostics"));
	for (const FADSKReportRow& Row : Rows)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("path"), Row.Path);
		Writer->WriteValue(TEXT("line"), Row.Line);
		Writer->WriteValue(TEXT("severity"), Row.Severity);
		Writer->WriteValue(TEXT("code"), Row.Code);
		Writer->WriteValue(TEXT("node"), Row.Node);
		Writer->WriteValue(TEXT("message"), Row.Message);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return Output;
}

bool FADSKValidateReport::WriteAtomic(const FString& FinalPath, const FString& Content, const FADSKAtomicWriteHooks& Hooks, FString& OutError)
{
	IFileManager& Files = IFileManager::Get();
	if (!Files.MakeDirectory(*FPaths::GetPath(FinalPath), true))
	{
		OutError = FString::Printf(TEXT("cannot create directory for %s"), *FinalPath);
		return false;
	}
	const FString Temp = FinalPath + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
	const bool bSaved = Hooks.Save ? Hooks.Save(Temp, Content)
								   : FFileHelper::SaveStringToFile(Content, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (!bSaved)
	{
		Files.Delete(*Temp, false, true, true);
		OutError = FString::Printf(TEXT("cannot write temp file for %s"), *FinalPath);
		return false;
	}
	const bool bReplaced = Hooks.Replace ? Hooks.Replace(Temp, FinalPath) : Files.Move(*FinalPath, *Temp, true, true, false, true);
	if (!bReplaced)
	{
		Files.Delete(*Temp, false, true, true);
		OutError = FString::Printf(TEXT("cannot replace %s"), *FinalPath);
		return false;
	}
	return true;
}
