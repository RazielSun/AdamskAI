// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Debug/ADSKDebugSnapshot.h"

// Tooling only: Shipping keeps the declarations but not the code (see the header).
#if !UE_BUILD_SHIPPING

#include "Brain/ADSKBrainComponent.h"
#include "Debug/ADSKDebugActions.h"
#include "Objects/ADSKObjectHandles.h"
#include "Program/ADSKDefinition.h"
#include "Trace/ADSKTrace.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "AIController.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

namespace ADSKDebugSnapshotPrivate
{
	/** Object values name what they resolve to, which the core cannot. */
	FString ShowValue(const FADSKDefinition& Definition, const FADSKValue& Value)
	{
		return Value.Type == EADSKValueType::Object ? FADSKObjectHandles::Describe(Value) : ADSK::FormatValue(Definition, Value);
	}

	FString FormatRecord(const FADSKDefinition& Definition, const FADSKRecord& Record)
	{
		return ADSK::FormatRecord(Definition, Record, [&Definition](const FADSKValue& Value) { return ShowValue(Definition, Value); });
	}

	void FillSlots(FADSKBrainDebugRow& Row, const UADSKBrainComponent& Brain, const FADSKInstance& Instance,
		const FADSKDefinition& Definition)
	{
		for (const FADSKSlotDefinition& Slot : Definition.GetSlots())
		{
			FADSKValue Value;
			if (Instance.GetValue(Slot.Name, Value))
			{
				(Slot.bIsInput ? Row.Inputs : Row.Locals).Add({Slot.Name, LexToString(Slot.Type), ShowValue(Definition, Value)});
			}
		}
		for (const TPair<FString, FADSKValue>& Cached : Brain.GetCachedInputs())
		{
			Row.CachedInputs.Add({Cached.Key, LexToString(Cached.Value.Type), ShowValue(Definition, Cached.Value)});
		}
		Row.CachedInputs.Sort([](const FADSKDebugNamedValue& A, const FADSKDebugNamedValue& B) { return A.Name < B.Name; });
	}

	void FillEvents(FADSKBrainDebugRow& Row, const FADSKDefinition& Definition)
	{
		// The events a debug action may raise: change events, like completions, come from the core.
		for (const FADSKEventDefinition& Event : Definition.GetEvents())
		{
			if (Event.IsRaisedByCore())
			{
				continue;
			}
			FADSKDebugEventSchema& Schema = Row.Events.AddDefaulted_GetRef();
			Schema.Name = Event.Name;
			Schema.Priority = Event.Priority;
			for (int32 Index = 0; Index < Event.ParamCount; ++Index)
			{
				const FADSKEventParamDefinition& Param = Definition.GetEventParams()[Event.FirstParam + Index];
				Schema.Params.Add({Param.Name, LexToString(Param.Type), FString()});
			}
		}
	}

	void FillRecords(FADSKBrainDebugRow& Row, const UADSKBrainComponent& Brain, const FADSKDefinition& Definition)
	{
		const TSharedPtr<FADSKRecorder>& Recorder = Brain.GetRecorder();
		if (!Recorder)
		{
			return;
		}
		for (const FADSKRecord& Record : Recorder->Tail(FADSKDebugSnapshot::RecordTail))
		{
			Row.RecentRecords.Add(FormatRecord(Definition, Record));
		}
	}

	void FillAsyncOps(FADSKBrainDebugRow& Row, const UADSKBrainComponent& Brain)
	{
		const UWorld* World = Brain.GetWorld();
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		for (const FADSKAsyncOp& Op : Brain.GetAsyncOps().GetOps())
		{
			FADSKDebugAsyncOp& Entry = Row.PendingOps.AddDefaulted_GetRef();
			Entry.Kind = Op.Kind;
			Entry.NodeId = Op.NodeId;
			Entry.Handle = FString::Printf(TEXT("%u.%u"), Op.Handle.Generation, Op.Handle.ActivationId);
			Entry.AgeSeconds = static_cast<float>(FMath::Max(0.0, Now - Op.StartTime));
			if (Op.MoveRequest.IsValid()) { Entry.MoveRequest = Op.MoveRequest.ToString(); }
		}
		Row.StaleAsyncCompletions = Brain.GetStaleAsyncCompletions();
	}

	FADSKBrainDebugRow MakeRow(const UADSKWorldSubsystem* Subsystem, UADSKBrainComponent& Brain, EADSKDebugDetail Detail = EADSKDebugDetail::Full)
	{
		FADSKBrainDebugRow Row;
		Row.BrainPath = Brain.GetPathName();
		Row.OwnerName = ADSKDebugActions::OwnerName(Brain);
		const AAIController* Controller = Brain.GetAIController();
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		Row.PawnName = ADSKDebugActions::PawnName(Brain);
		if (Pawn)
		{
			Row.bHasLabelLocation = true;
			Row.LabelLocation = Pawn->GetActorLocation() + FVector(0, 0, Pawn->GetSimpleCollisionHalfHeight() + 20.0);
		}
		Row.SourcePath = Brain.GetSourcePath();
		Row.Diagnostics = Subsystem ? Subsystem->GetLastDiagnostics(Row.SourcePath) : FString();
		Row.LastError = Brain.GetLastError();
		Row.bHostRunning = Brain.IsADSKRunning();
		const FADSKInstance* Instance = Brain.GetInstance();
		if (!Instance)
		{
			return Row;
		}

		const FADSKDefinition& Definition = *Brain.GetDefinition();
		Row.CompositeHash = Definition.GetCompositeHashHex();
		Row.ExecState = !Instance->IsRunning() ? EADSKDebugExecState::Stopped
			: Instance->IsHolding() ? EADSKDebugExecState::Holding : EADSKDebugExecState::Running;
		Row.Generation = Instance->GetGeneration();
		Row.TickCount = Instance->GetTickCount();
		Row.ActiveStatePath = Instance->GetActiveStatePath();
		Row.ActiveTags.Append(Instance->GetActiveTags().GetData(), Instance->GetActiveTags().Num());
		Row.ActiveNodeId = Instance->GetActiveNodeId();
		TArray<FADSKActivationHandle> LeafHandles;
		Instance->GetActiveNodes(Row.BackgroundNodeIds, LeafHandles);
		if (!Row.BackgroundNodeIds.IsEmpty())
		{
			Row.BackgroundNodeIds.RemoveAt(0);
		}
		if (Detail == EADSKDebugDetail::Labels)
		{
			return Row;
		}
		Instance->GetQueuedEvents(Row.QueuedEvents);

		FillSlots(Row, Brain, *Instance, Definition);
		FillEvents(Row, Definition);

		FillAsyncOps(Row, Brain);

		FillRecords(Row, Brain, Definition);
		return Row;
	}

	const TCHAR* ExecText(EADSKDebugExecState State)
	{
		switch (State)
		{
		case EADSKDebugExecState::Running: return TEXT("running");
		case EADSKDebugExecState::Holding: return TEXT("holding");
		default: return TEXT("stopped");
		}
	}
}

FADSKDebugSnapshot FADSKDebugSnapshot::Collect(const UWorld* World, EADSKDebugDetail Detail)
{
	check(IsInGameThread());
	FADSKDebugSnapshot Snapshot;
	const UADSKWorldSubsystem* Subsystem = World ? World->GetSubsystem<UADSKWorldSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return Snapshot;
	}
	for (UADSKBrainComponent* Brain : Subsystem->GetWorldBrains())
	{
		if (Brain)
		{
			Snapshot.Rows.Add(ADSKDebugSnapshotPrivate::MakeRow(Subsystem, *Brain, Detail));
		}
	}
	return Snapshot;
}

FADSKBrainDebugRow FADSKDebugSnapshot::Describe(const UADSKBrainComponent& Brain)
{
	check(IsInGameThread());
	const UWorld* World = Brain.GetWorld();
	// Read-only: the row builder takes the brain mutably only for its non-const accessors.
	return ADSKDebugSnapshotPrivate::MakeRow(World ? World->GetSubsystem<UADSKWorldSubsystem>() : nullptr, const_cast<UADSKBrainComponent&>(Brain));
}

FString FADSKDebugSnapshot::FormatLegacyLine(const FADSKBrainDebugRow& Row)
{
	return FString::Printf(TEXT("adsk: brain=%s pawn=%s source=%s running=%s state=%s ticks=%llu hash=%s error=%s diagnostics=%s"),
		*Row.OwnerName, *Row.PawnName, *Row.SourcePath, Row.bHostRunning ? TEXT("true") : TEXT("false"),
		*FString::Join(Row.ActiveStatePath, TEXT("/")), Row.TickCount, *Row.CompositeHash, *Row.LastError, *Row.Diagnostics);
}

FString FADSKDebugSnapshot::FormatOps(const FADSKBrainDebugRow& Row)
{
	TArray<FString> Parts;
	for (const FADSKDebugAsyncOp& Op : Row.PendingOps)
	{
		Parts.Add(FString::Printf(TEXT("%s@%s(%.1fs)"), *Op.Kind.ToString(), *Op.NodeId.ToString(), Op.AgeSeconds));
	}
	return Parts.IsEmpty() ? FString(TEXT("none")) : FString::Join(Parts, TEXT(","));
}

TArray<FString> FADSKDebugSnapshot::ToLines() const
{
	TArray<FString> Lines;
	for (const FADSKBrainDebugRow& Row : Rows)
	{
		Lines.Append(FormatRow(Row));
	}
	return Lines;
}

TArray<FString> FADSKDebugSnapshot::FormatRow(const FADSKBrainDebugRow& Row)
{
	TArray<FString> Lines;
	{
		Lines.Add(FormatLegacyLine(Row));
		Lines.Add(FString::Printf(TEXT("  exec=%s gen=%u node=%s ops=%s stale=%u queued=[%s]%s"),
			ADSKDebugSnapshotPrivate::ExecText(Row.ExecState), Row.Generation,
			Row.ActiveNodeId.IsNone() ? TEXT("none") : *Row.ActiveNodeId.ToString(), *FormatOps(Row), Row.StaleAsyncCompletions,
			*FString::Join(Row.QueuedEvents, TEXT(",")),
			Row.BackgroundNodeIds.IsEmpty() ? TEXT("") : *(TEXT(" background=[") + FString::JoinBy(Row.BackgroundNodeIds, TEXT(","), [](FName Id) { return Id.ToString(); }) + TEXT("]"))));
		if (!Row.ActiveTags.IsEmpty())
		{
			Lines.Add(TEXT("  tags=") + FString::JoinBy(Row.ActiveTags, TEXT(","), [](FName Tag) { return Tag.ToString(); }));
		}
		for (const FADSKDebugNamedValue& Input : Row.Inputs)
		{
			Lines.Add(FString::Printf(TEXT("  input %s:%s=%s"), *Input.Name, *Input.Type, *Input.Value));
		}
		for (const FADSKDebugNamedValue& Local : Row.Locals)
		{
			Lines.Add(FString::Printf(TEXT("  local %s:%s=%s"), *Local.Name, *Local.Type, *Local.Value));
		}
		for (const FADSKDebugNamedValue& Cached : Row.CachedInputs)
		{
			Lines.Add(FString::Printf(TEXT("  cached %s=%s"), *Cached.Name, *Cached.Value));
		}
		for (const FString& Record : Row.RecentRecords)
		{
			Lines.Add(TEXT("  record ") + Record);
		}
	}
	return Lines;
}

FString FADSKDebugSnapshot::ToText() const
{
	return FString::Join(ToLines(), LINE_TERMINATOR);
}

const UADSKBrainComponent* FADSKDebugSnapshot::FindBrainFor(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (const UADSKBrainComponent* Own = Actor->FindComponentByClass<UADSKBrainComponent>())
	{
		return Own;
	}
	const APawn* Pawn = Cast<APawn>(Actor);
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	return Controller ? Controller->FindComponentByClass<UADSKBrainComponent>() : nullptr;
}

TArray<FString> FADSKDebugSnapshot::GameplayDebuggerLines(const AActor* DebugActor)
{
	const UADSKBrainComponent* Brain = FindBrainFor(DebugActor);
	if (!Brain)
	{
		return {TEXT("{grey}no AdamskAI brain")};
	}
	TArray<FString> Lines = FormatRow(Describe(*Brain));
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		Lines[Index] = (Index == 0 ? TEXT("{green}") : TEXT("{white}")) + Lines[Index].TrimStart();
	}
	return Lines;
}
#endif // !UE_BUILD_SHIPPING
