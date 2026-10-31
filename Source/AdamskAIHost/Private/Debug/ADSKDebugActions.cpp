// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Debug/ADSKDebugActions.h"

// Tooling only: Shipping keeps the declarations but not the code (see the header).
#if !UE_BUILD_SHIPPING

#include "Brain/ADSKBrainComponent.h"
#include "Program/ADSKDefinition.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "AIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace ADSKDebugActions
{
	TArray<UADSKWorldSubsystem*> ResolveWorlds(UWorld* ConsoleWorld)
	{
		TArray<UADSKWorldSubsystem*> Out;
		if (ConsoleWorld && (ConsoleWorld->IsGameWorld() || ConsoleWorld->WorldType == EWorldType::PIE))
		{
			if (UADSKWorldSubsystem* Subsystem = ConsoleWorld->GetSubsystem<UADSKWorldSubsystem>()) Out.Add(Subsystem);
			return Out;
		}
		if (!GEngine) return Out;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Candidate = Context.World();
			if (Candidate && Candidate->WorldType == EWorldType::PIE)
				if (UADSKWorldSubsystem* Subsystem = Candidate->GetSubsystem<UADSKWorldSubsystem>()) Out.Add(Subsystem);
		}
		return Out;
	}

	FString OwnerName(const UADSKBrainComponent& Brain)
	{
		return Brain.GetOwner() ? Brain.GetOwner()->GetName() : Brain.GetName();
	}

	FString PawnName(const UADSKBrainComponent& Brain)
	{
		const AAIController* Controller = Brain.GetAIController();
		return Controller && Controller->GetPawn() ? Controller->GetPawn()->GetName() : FString(TEXT("-"));
	}

	FString ClientWorldHint(const UWorld* World)
	{
		if (!World || World->GetNetMode() != NM_Client) return FString();
		const UADSKWorldSubsystem* Subsystem = World->GetSubsystem<UADSKWorldSubsystem>();
		if (Subsystem && !Subsystem->GetWorldBrains().IsEmpty()) return FString();
		return TEXT("adsk: client world: brains run on the server; use the Gameplay Debugger, or ServerExec adsk.Status (output in the server log)");
	}

	void LogClientWorldHint(TConstArrayView<UADSKWorldSubsystem*> Subsystems, FOutputDevice& Output)
	{
		for (const UADSKWorldSubsystem* Subsystem : Subsystems)
		{
			const FString Hint = Subsystem ? ClientWorldHint(Subsystem->GetWorld()) : FString();
			if (!Hint.IsEmpty()) { Output.Log(*Hint); return; }
		}
	}

	bool ResolveBrains(TConstArrayView<UADSKWorldSubsystem*> Subsystems, FStringView Address,
		TArray<UADSKBrainComponent*>& Out, FOutputDevice& Output)
	{
		Out.Reset();
		const FString Query(Address);
		const bool bAll = Query == TEXT("all");
		for (UADSKWorldSubsystem* Subsystem : Subsystems)
		{
			if (!Subsystem) continue;
			for (UADSKBrainComponent* Brain : Subsystem->GetWorldBrains())
			{
				if (!Brain) continue;
				const bool bMatches = Query.StartsWith(TEXT("/")) ? Brain->GetPathName() == Query
					: Brain->GetName() == Query || OwnerName(*Brain) == Query || PawnName(*Brain) == Query;
				if (bAll || bMatches) Out.Add(Brain);
			}
		}
		if (Out.IsEmpty() || (!bAll && Out.Num() != 1))
		{
			Output.Logf(TEXT("adsk: %s target '%s' (use all, or a brain= / pawn= name from adsk.Status)"),
				Out.IsEmpty() ? TEXT("unknown") : TEXT("ambiguous"), *Query);
			if (Out.IsEmpty()) LogClientWorldHint(Subsystems, Output);
			Out.Reset();
			return false;
		}
		return true;
	}

	bool ParseEvent(const UADSKBrainComponent& Brain, const FString& Event, TConstArrayView<FString> Args,
		TArray<FADSKNamedValue>& Out, FString& Error)
	{
		Out.Reset();
		Error.Reset();
		if (!Brain.GetInstance()) { Error = TEXT("not-running"); return false; }
		const FADSKDefinition& Definition = *Brain.GetDefinition();
		const int32 EventIndex = Definition.FindEvent(Event);
		if (EventIndex == INDEX_NONE || Definition.GetEvents()[EventIndex].IsRaisedByCore()) { Error = TEXT("unknown-or-reserved-event"); return false; }
		const FADSKEventDefinition& EventDefinition = Definition.GetEvents()[EventIndex];
		TSet<FString> Seen;
		for (const FString& Argument : Args)
		{
			FString Name, Text;
			if (!Argument.Split(TEXT("="), &Name, &Text) || Name.IsEmpty() || Seen.Contains(Name)) { Error = TEXT("invalid-or-duplicate-parameter"); return false; }
			int32 Param = INDEX_NONE;
			for (int32 Index = 0; Index < EventDefinition.ParamCount; ++Index)
				if (Definition.GetEventParams()[EventDefinition.FirstParam + Index].Name == Name) { Param = Index; break; }
			if (Param == INDEX_NONE) { Error = TEXT("unknown-parameter"); return false; }
			FADSKValue Value;
			if (!ADSK::TryParseValue(Definition.GetEventParams()[EventDefinition.FirstParam + Param].Type, Text, Value)) { Error = TEXT("wrong-type"); return false; }
			Seen.Add(Name);
			Out.Add({Name, Value});
		}
		if (Seen.Num() != EventDefinition.ParamCount) { Error = TEXT("missing-parameter"); return false; }
		return true;
	}

	void Reload(UWorld* World, FStringView SourcePathOrAll, FOutputDevice& Out)
	{
		const TArray<UADSKWorldSubsystem*> Subsystems = ResolveWorlds(World);
		if (Subsystems.IsEmpty()) { Out.Logf(TEXT("adsk: no Game/PIE world")); return; }
		for (UADSKWorldSubsystem* Subsystem : Subsystems)
		{
			TArray<FString> Paths;
			if (SourcePathOrAll != TEXT("all")) Paths.Add(FString(SourcePathOrAll));
			else for (UADSKBrainComponent* Brain : Subsystem->GetAllBrains()) if (Brain) Paths.AddUnique(Brain->GetSourcePath());
			Paths.Sort();
			for (const FString& Path : Paths)
			{
				FString Detail;
				const EADSKReloadResult Result = Subsystem->RequestReload(Path, Detail);
				// The detail repeats the result for not-loaded; anything else it says is new.
				const bool bDetail = !Detail.IsEmpty() && Detail != LexToString(Result);
				Out.Logf(TEXT("%s: %s%s%s"), *Path, LexToString(Result), bDetail ? TEXT(" ") : TEXT(""), bDetail ? *Detail : TEXT(""));
			}
		}
	}

	void Restart(UWorld* World, FStringView Address, FOutputDevice& Out)
	{
		TArray<UADSKBrainComponent*> Brains;
		if (!ResolveBrains(ResolveWorlds(World), Address, Brains, Out)) return;
		for (UADSKBrainComponent* Brain : Brains)
		{
			FString Error;
			UADSKWorldSubsystem* Subsystem = Brain->GetWorld() ? Brain->GetWorld()->GetSubsystem<UADSKWorldSubsystem>() : nullptr;
			// A brain on a behavior file restarts on what that file publishes now; one given a definition directly keeps it.
			const TSharedPtr<const FADSKDefinition> Published = Subsystem ? Subsystem->GetActiveDefinition(Brain->GetSourcePath()) : nullptr;
			if (!Brain->GetInstance()) Error = TEXT("not-running");
			else if (Published) Brain->ReplaceDefinition(Published.ToSharedRef(), true, Error);
			else Brain->RestartLogic();
			if (Error.IsEmpty() && !Brain->IsADSKRunning()) Error = TEXT("start-failed");
			if (Error.IsEmpty()) Out.Logf(TEXT("adsk: restarted %s"), *OwnerName(*Brain));
			else Out.Logf(TEXT("adsk: %s"), *Error);
		}
	}

	void Event(UWorld* World, FStringView Address, const FString& EventName, TConstArrayView<FString> Args, FOutputDevice& Out)
	{
		TArray<UADSKBrainComponent*> Brains;
		if (!ResolveBrains(ResolveWorlds(World), Address, Brains, Out)) return;
		TArray<TArray<FADSKNamedValue>> Values;
		Values.SetNum(Brains.Num());
		for (int32 Index = 0; Index < Brains.Num(); ++Index)
		{
			FString Error;
			if (!ParseEvent(*Brains[Index], EventName, Args, Values[Index], Error)) { Out.Logf(TEXT("adsk: %s"), *Error); return; }
		}
		int32 Queued = 0;
		for (int32 Index = 0; Index < Brains.Num(); ++Index)
		{
			if (Brains[Index]->GetInstance()->EnqueueEvent(EventName, Values[Index]) == EADSKInputResult::Accepted) ++Queued;
			else Out.Logf(TEXT("adsk: event rejected by %s"), *OwnerName(*Brains[Index]));
		}
		Out.Logf(TEXT("adsk: %s queued for %d of %d brain(s)"), *EventName, Queued, Brains.Num());
	}
}

#endif // !UE_BUILD_SHIPPING
