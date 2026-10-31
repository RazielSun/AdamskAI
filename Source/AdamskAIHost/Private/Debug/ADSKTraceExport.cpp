// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Debug/ADSKTraceExport.h"

// Tooling only: Shipping keeps the declarations but not the code (see the header).
#if !UE_BUILD_SHIPPING

#include "Brain/ADSKBrainComponent.h"
#include "Program/ADSKDefinition.h"
#include "Trace/ADSKTrace.h"
#include "Json.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace ADSKTraceExportPrivate
{
	FString Safe(FString Value)
	{
		for (TCHAR& Character : Value) if (!(FChar::IsAlnum(Character) || Character == TEXT('_') || Character == TEXT('-'))) Character = TEXT('_');
		return Value.IsEmpty() ? TEXT("unnamed") : Value;
	}
	const TCHAR* Status(EADSKNodeStatus Value)
	{
		switch (Value) { case EADSKNodeStatus::Running: return TEXT("running"); case EADSKNodeStatus::Succeeded: return TEXT("succeeded"); case EADSKNodeStatus::Failed: return TEXT("failed"); }
		return TEXT("unknown");
	}
	FString State(const FADSKDefinition& Definition, int32 Index) { return Definition.GetStates().IsValidIndex(Index) ? Definition.GetStates()[Index].Id : FString(); }
	FString Node(const FADSKDefinition& Definition, int32 Index) { return Definition.GetNodes().IsValidIndex(Index) ? Definition.GetNodes()[Index].AuthoredId : FString(); }
	FString Event(const FADSKDefinition& Definition, int32 Index) { return Definition.GetEvents().IsValidIndex(Index) ? Definition.GetEvents()[Index].Name : FString(); }
	FString Slot(const FADSKDefinition& Definition, int32 Index) { return Definition.GetSlots().IsValidIndex(Index) ? Definition.GetSlots()[Index].Name : FString(); }
	void WriteValue(TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>>& Writer, const FADSKDefinition& Definition, const FADSKValue& Value)
	{
		Writer->WriteObjectStart(TEXT("value"));
		FString EnumType, EnumName;
		const bool bNamedEnum = Value.Type == EADSKValueType::Enum && Definition.ResolveEnumValue(Value, EnumType, EnumName);
		Writer->WriteValue(TEXT("type"), bNamedEnum ? EnumType : LexToString(Value.Type));
		switch (Value.Type)
		{
		case EADSKValueType::Bool: Writer->WriteValue(TEXT("value"), Value.bBool); break;
		case EADSKValueType::Float: Writer->WriteValue(TEXT("value"), Value.Float); break;
		case EADSKValueType::Int: Writer->WriteValue(TEXT("value"), Value.Int); break;
		case EADSKValueType::Vector: Writer->WriteArrayStart(TEXT("value")); Writer->WriteValue(Value.Vector.X); Writer->WriteValue(Value.Vector.Y); Writer->WriteValue(Value.Vector.Z); Writer->WriteArrayEnd(); break;
		case EADSKValueType::Enum: Writer->WriteValue(TEXT("value"), bNamedEnum ? EnumName : LexToString(Value)); break;
		case EADSKValueType::Object: Writer->WriteValue(TEXT("value"), LexToString(Value)); break;
		}
		Writer->WriteObjectEnd();
	}
	FString Line(TFunctionRef<void(TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>>&)> Fill)
	{
		FString Json; TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json, 0); Writer->WriteObjectStart(); Fill(Writer); Writer->WriteObjectEnd(); Writer->Close(); return Json + LINE_TERMINATOR;
	}
}

FADSKTraceExportResult FADSKTraceExport::Write(const UADSKBrainComponent& Brain, const FString& WorldName, const FString& Root)
{
	using namespace ADSKTraceExportPrivate;
	FADSKTraceExportResult Out;
	const TSharedPtr<FADSKRecorder>& Recorder = Brain.GetRecorder();
	if (!Recorder || !Brain.GetInstance()) { Out.Error = TEXT("brain-not-initialized"); return Out; }
	const FADSKDefinition& Definition = *Brain.GetDefinition();
	const FString Directory = (Root.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("AdamskAI") : Root) / Safe(WorldName);
	if (!IFileManager::Get().MakeDirectory(*Directory, true)) { Out.Error = TEXT("create-directory"); return Out; }
	const FString Stem = Safe(Brain.GetOwner() ? Brain.GetOwner()->GetName() : TEXT("owner")) + TEXT("_") + Safe(Brain.GetName());
	const FString Final = Directory / (Stem + TEXT(".jsonl"));
	const FString Temp = Final + TEXT(".tmp");
	FString Contents = Line([&](TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>>& Writer)
	{
		Writer->WriteValue(TEXT("version"), 1); Writer->WriteValue(TEXT("kind"), TEXT("header"));
		Writer->WriteValue(TEXT("source"), Brain.GetSourcePath()); Writer->WriteValue(TEXT("compositeHash"), Definition.GetCompositeHashHex());
		Writer->WriteValue(TEXT("sourceHash"), Definition.GetSourceHash().ToString().ToLower()); Writer->WriteValue(TEXT("catalogueHash"), Definition.GetCatalogueFingerprintHex());
		Writer->WriteValue(TEXT("brain"), Brain.GetPathName()); Writer->WriteValue(TEXT("lost"), static_cast<double>(Recorder->GetTotalLost()));
	});
	for (const FADSKRecord& Record : Recorder->Snapshot())
	{
		Contents += Line([&](TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>>& Writer)
		{
			Writer->WriteValue(TEXT("sequence"), static_cast<double>(Record.Sequence)); Writer->WriteValue(TEXT("tick"), static_cast<double>(Record.Tick));
			Writer->WriteValue(TEXT("kind"), LexToString(Record.Kind)); Writer->WriteValue(TEXT("reason"), LexToString(Record.Reason)); Writer->WriteValue(TEXT("status"), Status(Record.Status));
			if (Record.State != INDEX_NONE) Writer->WriteValue(TEXT("state"), State(Definition, Record.State));
			if (Record.Node != INDEX_NONE) Writer->WriteValue(TEXT("node"), Node(Definition, Record.Node));
			if (Record.Event != INDEX_NONE) Writer->WriteValue(TEXT("event"), Event(Definition, Record.Event));
			if (Record.Slot != INDEX_NONE) Writer->WriteValue(TEXT("slot"), Slot(Definition, Record.Slot));
			if (Record.Transition != INDEX_NONE)
			{
				Writer->WriteValue(TEXT("transition"), Record.Transition);
				const int32 Target = Definition.GetTransitions().IsValidIndex(Record.Transition) ? Definition.GetTransitions()[Record.Transition].Target : INDEX_NONE;
				if (Target != INDEX_NONE) Writer->WriteValue(TEXT("target"), State(Definition, Target));
			}
			if (Record.Handle.IsValid()) { Writer->WriteValue(TEXT("generation"), Record.Handle.Generation); Writer->WriteValue(TEXT("activation"), Record.Handle.ActivationId); }
			if (Record.LostCount) Writer->WriteValue(TEXT("lost"), static_cast<double>(Record.LostCount));
			if (Record.bHasValue) WriteValue(Writer, Definition, Record.Value);
		});
	}
	if (!FFileHelper::SaveStringToFile(Contents, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { Out.Error = TEXT("write-temp"); return Out; }
	if (!IFileManager::Get().Move(*Final, *Temp, true, true)) { IFileManager::Get().Delete(*Temp); Out.Error = TEXT("replace-final"); return Out; }
	Out.Path = Final;
	return Out;
}

#endif // !UE_BUILD_SHIPPING
