// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Bridges/ADSKPerceptionBridgeComponent.h"

#include "ADSKHostModule.h"
#include "Algo/AnyOf.h"
#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKHostServices.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Objects/ADSKObjectHandles.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Sight.h"
#include "Program/ADSKDefinition.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Trace/ADSKStats.h"

DECLARE_CYCLE_STAT(TEXT("Perception bridge"), STAT_ADSKPerceptionBridge, STATGROUP_AdamskAI);

UADSKPerceptionBridgeComponent::UADSKPerceptionBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UADSKPerceptionBridgeComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* Owner = GetOwner();
	if (!Owner) { Disable(TEXT("missing owner")); return; }
	TInlineComponentArray<UAIPerceptionComponent*> Perceptions(Owner);
	TInlineComponentArray<UADSKBrainComponent*> Brains(Owner);
	if (Perceptions.Num() != 1 || Brains.Num() != 1)
	{
		Disable(FString::Printf(TEXT("requires exactly one perception component and one AdamskAI brain (found %d and %d)"), Perceptions.Num(), Brains.Num()));
		return;
	}
	Perception = Perceptions[0];
	Brain = Brains[0];
	FString Error;
	TWeakObjectPtr<UADSKPerceptionBridgeComponent> WeakThis(this);
	if (!Brains[0]->AddBindingValidator(this, [WeakThis](const FADSKDefinition& Definition, FString& OutError)
	{
		return !WeakThis.IsValid() || WeakThis->ValidateDefinition(Definition, OutError);
	}, Error))
	{
		Disable(Error);
		return;
	}
	for (const FADSKPerceptionBinding& Binding : Bindings)
	{
		if (!Binding.Sense) continue;
		FBindingState& State = States.FindOrAdd(Binding.Sense.Get());
		State.ForgetAfter = Binding.ForgetAfter;
		State.RequiredActorTag = Binding.RequiredActorTag;
		State.RequiredStimulusTag = Binding.RequiredStimulusTag;
		State.IgnoreGameplayTags = Binding.IgnoreGameplayTags;
	}
	Perceptions[0]->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &UADSKPerceptionBridgeComponent::OnTargetPerceptionUpdated);
	PreTickHandle = Brains[0]->OnPreTick.AddUObject(this, &UADSKPerceptionBridgeComponent::OnPreTick);
	StoppedHandle = Brains[0]->OnStopped.AddUObject(this, &UADSKPerceptionBridgeComponent::OnStopped);
	bEnabled = true;
}

void UADSKPerceptionBridgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAIPerceptionComponent* Value = Perception.Get()) Value->OnTargetPerceptionUpdated.RemoveDynamic(this, &UADSKPerceptionBridgeComponent::OnTargetPerceptionUpdated);
	if (UADSKBrainComponent* Value = Brain.Get())
	{
		if (PreTickHandle.IsValid()) Value->OnPreTick.Remove(PreTickHandle);
		if (StoppedHandle.IsValid()) Value->OnStopped.Remove(StoppedHandle);
		Value->RemoveBindingValidators(this);
	}
	PreTickHandle.Reset();
	StoppedHandle.Reset();
	Perception.Reset();
	Brain.Reset();
	States.Reset();
	ResolvedInputs.Reset();
	ResolvedSerial = 0;
	WarnedUnboundSenses.Reset();
	LoggedInputFailures.Reset();
#if WITH_AUTOMATION_TESTS
	TestResyncCandidates.Reset();
#endif
	LastPreTick.Reset();
	SelectTarget.Unbind();
	bEnabled = false;
	Super::EndPlay(EndPlayReason);
}

void UADSKPerceptionBridgeComponent::Disable(const FString& Reason)
{
	bEnabled = false;
	if (!bLoggedDisable)
	{
		bLoggedDisable = true;
		UE_LOG(LogAdamskAIHost, Warning, TEXT("AdamskAI perception bridge disabled on %s: %s"), *GetPathName(), *Reason);
	}
}

FString UADSKPerceptionBridgeComponent::BindingLabel(const FADSKPerceptionBinding& Binding) const
{
	for (const FString* Name : {&Binding.HasTargetInput, &Binding.TargetLocationInput, &Binding.TargetInput, &Binding.TargetSeenEvent, &Binding.TargetLostEvent, &Binding.StimulusEvent,
		&Binding.SensedInput, &Binding.TargetCountInput, &Binding.AlertInput, &Binding.AlertedEvent, &Binding.CalmedEvent})
	{
		if (!Name->IsEmpty()) return *Name;
	}
	return Binding.Sense ? Binding.Sense->GetName() : TEXT("none");
}

bool UADSKPerceptionBridgeComponent::ValidateDefinition(const FADSKDefinition& Definition, FString& OutError) const
{
	TSet<UClass*> Senses;
	TSet<FString> Names;
	auto Fail = [&OutError](const FString& Label, const TCHAR* Detail) { OutError = FString::Printf(TEXT("perception-binding: %s: %s"), *Label, Detail); return false; };
	auto CheckInput = [&Definition, &Fail](const FString& Name, EADSKValueType Type, const FString& Label)
	{
		if (Name.IsEmpty()) return true;
		const int32 Index = Definition.FindSlot(Name);
		return Index != INDEX_NONE && Definition.GetSlots()[Index].bIsInput && Definition.GetSlots()[Index].Type == Type
			? true : Fail(Label, TEXT("input must exist with exact input type"));
	};
	auto CheckEvent = [&Definition, &Fail](const FString& Name, bool bStimulus, const FString& Label)
	{
		if (Name.IsEmpty()) return true;
		const int32 Index = Definition.FindEvent(Name);
		if (Index == INDEX_NONE) return Fail(Label, TEXT("event does not exist"));
		const FADSKEventDefinition& Event = Definition.GetEvents()[Index];
		if (!bStimulus) return Event.ParamCount == 0 ? true : Fail(Label, TEXT("event must have zero parameters"));
		if (Event.ParamCount != 1) return Fail(Label, TEXT("stimulus event must have one location parameter"));
		const FADSKEventParamDefinition& Param = Definition.GetEventParams()[Event.FirstParam];
		return Param.Name == TEXT("location") && Param.Type == EADSKValueType::Vector ? true : Fail(Label, TEXT("stimulus event parameter must be Vector location"));
	};
	for (const FADSKPerceptionBinding& Binding : Bindings)
	{
		const FString Label = BindingLabel(Binding);
		if (!Binding.Sense) return Fail(Label, TEXT("sense is required"));
		if (Senses.Contains(Binding.Sense.Get())) return Fail(Label, TEXT("sense must be unique"));
		Senses.Add(Binding.Sense.Get());
		for (const FString* Name : {&Binding.HasTargetInput, &Binding.TargetLocationInput, &Binding.TargetInput, &Binding.TargetSeenEvent, &Binding.TargetLostEvent, &Binding.StimulusEvent,
			&Binding.SensedInput, &Binding.TargetCountInput, &Binding.AlertInput, &Binding.AlertedEvent, &Binding.CalmedEvent})
		{
			if (!Name->IsEmpty() && Names.Contains(*Name)) return Fail(Label, TEXT("configured names must be unique"));
			if (!Name->IsEmpty()) Names.Add(*Name);
		}
		if (!CheckInput(Binding.HasTargetInput, EADSKValueType::Bool, Label) || !CheckInput(Binding.TargetLocationInput, EADSKValueType::Vector, Label)
			|| !CheckInput(Binding.TargetInput, EADSKValueType::Object, Label)
			|| !CheckEvent(Binding.TargetSeenEvent, false, Label) || !CheckEvent(Binding.TargetLostEvent, false, Label)
			|| !CheckEvent(Binding.StimulusEvent, true, Label)
			|| !CheckInput(Binding.SensedInput, EADSKValueType::Bool, Label) || !CheckInput(Binding.TargetCountInput, EADSKValueType::Int, Label)
			|| !CheckInput(Binding.AlertInput, EADSKValueType::Float, Label)
			|| !CheckEvent(Binding.AlertedEvent, false, Label) || !CheckEvent(Binding.CalmedEvent, false, Label)) return false;
	}
	return true;
}

void UADSKPerceptionBridgeComponent::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!bEnabled) return;
	const TSubclassOf<UAISense> Sense = UAIPerceptionSystem::GetSenseClassForStimulus(this, Stimulus);
	ProcessResolvedStimulus(Sense.Get(), Actor, Stimulus.StimulusLocation, Stimulus.WasSuccessfullySensed(), Stimulus.Tag);
}

void UADSKPerceptionBridgeComponent::ProcessResolvedStimulus(UClass* Sense, AActor* Actor, FVector Location, bool bSuccessfullySensed, FName Tag)
{
	if (!Sense)
	{
		if (!bWarnedUnknownSense) { bWarnedUnknownSense = true; UE_LOG(LogAdamskAIHost, Warning, TEXT("AdamskAI perception bridge ignored null or unknown sense")); }
		return;
	}
	if (!States.Contains(Sense))
	{
		if (!WarnedUnboundSenses.Contains(Sense)) { WarnedUnboundSenses.Add(Sense); UE_LOG(LogAdamskAIHost, Warning, TEXT("AdamskAI perception bridge ignored unbound sense %s"), *Sense->GetName()); }
		return;
	}
	ProcessStimulus(Sense, Actor, Location, bSuccessfullySensed, Tag);
}

bool UADSKPerceptionBridgeComponent::AcceptsActor(const FBindingState& State, const AActor& Actor)
{
	return State.RequiredActorTag.IsNone() || Actor.ActorHasTag(State.RequiredActorTag);
}

bool UADSKPerceptionBridgeComponent::AcceptsStimulus(const FBindingState& State, const AActor& Actor, FName Tag)
{
	return AcceptsActor(State, Actor) && (State.RequiredStimulusTag.IsNone() || State.RequiredStimulusTag == Tag);
}

bool UADSKPerceptionBridgeComponent::IsIgnored(const FBindingState& State, const AActor& Actor)
{
	if (State.IgnoreGameplayTags.IsEmpty()) return false;
	const IGameplayTagAssetInterface* Tags = Cast<const IGameplayTagAssetInterface>(&Actor);
	return Tags && Tags->HasAnyMatchingGameplayTags(State.IgnoreGameplayTags);
}

void UADSKPerceptionBridgeComponent::ProcessStimulus(UClass* Sense, AActor* Actor, FVector Location, bool bSuccessfullySensed, FName Tag)
{
	FBindingState* State = States.Find(Sense);
	if (!State || !IsValid(Actor)) return;
	if (!bSuccessfullySensed)
	{
		// With a memory the actor stays a candidate at the location it was last sensed at.
		FBindingState::FCandidate* Candidate = State->ForgetAfter > 0.f ? State->Candidates.Find(Actor) : nullptr;
		if (!Candidate) { State->bSortDirty |= State->Candidates.Remove(Actor) > 0; }
		else if (Candidate->LostAt < 0.0) { Candidate->LostAt = Now(); }
		return;
	}
	if (!AcceptsStimulus(*State, *Actor, Tag)) return;
	AddCandidate(*State, Actor, Location);
	if (IsIgnored(*State, *Actor)) return;
	if (State->Stimuli.Num() == MaxQueuedStimuli) { State->Stimuli.RemoveAt(0); ++DroppedStimuli; }
	State->Stimuli.Add(Location);
}

void UADSKPerceptionBridgeComponent::AddCandidate(FBindingState& State, AActor* Actor, const FVector& Location)
{
	if (FBindingState::FCandidate* Existing = State.Candidates.Find(Actor)) { Existing->Location = Location; Existing->LostAt = -1.0; return; }
	State.Candidates.Add(Actor, {Location, Actor->GetPathName()});
	State.bSortDirty = true;
}

void UADSKPerceptionBridgeComponent::Resync(FBindingState& State, TSubclassOf<UAISense> Sense)
{
	State.Candidates.Reset();
	State.bSortDirty = true;
#if WITH_AUTOMATION_TESTS
	if (const TArray<FADSKPerceptionTestStimulus>* Test = TestResyncCandidates.Find(Sense.Get()))
	{
		for (const FADSKPerceptionTestStimulus& Candidate : *Test)
		{
			AActor* Actor = Candidate.Actor.Get();
			if (Actor && AcceptsStimulus(State, *Actor, Candidate.Tag)) AddCandidate(State, Actor, Candidate.Location);
		}
		State.bNeedsResync = false;
		return;
	}
#endif
	UAIPerceptionComponent* Source = Perception.Get();
	if (!Source) { State.bNeedsResync = false; return; }
	TArray<AActor*> Actors;
	Source->GetCurrentlyPerceivedActors(Sense, Actors);
	for (AActor* Actor : Actors)
	{
		FActorPerceptionBlueprintInfo Info;
		if (!IsValid(Actor) || !Source->GetActorsPerception(Actor, Info)) continue;
		for (int32 Index = Info.LastSensedStimuli.Num() - 1; Index >= 0; --Index)
		{
			const FAIStimulus& Stimulus = Info.LastSensedStimuli[Index];
			if (Stimulus.WasSuccessfullySensed() && UAIPerceptionSystem::GetSenseClassForStimulus(this, Stimulus) == Sense)
			{
				if (AcceptsStimulus(State, *Actor, Stimulus.Tag)) AddCandidate(State, Actor, Stimulus.StimulusLocation);
				break;
			}
		}
	}
	State.bNeedsResync = false;
}

double UADSKPerceptionBridgeComponent::Now() const
{
	const UWorld* World = GetWorld();
	return IADSKHostServices::Get(World).GetTimeSeconds(World);
}

AActor* UADSKPerceptionBridgeComponent::Select(FBindingState& State, TSubclassOf<UAISense> Sense)
{
	const double Time = Now();
	for (auto It = State.Candidates.CreateIterator(); It; ++It)
	{
		const bool bForgotten = It.Value().LostAt >= 0.0 && Time - It.Value().LostAt >= State.ForgetAfter;
		if (!It.Key().IsValid() || bForgotten || !AcceptsActor(State, *It.Key().Get())) { It.RemoveCurrent(); State.bSortDirty = true; }
	}
	// Most ticks see the same candidates as the last one, so the string sort runs only when the set changed.
	if (State.bSortDirty)
	{
		using FKeyed = TPair<TWeakObjectPtr<AActor>, const FString*>;
		TArray<FKeyed, TInlineAllocator<8>> Keyed;
		Keyed.Reserve(State.Candidates.Num());
		for (const TPair<TWeakObjectPtr<AActor>, FBindingState::FCandidate>& Candidate : State.Candidates) Keyed.Add({Candidate.Key, &Candidate.Value.SortKey});
		Keyed.Sort([](const FKeyed& A, const FKeyed& B) { return *A.Value < *B.Value; });
		State.Sorted.Reset();
		for (const FKeyed& Entry : Keyed) State.Sorted.Add(Entry.Key);
		State.bSortDirty = false;
	}
	TArray<AActor*>& Candidates = SelectScratch;
	Candidates.Reset();
	// An ignored candidate stays tracked, so clearing its tag brings it back without a new stimulus.
	for (const TWeakObjectPtr<AActor>& Entry : State.Sorted) { if (!IsIgnored(State, *Entry.Get())) Candidates.Add(Entry.Get()); }
	State.EligibleCount = Candidates.Num();
	State.bAnyEligibleSensed = Algo::AnyOf(State.Candidates, [&State](const TPair<TWeakObjectPtr<AActor>, FBindingState::FCandidate>& Candidate)
	{
		return Candidate.Value.LostAt < 0.0 && !IsIgnored(State, *Candidate.Key.Get());
	});
	if (SelectTarget.IsBound())
	{
		AActor* Selected = SelectTarget.Execute(*this, Sense, Candidates);
		if (!Selected || Candidates.Contains(Selected)) return Selected;
		if (!State.bWarnedForeignSelection)
		{
			State.bWarnedForeignSelection = true;
			UE_LOG(LogAdamskAIHost, Warning, TEXT("AdamskAI perception bridge selector returned a foreign actor for %s"), *Sense->GetName());
		}
		return nullptr;
	}
	AActor* Host = Brain.IsValid() ? Brain->GetHostActor() : nullptr;
	if (!Host) return nullptr;
	AActor* Best = nullptr;
	bool bBestSensed = false;
	double BestDistance = 0.0;
	for (AActor* Candidate : Candidates)
	{
		// A remembered actor is judged where it was last sensed, and only when nothing is sensed.
		const FBindingState::FCandidate& Entry = State.Candidates.FindChecked(Candidate);
		const bool bSensed = Entry.LostAt < 0.0;
		const double Distance = FVector::DistSquared(Host->GetActorLocation(), bSensed ? Candidate->GetActorLocation() : Entry.Location);
		// Candidates are in path-name order, so a strict comparison keeps the lowest path name on a tie.
		if (!Best || (bSensed && !bBestSensed) || (bSensed == bBestSensed && Distance < BestDistance)) { Best = Candidate; bBestSensed = bSensed; BestDistance = Distance; }
	}
	return Best;
}

void UADSKPerceptionBridgeComponent::LogInputFailure(int32 BindingIndex, EADSKInputResult Result)
{
	const FString Key = FString::Printf(TEXT("%d:%s"), BindingIndex, LexToString(Result));
	if (!LoggedInputFailures.Contains(Key)) { LoggedInputFailures.Add(Key); UE_LOG(LogAdamskAIHost, Warning, TEXT("AdamskAI perception bridge input write failed for binding %d: %s"), BindingIndex, LexToString(Result)); }
}

void UADSKPerceptionBridgeComponent::UpdateAlert(int32 BindingIndex, FBindingState& State, float Delta, FADSKInstance& Instance)
{
	const FADSKPerceptionBinding& Binding = Bindings[BindingIndex];
	State.Alert = FMath::Clamp(State.Alert + (State.bAnyEligibleSensed ? Binding.AlertRise : -Binding.AlertDecay) * Delta, 0.f, 1.f);
	if (!Binding.AlertInput.IsEmpty()) { const EADSKInputResult Result = Brain->SetInput(ResolvedInputs[BindingIndex].Alert, FADSKValue::MakeFloat(State.Alert)); if (Result != EADSKInputResult::Accepted) LogInputFailure(BindingIndex, Result); }
	const FString* Crossing = !State.bAlerted && State.Alert >= 1.f ? &Binding.AlertedEvent : State.bAlerted && State.Alert <= 0.f ? &Binding.CalmedEvent : nullptr;
	if (!Crossing) return;
	State.bAlerted = !State.bAlerted;
	if (!Crossing->IsEmpty()) { const EADSKInputResult Result = Instance.EnqueueEvent(State.bAlerted ? ResolvedInputs[BindingIndex].Alerted : ResolvedInputs[BindingIndex].Calmed); if (Result != EADSKInputResult::Accepted) LogInputFailure(BindingIndex, Result); }
}

void UADSKPerceptionBridgeComponent::OnPreTick()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UADSKPerceptionBridgeComponent::OnPreTick);
	SCOPE_CYCLE_COUNTER(STAT_ADSKPerceptionBridge);
	if (!bEnabled || !Brain.IsValid()) return;
	FADSKInstance* Instance = Brain->GetInstance();
	if (!Instance) return;
	if (ResolvedSerial != Instance->GetDefinition()->GetSerial())
	{
		ResolvedSerial = Instance->GetDefinition()->GetSerial();
		ResolvedInputs.Reset(Bindings.Num());
		for (const FADSKPerceptionBinding& Binding : Bindings)
		{
			ResolvedInputs.Add({Instance->FindInput(Binding.HasTargetInput), Instance->FindInput(Binding.TargetLocationInput), Instance->FindInput(Binding.TargetInput),
				Instance->FindInput(Binding.SensedInput), Instance->FindInput(Binding.TargetCountInput), Instance->FindInput(Binding.AlertInput),
				Instance->FindEvent(Binding.TargetSeenEvent), Instance->FindEvent(Binding.TargetLostEvent), Instance->FindEvent(Binding.StimulusEvent),
				Instance->FindEvent(Binding.AlertedEvent), Instance->FindEvent(Binding.CalmedEvent)});
		}
	}
	const double Time = Now();
	const float Delta = LastPreTick.IsSet() ? static_cast<float>(FMath::Max(0.0, Time - *LastPreTick)) : 0.f;
	LastPreTick = Time;
	for (int32 Index = 0; Index < Bindings.Num(); ++Index)
	{
		const FADSKPerceptionBinding& Binding = Bindings[Index];
		FBindingState* State = States.Find(Binding.Sense.Get());
		if (!State) continue;
		if (State->bNeedsResync) Resync(*State, Binding.Sense);
		AActor* Target = Select(*State, Binding.Sense);
		if (!Binding.HasTargetInput.IsEmpty()) { const EADSKInputResult Result = Brain->SetInput(ResolvedInputs[Index].HasTarget, FADSKValue::MakeBool(Target != nullptr)); if (Result != EADSKInputResult::Accepted) LogInputFailure(Index, Result); }
		if (!Binding.TargetInput.IsEmpty()) { const EADSKInputResult Result = Brain->SetInput(ResolvedInputs[Index].Target, FADSKObjectHandles::Make(Target)); if (Result != EADSKInputResult::Accepted) LogInputFailure(Index, Result); }
		const bool bTargetSensed = Target && State->Candidates.FindChecked(Target).LostAt < 0.0;
		if (!Binding.SensedInput.IsEmpty()) { const EADSKInputResult Result = Brain->SetInput(ResolvedInputs[Index].Sensed, FADSKValue::MakeBool(bTargetSensed)); if (Result != EADSKInputResult::Accepted) LogInputFailure(Index, Result); }
		if (!Binding.TargetCountInput.IsEmpty()) { const EADSKInputResult Result = Brain->SetInput(ResolvedInputs[Index].TargetCount, FADSKValue::MakeInt(State->EligibleCount)); if (Result != EADSKInputResult::Accepted) LogInputFailure(Index, Result); }
		UpdateAlert(Index, *State, Delta, *Instance);
		if (Target && !Binding.TargetLocationInput.IsEmpty())
		{
			// Sight follows the actor while it is seen; anything remembered stays where it was last sensed.
			const FVector Location = bTargetSensed && Binding.Sense == UAISense_Sight::StaticClass() ? Target->GetActorLocation() : State->Candidates.FindChecked(Target).Location;
			const EADSKInputResult Result = Brain->SetInput(ResolvedInputs[Index].TargetLocation, FADSKValue::MakeVector(Location));
			if (Result != EADSKInputResult::Accepted) LogInputFailure(Index, Result);
		}
		const bool bWasTargeted = State->bHadPublishedTarget;
		if (!bWasTargeted && Target && !Binding.TargetSeenEvent.IsEmpty()) { const EADSKInputResult Result = Instance->EnqueueEvent(ResolvedInputs[Index].TargetSeen); if (Result != EADSKInputResult::Accepted) LogInputFailure(Index, Result); }
		if (bWasTargeted && !Target && !Binding.TargetLostEvent.IsEmpty()) { const EADSKInputResult Result = Instance->EnqueueEvent(ResolvedInputs[Index].TargetLost); if (Result != EADSKInputResult::Accepted) LogInputFailure(Index, Result); }
		State->bHadPublishedTarget = Target != nullptr;
		for (const FVector& Location : State->Stimuli)
		{
			if (!Binding.StimulusEvent.IsEmpty()) { const FADSKNamedValue Parameter{TEXT("location"), FADSKValue::MakeVector(Location)}; const EADSKInputResult Result = Instance->EnqueueEvent(ResolvedInputs[Index].Stimulus, MakeArrayView(&Parameter, 1)); if (Result != EADSKInputResult::Accepted) LogInputFailure(Index, Result); }
		}
		State->Stimuli.Reset();
	}
}

void UADSKPerceptionBridgeComponent::OnStopped()
{
	for (TPair<UClass*, FBindingState>& Pair : States)
	{
		Pair.Value.bHadPublishedTarget = false;
		Pair.Value.Stimuli.Reset();
		Pair.Value.bNeedsResync = true;
		Pair.Value.Alert = 0.f;
		Pair.Value.bAlerted = false;
	}
	LastPreTick.Reset();
}

#if WITH_AUTOMATION_TESTS
void UADSKPerceptionBridgeComponent::InjectStimulusForTest(TSubclassOf<UAISense> Sense, AActor* Actor, FVector Location, bool bSuccessfullySensed, FName Tag) { ProcessStimulus(Sense.Get(), Actor, Location, bSuccessfullySensed, Tag); }
void UADSKPerceptionBridgeComponent::InjectResolvedStimulusForTest(TSubclassOf<UAISense> Sense, AActor* Actor, FVector Location, bool bSuccessfullySensed, FName Tag) { ProcessResolvedStimulus(Sense.Get(), Actor, Location, bSuccessfullySensed, Tag); }
void UADSKPerceptionBridgeComponent::SetResyncCandidatesForTest(TSubclassOf<UAISense> Sense, TArray<FADSKPerceptionTestStimulus> Candidates) { TestResyncCandidates.Add(Sense.Get(), MoveTemp(Candidates)); }
AActor* UADSKPerceptionBridgeComponent::SelectTargetForTest(TSubclassOf<UAISense> Sense) { if (FBindingState* State = States.Find(Sense.Get())) return Select(*State, Sense); return nullptr; }
int32 UADSKPerceptionBridgeComponent::GetCandidateCountForTest(TSubclassOf<UAISense> Sense) const { if (const FBindingState* State = States.Find(Sense.Get())) return State->Candidates.Num(); return 0; }
int32 UADSKPerceptionBridgeComponent::GetQueuedStimulusCountForTest(TSubclassOf<UAISense> Sense) const { if (const FBindingState* State = States.Find(Sense.Get())) return State->Stimuli.Num(); return 0; }
bool UADSKPerceptionBridgeComponent::NeedsResyncForTest(TSubclassOf<UAISense> Sense) const { if (const FBindingState* State = States.Find(Sense.Get())) return State->bNeedsResync; return false; }
bool UADSKPerceptionBridgeComponent::HasPerceptionRegistrationForTest() const
{
	const UAIPerceptionComponent* Value = Perception.Get();
	return Value && Value->OnTargetPerceptionUpdated.IsAlreadyBound(this, &UADSKPerceptionBridgeComponent::OnTargetPerceptionUpdated);
}
#endif
