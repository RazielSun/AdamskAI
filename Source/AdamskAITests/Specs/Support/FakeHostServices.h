// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Brain/ADSKHostServices.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Engine/World.h"
#include "Support/TestChecks.h"

/** Each service the test set replaces the engine's; the rest go to the engine. */
class FADSKFakeHostServices final : public IADSKHostServices
{
public:
	TFunction<int32(AActor& Querier, const UEnvQuery& Query, EEnvQueryRunMode::Type Mode, TConstArrayView<FADSKNamedParam> Params,
		TFunction<void(const FEnvQueryResult&)> OnFinished)> Run;
	TFunction<void(UWorld* World, int32 QueryId)> Abort;
	TOptional<double> Time;
	TOptional<TArray<FVector>> Viewers;

	virtual int32 RunQuery(AActor& Querier, const UEnvQuery& Query, EEnvQueryRunMode::Type Mode, TConstArrayView<FADSKNamedParam> Params,
		TFunction<void(const FEnvQueryResult&)> OnFinished) override
	{
		return Run ? Run(Querier, Query, Mode, Params, MoveTemp(OnFinished)) : Engine().RunQuery(Querier, Query, Mode, Params, MoveTemp(OnFinished));
	}

	virtual void AbortQuery(UWorld* World, int32 QueryId) override
	{
		Abort ? Abort(World, QueryId) : Engine().AbortQuery(World, QueryId);
	}

	virtual double GetTimeSeconds(const UWorld* World) const override
	{
		return Time.IsSet() ? *Time : Engine().GetTimeSeconds(World);
	}

	virtual void GetViewers(const UWorld* World, TArray<FVector>& OutViewers) const override
	{
		if (Viewers.IsSet()) { OutViewers = *Viewers; return; }
		Engine().GetViewers(World, OutViewers);
	}
};

/** A fake installed on World's subsystem until destroyed; set its fields through ->. */
class FADSKScopedFakeHostServices
{
public:
	explicit FADSKScopedFakeHostServices(UWorld& World)
		: Subsystem(World.GetSubsystem<UADSKWorldSubsystem>())
		, Fake(MakeShared<FADSKFakeHostServices>())
	{
		if (ADSKTest::Expect(Subsystem != nullptr, TEXT("the test world has no AdamskAI world subsystem to fake services on")))
		{
			Subsystem->SetHostServices(Fake);
		}
	}

	~FADSKScopedFakeHostServices()
	{
		if (Subsystem.IsValid()) { Subsystem->SetHostServices(nullptr); }
	}

	FADSKScopedFakeHostServices(const FADSKScopedFakeHostServices&) = delete;
	FADSKScopedFakeHostServices& operator=(const FADSKScopedFakeHostServices&) = delete;

	FADSKFakeHostServices* operator->() const { return &*Fake; }

private:
	TWeakObjectPtr<UADSKWorldSubsystem> Subsystem;
	TSharedRef<FADSKFakeHostServices> Fake;
};
