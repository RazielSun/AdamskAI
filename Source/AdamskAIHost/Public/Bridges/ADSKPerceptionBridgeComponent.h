// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Misc/AutomationTest.h"
#include "Perception/AISense.h"
#include "Runtime/ADSKInstance.h"

#include "ADSKPerceptionBridgeComponent.generated.h"

class AActor;
class UAIPerceptionComponent;
class UADSKBrainComponent;
class UADSKPerceptionBridgeComponent;
struct FAIStimulus;
class FADSKDefinition;

USTRUCT(BlueprintType)
struct FADSKPerceptionBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "AdamskAI") TSubclassOf<UAISense> Sense;
	UPROPERTY(EditAnywhere, Category = "AdamskAI") FString HasTargetInput;
	UPROPERTY(EditAnywhere, Category = "AdamskAI") FString TargetLocationInput;
	/** An Object Input: the selected actor, `none` when there is none. */
	UPROPERTY(EditAnywhere, Category = "AdamskAI") FString TargetInput;
	UPROPERTY(EditAnywhere, Category = "AdamskAI") FString TargetSeenEvent;
	UPROPERTY(EditAnywhere, Category = "AdamskAI") FString TargetLostEvent;
	UPROPERTY(EditAnywhere, Category = "AdamskAI") FString StimulusEvent;
	/** Optional AActor::Tags filter, not a GameplayTag. Configure before BeginPlay. */
	UPROPERTY(EditAnywhere, Category = "AdamskAI") FName RequiredActorTag = NAME_None;
	/** Optional exact FAIStimulus::Tag filter. Configure before BeginPlay; loss still clears accepted sight. */
	UPROPERTY(EditAnywhere, Category = "AdamskAI") FName RequiredStimulusTag = NAME_None;

	/** Seconds a lost actor stays a candidate at its last sensed location; 0 forgets at once. Sensed candidates always win. */
	UPROPERTY(EditAnywhere, Category = "AdamskAI", meta = (ClampMin = "0")) float ForgetAfter = 0.f;
	/** A Bool Input: true while the selected target is sensed, false while it is only remembered (or there is none). */
	UPROPERTY(EditAnywhere, Category = "AdamskAI") FString SensedInput;
	/** An Int Input: how many actors are candidates, sensed or remembered. */
	UPROPERTY(EditAnywhere, Category = "AdamskAI") FString TargetCountInput;

	/** A Float Input in [0, 1]: rises by AlertRise per second while any candidate is sensed, falls by AlertDecay otherwise. */
	UPROPERTY(EditAnywhere, Category = "AdamskAI") FString AlertInput;
	UPROPERTY(EditAnywhere, Category = "AdamskAI", meta = (ClampMin = "0")) float AlertRise = 1.f;
	UPROPERTY(EditAnywhere, Category = "AdamskAI", meta = (ClampMin = "0")) float AlertDecay = 0.25f;
	/** Zero-parameter events raised when the alert level reaches 1 and when it falls back to 0. */
	UPROPERTY(EditAnywhere, Category = "AdamskAI") FString AlertedEvent;
	UPROPERTY(EditAnywhere, Category = "AdamskAI") FString CalmedEvent;
};

DECLARE_DELEGATE_RetVal_ThreeParams(AActor*, FADSKSelectTarget, const UADSKPerceptionBridgeComponent&, TSubclassOf<UAISense>, TConstArrayView<AActor*>);

#if WITH_AUTOMATION_TESTS
struct FADSKPerceptionTestStimulus
{
	TWeakObjectPtr<AActor> Actor;
	FVector Location;
	FName Tag = NAME_None;
};
#endif

/** Mirrors configured perception senses into one AdamskAI brain immediately before its tick. */
UCLASS(ClassGroup = AI, meta = (BlueprintSpawnableComponent))
class ADAMSKAIHOST_API UADSKPerceptionBridgeComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UADSKPerceptionBridgeComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, Category = "AdamskAI")
	TArray<FADSKPerceptionBinding> Bindings;

	/** Optional policy hook. An unbound delegate selects nearest, with path-name tie breaking. */
	FADSKSelectTarget SelectTarget;

	uint32 GetDroppedStimuli() const { return DroppedStimuli; }
	bool IsBridgeEnabled() const { return bEnabled; }

#if WITH_AUTOMATION_TESTS
	void InjectStimulusForTest(TSubclassOf<UAISense> Sense, AActor* Actor, FVector Location, bool bSuccessfullySensed, FName Tag = NAME_None);
	void InjectResolvedStimulusForTest(TSubclassOf<UAISense> Sense, AActor* Actor, FVector Location, bool bSuccessfullySensed, FName Tag = NAME_None);
	void SetResyncCandidatesForTest(TSubclassOf<UAISense> Sense, TArray<FADSKPerceptionTestStimulus> Candidates);
	AActor* SelectTargetForTest(TSubclassOf<UAISense> Sense);
	bool ValidateDefinitionForTest(const FADSKDefinition& Definition, FString& OutError) const { return ValidateDefinition(Definition, OutError); }
	int32 GetCandidateCountForTest(TSubclassOf<UAISense> Sense) const;
	int32 GetQueuedStimulusCountForTest(TSubclassOf<UAISense> Sense) const;
	bool NeedsResyncForTest(TSubclassOf<UAISense> Sense) const;
	bool HasPerceptionRegistrationForTest() const;
	bool HasNativeRegistrationsForTest() const { return PreTickHandle.IsValid() || StoppedHandle.IsValid(); }
#endif

private:
	struct FBindingState
	{
		struct FCandidate
		{
			FVector Location = FVector::ZeroVector;
			/** The actor's path name, taken once on arrival: the deterministic order without a string per comparison. */
			FString SortKey;
			/** When the sense lost it; negative while it is sensed. */
			double LostAt = -1.0;
		};
		TMap<TWeakObjectPtr<AActor>, FCandidate> Candidates;
		/** Candidates by SortKey, rebuilt only when one is added or removed (bSortDirty). */
		TArray<TWeakObjectPtr<AActor>> Sorted;
		bool bSortDirty = true;
		TArray<FVector> Stimuli;
		float ForgetAfter = 0.f;
		FName RequiredActorTag;
		FName RequiredStimulusTag;
		float Alert = 0.f;
		bool bAlerted = false;
		bool bHadPublishedTarget = false;
		bool bNeedsResync = false;
		bool bWarnedForeignSelection = false;
	};

	UFUNCTION()
	void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);
	void OnPreTick();
	void OnStopped();
	bool ValidateDefinition(const FADSKDefinition& Definition, FString& OutError) const;
	void Disable(const FString& Reason);
	void ProcessResolvedStimulus(UClass* Sense, AActor* Actor, FVector Location, bool bSuccessfullySensed, FName Tag);
	void ProcessStimulus(UClass* Sense, AActor* Actor, FVector Location, bool bSuccessfullySensed, FName Tag);
	static bool AcceptsActor(const FBindingState& State, const AActor& Actor);
	static bool AcceptsStimulus(const FBindingState& State, const AActor& Actor, FName Tag);
	void Resync(FBindingState& State, TSubclassOf<UAISense> Sense);
	static void AddCandidate(FBindingState& State, AActor* Actor, const FVector& Location);
	AActor* Select(FBindingState& State, TSubclassOf<UAISense> Sense);
	double Now() const;
	void UpdateAlert(int32 BindingIndex, FBindingState& State, float Delta, FADSKInstance& Instance);
	void LogInputFailure(int32 BindingIndex, EADSKInputResult Result);
	FString BindingLabel(const FADSKPerceptionBinding& Binding) const;

	TWeakObjectPtr<UAIPerceptionComponent> Perception;
	TWeakObjectPtr<UADSKBrainComponent> Brain;
	TMap<UClass*, FBindingState> States;
	/** Per binding, its Inputs and Events resolved against the definition with ResolvedSerial; re-resolved after a reload. */
	struct FResolvedInputs
	{
		FADSKInputId HasTarget;
		FADSKInputId TargetLocation;
		FADSKInputId Target;
		FADSKInputId Sensed;
		FADSKInputId TargetCount;
		FADSKInputId Alert;
		FADSKEventId TargetSeen;
		FADSKEventId TargetLost;
		FADSKEventId Stimulus;
		FADSKEventId Alerted;
		FADSKEventId Calmed;
	};
	TArray<FResolvedInputs> ResolvedInputs;
	/** Stimuli kept per sense between two pre-ticks; the oldest is dropped (and counted) beyond this. */
	static constexpr int32 MaxQueuedStimuli = 8;
	/** Select's candidate list, kept so a pre-tick does not allocate. */
	TArray<AActor*> SelectScratch;
	uint64 ResolvedSerial = 0;
	TSet<UClass*> WarnedUnboundSenses;
	TSet<FString> LoggedInputFailures;
	FDelegateHandle PreTickHandle;
	FDelegateHandle StoppedHandle;
	uint32 DroppedStimuli = 0;
	bool bEnabled = false;
	bool bLoggedDisable = false;
	bool bWarnedUnknownSense = false;
#if WITH_AUTOMATION_TESTS
	TMap<UClass*, TArray<FADSKPerceptionTestStimulus>> TestResyncCandidates;
#endif
	TOptional<double> LastPreTick;
};
