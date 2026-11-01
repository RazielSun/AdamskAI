// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Brain/ADSKHostServices.h"

#include "Brain/ADSKWorldSubsystem.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "GameFramework/PlayerController.h"

namespace ADSKHostServicesPrivate
{
	class FEngineHostServices final : public IADSKHostServices
	{
	public:
		virtual int32 RunQuery(AActor& Querier, const UEnvQuery& Query, EEnvQueryRunMode::Type Mode, TConstArrayView<FADSKNamedParam> Params,
			TFunction<void(const FEnvQueryResult&)> OnFinished) override
		{
			FEnvQueryRequest Request(&Query, &Querier);
			for (const FADSKNamedParam& Param : Params)
			{
				switch (Param.Type)
				{
				case EADSKParamType::Float: Request.SetFloatParam(Param.Name, Param.Value); break;
				case EADSKParamType::Int: Request.SetIntParam(Param.Name, Param.IntValue); break;
				case EADSKParamType::Bool: Request.SetBoolParam(Param.Name, Param.bBoolValue); break;
				}
			}
			// The callback is bound to the querier, so destroying it silences a late result.
			return Request.Execute(Mode,
				FQueryFinishedSignature::CreateWeakLambda(&Querier, [OnFinished = MoveTemp(OnFinished)](TSharedPtr<FEnvQueryResult> Result) { if (Result.IsValid()) { OnFinished(*Result); } }));
		}

		virtual void AbortQuery(UWorld* World, int32 QueryId) override
		{
			if (UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(World)) { Manager->AbortQuery(QueryId); }
		}

		virtual double GetTimeSeconds(const UWorld* World) const override
		{
			return World ? World->GetTimeSeconds() : 0.0;
		}

		virtual void GetViewers(const UWorld* World, TArray<FVector>& OutViewers) const override
		{
			OutViewers.Reset();
			if (!World) return;
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				const APlayerController* Controller = It->Get();
				if (!Controller) continue;
				// No pawn and no own camera (joining, dead): its view point is the spawn spot nobody watches.
				const bool bSyncedSpectator = Controller->IsInState(NAME_Spectating) && Controller->HasAuthority() && !Controller->IsLocalController();
				if (!Controller->GetPawn() && !bSyncedSpectator && Controller->GetViewTarget() == Controller) continue;
				FVector Location; FRotator Rotation;
				Controller->GetPlayerViewPoint(Location, Rotation);
				OutViewers.Add(Location);
			}
		}
	};
}

IADSKHostServices& IADSKHostServices::Engine()
{
	static ADSKHostServicesPrivate::FEngineHostServices Services;
	return Services;
}

IADSKHostServices& IADSKHostServices::Get(const UWorld* World)
{
	const UADSKWorldSubsystem* Subsystem = World ? World->GetSubsystem<UADSKWorldSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetHostServices() : Engine();
}
