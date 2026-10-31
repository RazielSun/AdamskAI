// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Groups/ADSKGroupSubsystem.h"

#include "Algo/Count.h"
#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKHostServices.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "Settings/ADSKSettings.h"

UADSKGroupSubsystem* UADSKGroupSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UADSKGroupSubsystem>() : nullptr;
}

void UADSKGroupSubsystem::Join(AActor& Member, FName Group)
{
	check(IsInGameThread());
	Leave(Member);
	if (Group.IsNone()) { return; }
	Groups.FindOrAdd(Group).Members.Add(&Member);
	MemberGroups.Add(&Member, Group);
}

void UADSKGroupSubsystem::Leave(const AActor& Member)
{
	check(IsInGameThread());
	FName Group;
	if (!MemberGroups.RemoveAndCopyValue(&Member, Group)) { return; }
	if (FGroup* Found = Groups.Find(Group))
	{
		Found->Members.RemoveAll([&Member](const TWeakObjectPtr<AActor>& Each) { return !Each.IsValid() || Each.Get() == &Member; });
		if (Found->Members.IsEmpty()) { Groups.Remove(Group); }
	}
}

FName UADSKGroupSubsystem::GetGroup(const AActor& Member) const
{
	const FName* Group = MemberGroups.Find(&Member);
	return Group ? *Group : NAME_None;
}

TArray<AActor*> UADSKGroupSubsystem::GetMembers(FName Group) const
{
	TArray<AActor*> Out;
	if (const FGroup* Found = Groups.Find(Group))
	{
		for (const TWeakObjectPtr<AActor>& Member : Found->Members)
		{
			if (AActor* Alive = Member.Get()) { Out.Add(Alive); }
		}
	}
	return Out;
}

AActor* UADSKGroupSubsystem::GetLeader(FName Group) const
{
	const TArray<AActor*> Members = GetMembers(Group);
	return Members.IsEmpty() ? nullptr : Members[0];
}

void UADSKGroupSubsystem::ReportTarget(const AActor& Member, AActor* Target)
{
	check(IsInGameThread());
	const FName* Group = MemberGroups.Find(&Member);
	FGroup* Found = Group ? Groups.Find(*Group) : nullptr;
	if (!Found || !Target) { return; }
	Found->Target = Target;
	Found->SightingTime = Now();
}

AActor* UADSKGroupSubsystem::GetGroupTarget(const AActor& Member, double MaxAge) const
{
	const FGroup* Found = FindGroupOf(Member);
	return Found && Now() - Found->SightingTime <= MaxAge ? Found->Target.Get() : nullptr;
}

bool UADSKGroupSubsystem::Broadcast(const AActor& Sender, FName Event, float Delay, TConstArrayView<FADSKNamedValue> Parameters)
{
	check(IsInGameThread());
	const FName Group = GetGroup(Sender);
	if (Group.IsNone()) { return false; }
	const TWeakObjectPtr<const AActor> WeakSender(&Sender);
	if (Delay <= 0.f)
	{
		Deliver(WeakSender, Group, Event, Parameters);
		return true;
	}
	FTimerHandle Unused;
	GetWorld()->GetTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, WeakSender, Group, Event, Copy = TArray<FADSKNamedValue>(Parameters)]()
	{
		Deliver(WeakSender, Group, Event, Copy);
	}), Delay, false);
	return true;
}

void UADSKGroupSubsystem::Deliver(TWeakObjectPtr<const AActor> Sender, FName Group, FName Event, TConstArrayView<FADSKNamedValue> Parameters)
{
	const AActor* Caller = Sender.Get();
	if (!Caller || GetGroup(*Caller) != Group) { return; }
	for (AActor* Member : GetMembers(Group))
	{
		UADSKBrainComponent* Brain = Member != Caller ? FindBrain(Member) : nullptr;
		if (Brain && Brain->IsADSKRunning()) { Brain->EnqueueEvent(Event.ToString(), Parameters); }
	}
}

UADSKBrainComponent* UADSKGroupSubsystem::FindBrain(const AActor* Actor)
{
	if (!Actor) { return nullptr; }
	if (UADSKBrainComponent* Own = Actor->FindComponentByClass<UADSKBrainComponent>()) { return Own; }
	const APawn* Pawn = Cast<APawn>(Actor);
	return Pawn && Pawn->GetController() ? Pawn->GetController()->FindComponentByClass<UADSKBrainComponent>() : nullptr;
}

bool UADSKGroupSubsystem::TakeToken(FName Pool, const AActor* Target, const UObject& Holder)
{
	check(IsInGameThread());
	TArray<TWeakObjectPtr<const UObject>>& Holders = Tokens.FindOrAdd(FTokenKey(Pool, Target));
	// A holder destroyed without returning its token must not keep it.
	Holders.RemoveAll([](const TWeakObjectPtr<const UObject>& Each) { return !Each.IsValid(); });
	if (Holders.Contains(&Holder)) { return true; }
	if (Holders.Num() >= GetPoolSize(Pool)) { return false; }
	Holders.Add(&Holder);
	return true;
}

void UADSKGroupSubsystem::ReturnToken(FName Pool, const AActor* Target, const UObject& Holder)
{
	check(IsInGameThread());
	const FTokenKey Key(Pool, Target);
	if (TArray<TWeakObjectPtr<const UObject>>* Holders = Tokens.Find(Key))
	{
		Holders->RemoveAll([&Holder](const TWeakObjectPtr<const UObject>& Each) { return !Each.IsValid() || Each.Get() == &Holder; });
		if (Holders->IsEmpty()) { Tokens.Remove(Key); }
	}
}

int32 UADSKGroupSubsystem::CountHolders(FName Pool, const AActor* Target) const
{
	const TArray<TWeakObjectPtr<const UObject>>* Holders = Tokens.Find(FTokenKey(Pool, Target));
	return Holders ? Algo::CountIf(*Holders, [](const TWeakObjectPtr<const UObject>& Each) { return Each.IsValid(); }) : 0;
}

int32 UADSKGroupSubsystem::GetPoolSize(FName Pool)
{
	const int32* Size = GetDefault<UADSKSettings>()->TokenPools.Find(Pool);
	return Size ? FMath::Max(0, *Size) : 1;
}

double UADSKGroupSubsystem::Now() const
{
	return IADSKHostServices::Get(GetWorld()).GetTimeSeconds(GetWorld());
}

const UADSKGroupSubsystem::FGroup* UADSKGroupSubsystem::FindGroupOf(const AActor& Member) const
{
	const FName* Group = MemberGroups.Find(&Member);
	return Group ? Groups.Find(*Group) : nullptr;
}
