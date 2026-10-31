// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Nodes/ADSKHostNodes.h"

#include "ADSKHostModule.h"
#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKHostServices.h"
#include "Nodes/ADSKAsyncLeaf.h"
#include "Nodes/ADSKHostNodeTypes.h"
#include "Objects/ADSKObjectHandles.h"
#include "Runtime/ADSKNode.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "AIController.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "VisualLogger/VisualLogger.h"

namespace ADSKHostNodesPrivate
{
	EEnvQueryRunMode::Type RunMode(const FADSKRunEQSConfig& Config)
	{
		if (Config.bRandomBest25) { return EEnvQueryRunMode::RandomBest25Pct; }
		switch (Config.Pick)
		{
		case EADSKQueryPick::RandomBest5: return EEnvQueryRunMode::RandomBest5Pct;
		case EADSKQueryPick::RandomBest25: return EEnvQueryRunMode::RandomBest25Pct;
		case EADSKQueryPick::Best: break;
		}
		return EEnvQueryRunMode::SingleResult;
	}

	bool IsFinitePoint(const FVector& Point) { return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y) && FMath::IsFinite(Point.Z); }

	/** Clears the gameplay focus only while it is still the point this node set, so a later owner keeps it. */
	void ReleaseFocalPoint(UADSKBrainComponent* Brain, const FVector& Point)
	{
		AAIController* Controller = Brain ? Brain->GetAIController() : nullptr;
		if (Controller && Controller->GetFocusActorForPriority(EAIFocusPriority::Gameplay) == nullptr && Controller->GetFocalPointForPriority(EAIFocusPriority::Gameplay).Equals(Point))
		{
			Controller->ClearFocus(EAIFocusPriority::Gameplay);
		}
	}

	bool TrackFocalPoint(FADSKNodeContext& Context, FVector& InOutPoint)
	{
		FVector Point; UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
		AAIController* Controller = Brain ? Brain->GetAIController() : nullptr;
		if (!Controller || !Context.ReadVector(0, Point) || !IsFinitePoint(Point)) { return false; }
		if (!Point.Equals(InOutPoint) || !Controller->GetFocalPointForPriority(EAIFocusPriority::Gameplay).Equals(Point))
		{
			Controller->SetFocalPoint(Point, EAIFocusPriority::Gameplay);
			UE_VLOG_SEGMENT(Brain->GetOwner(), LogAdamskAIHost, Log, Brain->GetHostActor() ? Brain->GetHostActor()->GetActorLocation() : Point, Point, FColor::Cyan,
				TEXT("%s focus"), *Context.GetNodeId().ToString());
		}
		InOutPoint = Point;
		return true;
	}

	UCharacterMovementComponent* FindCharacterMovement(FADSKNodeContext& Context)
	{
		UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
		const ACharacter* Character = Brain ? Cast<ACharacter>(Brain->GetHostActor()) : nullptr;
		return Character ? Character->GetCharacterMovement() : nullptr;
	}

	FADSKNodeDescriptor RunEQS()
	{
		FADSKNodeDescriptor Node; Node.Name = TEXT("RunEQS"); Node.ConfigStruct = FADSKRunEQSConfig::StaticStruct(); Node.MemoryStruct = FADSKRunEQSMemory::StaticStruct();
		Node.Fields.Add({TEXT("query"), TEXT("Query")}); Node.Fields.Add({TEXT("randomBest25"), TEXT("bRandomBest25")}); Node.Fields.Add({TEXT("pick"), TEXT("Pick")});
		Node.ParamsProperty = GET_MEMBER_NAME_CHECKED(FADSKRunEQSConfig, Params);
		Node.Ports.Add({TEXT("point"), EADSKValueType::Vector, EADSKPortDirection::Out});
		Node.ValidateConfig = [](const void* Value)
		{
			const FADSKRunEQSConfig& Config = *static_cast<const FADSKRunEQSConfig*>(Value);
			return !Config.Query.IsNull() && !(Config.bRandomBest25 && Config.Pick != EADSKQueryPick::Best);
		};
		Node.ValidateAssets = [](const void* Value, FString& OutError)
		{
			const FADSKRunEQSConfig& Config = *static_cast<const FADSKRunEQSConfig*>(Value);
			const UEnvQuery* Query = Cast<UEnvQuery>(Config.Query.ResolveObject());
			if (!Query || Config.Params.IsEmpty()) { return true; }
			const FString Unknown = ADSKRunEQS::FindUnknownParams(*Query, Config.Params);
			if (Unknown.IsEmpty()) { return true; }
			OutError = FString::Printf(TEXT("query '%s' declares no param of that type: %s"), *Config.Query.ToString(), *Unknown);
			return false;
		};
		ADSK::MakeAsyncLeaf(Node, TEXT("RunEQS"), [](FADSKNodeContext& Context, const FADSKAsyncTicket& Ticket)
		{
			const FADSKRunEQSConfig& Config = Context.GetConfig<FADSKRunEQSConfig>();
			UWorld* World = Ticket.GetBrain()->GetWorld();
			const int32 Id = IADSKHostServices::Get(World).RunQuery(*Ticket.GetBrain()->GetHostActor(), *Cast<UEnvQuery>(Config.Query.ResolveObject()), RunMode(Config), Config.Params, [Ticket, NodeId = Context.GetNodeId(), QueryName = Config.Query.GetAssetName()](const FEnvQueryResult& Result)
			{
				TOptional<FADSKValue> Payload; const EADSKNodeStatus Status = ADSKRunEQS::MapResult(Result, Payload);
				if (Payload.IsSet() && Ticket.GetBrain())
				{
					UE_VLOG_LOCATION(Ticket.GetBrain()->GetOwner(), LogAdamskAIHost, Log, Payload->Vector, 30.f, FColor::Yellow, TEXT("%s point"), *NodeId.ToString());
				}
				else if (Ticket.GetBrain())
				{
					// the trace only says failed; the engine's LogEQS entry next to this one has the item counts
					UE_VLOG(Ticket.GetBrain()->GetOwner(), LogAdamskAIHost, Log, TEXT("%s found no point: %s %s"), *NodeId.ToString(), *QueryName,
						Result.IsAborted() ? TEXT("was aborted") : Result.IsSuccessful() ? TEXT("returned no items") : TEXT("failed"));
				}
				Ticket.Complete(Status, MoveTemp(Payload));
			});
			if (Id == INDEX_NONE) { return EADSKNodeStatus::Failed; }
			Context.GetMemory<FADSKRunEQSMemory>().QueryId = Id;
			// A result that already arrived makes SetCancel refuse, so nothing obsolete is installed.
			Ticket.SetCancel([WeakWorld = TWeakObjectPtr<UWorld>(World), Id]() { IADSKHostServices::Get(WeakWorld.Get()).AbortQuery(WeakWorld.Get(), Id); });
			return EADSKNodeStatus::Running;
		}, 0, nullptr, [](FADSKNodeContext& Context)
		{
			// Never loads: the world subsystem preloaded the query with its definition.
			const UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
			return Brain && Brain->GetHostActor() && Cast<UEnvQuery>(Context.GetConfig<FADSKRunEQSConfig>().Query.ResolveObject());
		});
		return Node;
	}

	/** Tags the request Result started for the activation and returns the node status it implies. */
	EADSKNodeStatus BindRequest(UADSKBrainComponent& Brain, AAIController& Controller, FADSKActivationHandle Handle, EPathFollowingRequestResult::Type Result)
	{
		if (Result == EPathFollowingRequestResult::AlreadyAtGoal) { return EADSKNodeStatus::Succeeded; }
		if (Result != EPathFollowingRequestResult::RequestSuccessful) { return EADSKNodeStatus::Failed; }
		const FAIRequestID Id = Controller.GetCurrentMoveRequestID();
		// Aborts only while the controller still owns this exact request.
		Brain.GetAsyncOps().SetCancel(Handle, [WeakBrain = TWeakObjectPtr<UADSKBrainComponent>(&Brain), WeakController = TWeakObjectPtr<AAIController>(&Controller), Id]()
		{
			AAIController* C = WeakController.Get(); UADSKBrainComponent* B = WeakBrain.Get();
			if (C && B && C->GetCurrentMoveRequestID() == Id && C->GetPathFollowingComponent())
			{ C->GetPathFollowingComponent()->AbortMove(*B, FPathFollowingResultFlags::OwnerFinished, Id); }
		});
		return Brain.BindMove(Handle, Id) ? EADSKNodeStatus::Running : EADSKNodeStatus::Failed;
	}

	EADSKNodeStatus RequestMove(UADSKBrainComponent& Brain, AAIController& Controller, FADSKActivationHandle Handle, const FVector& Goal, const FADSKMoveToConfig& Config, FName NodeId)
	{
		UE_VLOG_LOCATION(Brain.GetOwner(), LogAdamskAIHost, Log, Goal, Config.AcceptanceRadius, FColor::Green, TEXT("%s goal"), *NodeId.ToString());
		return BindRequest(Brain, Controller, Handle, Controller.MoveToLocation(Goal, Config.AcceptanceRadius, true, true, false, Config.bStrafe, nullptr, Config.bAllowPartialPath));
	}

	EADSKNodeStatus RequestMoveToActor(UADSKBrainComponent& Brain, AAIController& Controller, FADSKActivationHandle Handle, AActor* Target, const FADSKMoveToActorConfig& Config, FName NodeId)
	{
		UE_VLOG_LOCATION(Brain.GetOwner(), LogAdamskAIHost, Log, Target ? Target->GetActorLocation() : FVector::ZeroVector, Config.AcceptanceRadius, FColor::Green,
			TEXT("%s goal %s"), *NodeId.ToString(), Target ? *Target->GetName() : TEXT("none"));
		return BindRequest(Brain, Controller, Handle, Controller.MoveToActor(Target, Config.AcceptanceRadius, true, true, Config.bStrafe, nullptr, Config.bAllowPartialPath));
	}

	AActor* ReadActor(const FADSKNodeContext& Context, int32 Port, uint64& OutId)
	{
		OutId = 0;
		return Context.ReadObject(Port, OutId) ? FADSKObjectHandles::Resolve<AActor>(OutId) : nullptr;
	}

	FADSKNodeDescriptor MoveToActor()
	{
		FADSKNodeDescriptor Node; Node.Name = TEXT("MoveToActor"); Node.ConfigStruct = FADSKMoveToActorConfig::StaticStruct(); Node.MemoryStruct = FADSKMoveToActorMemory::StaticStruct();
		Node.Fields.Add({TEXT("acceptanceRadius"), TEXT("AcceptanceRadius")}); Node.Fields.Add({TEXT("allowPartialPath"), TEXT("bAllowPartialPath")}); Node.Fields.Add({TEXT("strafe"), TEXT("bStrafe")});
		Node.Ports.Add({TEXT("target"), EADSKValueType::Object, EADSKPortDirection::In});
		Node.ValidateConfig = [](const void* Value) { const float Radius = static_cast<const FADSKMoveToActorConfig*>(Value)->AcceptanceRadius; return FMath::IsFinite(Radius) && Radius >= 0.f; };
		ADSK::MakeAsyncLeaf(Node, TEXT("MoveToActor"), [](FADSKNodeContext& Context, const FADSKAsyncTicket& Ticket)
		{
			uint64 Id; AActor* Target = ReadActor(Context, 0, Id);
			UADSKBrainComponent& Brain = *Ticket.GetBrain(); AAIController& Controller = *Brain.GetAIController();
			Context.GetMemory<FADSKMoveToActorMemory>().Target = Id;
			return RequestMoveToActor(Brain, Controller, Ticket.GetHandle(), Target, Context.GetConfig<FADSKMoveToActorConfig>(), Context.GetNodeId());
		}, INDEX_NONE, nullptr, [](FADSKNodeContext& Context)
		{
			uint64 Id; const UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
			return ReadActor(Context, 0, Id) && Brain && Brain->GetAIController();
		});
		// A destroyed target fails the move; a slot that now names another actor re-targets it.
		Node.OnUpdate = [](FADSKNodeContext& Context, float)
		{
			uint64 Id; UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>(); AActor* Target = ReadActor(Context, 0, Id);
			AAIController* Controller = Brain ? Brain->GetAIController() : nullptr;
			if (!Target || !Controller) { return EADSKNodeStatus::Failed; }
			FADSKMoveToActorMemory& Memory = Context.GetMemory<FADSKMoveToActorMemory>();
			if (Id == Memory.Target) { return EADSKNodeStatus::Running; }
			// The new request aborts the old one, and that abort must not complete this node.
			Brain->UnbindMove(Context.GetHandle());
			Memory.Target = Id;
			return RequestMoveToActor(*Brain, *Controller, Context.GetHandle(), Target, Context.GetConfig<FADSKMoveToActorConfig>(), Context.GetNodeId());
		};
		return Node;
	}

	bool TrackFocusActor(FADSKNodeContext& Context, uint64& OutId)
	{
		UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>(); AAIController* Controller = Brain ? Brain->GetAIController() : nullptr;
		AActor* Target = ReadActor(Context, 0, OutId);
		if (!Controller || !Target) { return false; }
		if (Controller->GetFocusActorForPriority(EAIFocusPriority::Gameplay) != Target)
		{
			Controller->SetFocus(Target, EAIFocusPriority::Gameplay);
			UE_VLOG_SEGMENT(Brain->GetOwner(), LogAdamskAIHost, Log, Brain->GetHostActor() ? Brain->GetHostActor()->GetActorLocation() : Target->GetActorLocation(),
				Target->GetActorLocation(), FColor::Cyan, TEXT("%s focus %s"), *Context.GetNodeId().ToString(), *Target->GetName());
		}
		return true;
	}

	/** Clears the gameplay focus only while it is still the actor this node set, so a later owner keeps it. */
	void ReleaseFocusActor(UADSKBrainComponent* Brain, uint64 Id)
	{
		AAIController* Controller = Brain ? Brain->GetAIController() : nullptr;
		AActor* Target = FADSKObjectHandles::Resolve<AActor>(Id);
		if (Controller && Target && Controller->GetFocusActorForPriority(EAIFocusPriority::Gameplay) == Target) { Controller->ClearFocus(EAIFocusPriority::Gameplay); }
	}

	FADSKNodeDescriptor MoveTo()
	{
		FADSKNodeDescriptor Node; Node.Name = TEXT("MoveTo"); Node.ConfigStruct = FADSKMoveToConfig::StaticStruct(); Node.MemoryStruct = FADSKMoveToMemory::StaticStruct();
		Node.Fields.Add({TEXT("acceptanceRadius"), TEXT("AcceptanceRadius")}); Node.Fields.Add({TEXT("repathDistance"), TEXT("RepathDistance")}); Node.Fields.Add({TEXT("allowPartialPath"), TEXT("bAllowPartialPath")}); Node.Fields.Add({TEXT("strafe"), TEXT("bStrafe")});
		Node.Ports.Add({TEXT("goal"), EADSKValueType::Vector, EADSKPortDirection::In});
		Node.ValidateConfig = [](const void* Value)
		{
			const FADSKMoveToConfig& Config = *static_cast<const FADSKMoveToConfig*>(Value);
			return FMath::IsFinite(Config.AcceptanceRadius) && Config.AcceptanceRadius >= 0.f && FMath::IsFinite(Config.RepathDistance) && Config.RepathDistance >= 0.f;
		};
		ADSK::MakeAsyncLeaf(Node, TEXT("MoveTo"), [](FADSKNodeContext& Context, const FADSKAsyncTicket& Ticket)
		{
			FVector Goal; Context.ReadVector(0, Goal);
			UADSKBrainComponent& Brain = *Ticket.GetBrain();
			Context.GetMemory<FADSKMoveToMemory>().Goal = Goal;
			return RequestMove(Brain, *Brain.GetAIController(), Ticket.GetHandle(), Goal, Context.GetConfig<FADSKMoveToConfig>(), Context.GetNodeId());
		}, INDEX_NONE, nullptr, [](FADSKNodeContext& Context)
		{
			// MoveTo requires an AAIController; brains on ordinary actors fail locally.
			FVector Goal; const UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
			return Brain && Context.ReadVector(0, Goal) && Brain->GetAIController();
		});
		Node.OnUpdate = [](FADSKNodeContext& Context, float)
		{
			const FADSKMoveToConfig& Config = Context.GetConfig<FADSKMoveToConfig>();
			FADSKMoveToMemory& Memory = Context.GetMemory<FADSKMoveToMemory>();
			FVector Goal; UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
			if (Config.RepathDistance <= 0.f || !Brain || !Context.ReadVector(0, Goal) || FVector::DistSquared(Goal, Memory.Goal) <= FMath::Square(Config.RepathDistance))
			{
				return EADSKNodeStatus::Running;
			}
			AAIController* Controller = Brain->GetAIController(); if (!Controller) { return EADSKNodeStatus::Failed; }
			// The new request aborts the old one, and that abort must not complete this node.
			Brain->UnbindMove(Context.GetHandle());
			Memory.Goal = Goal;
			return RequestMove(*Brain, *Controller, Context.GetHandle(), Goal, Config, Context.GetNodeId());
		};
		return Node;
	}
	FADSKNodeDescriptor LookAt()
	{
		FADSKNodeDescriptor Node; Node.Name = TEXT("LookAt"); Node.ConfigStruct = FADSKLookAtConfig::StaticStruct(); Node.MemoryStruct = FADSKLookAtMemory::StaticStruct();
		Node.Fields.Add({TEXT("duration"), TEXT("Duration")}); Node.Ports.Add({TEXT("point"), EADSKValueType::Vector, EADSKPortDirection::In});
		Node.ValidateConfig = [](const void* Value) { const float Duration = static_cast<const FADSKLookAtConfig*>(Value)->Duration; return FMath::IsFinite(Duration) && Duration >= 0.f; };
		Node.OnStart = [](FADSKNodeContext& Context)
		{
			// LookAt requires an AAIController; brains on ordinary actors fail locally.
			FADSKLookAtMemory& Memory = Context.GetMemory<FADSKLookAtMemory>();
			Memory.Elapsed = 0;
			if (!TrackFocalPoint(Context, Memory.Point)) return EADSKNodeStatus::Failed;
			return Context.GetConfig<FADSKLookAtConfig>().Duration == 0 ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Running;
		};
		// The point is re-read every tick, so a moving target slot keeps being faced.
		Node.OnUpdate = [](FADSKNodeContext& Context, float Delta)
		{
			FADSKLookAtMemory& M = Context.GetMemory<FADSKLookAtMemory>();
			if (!TrackFocalPoint(Context, M.Point)) return EADSKNodeStatus::Failed;
			M.Elapsed += Delta;
			return M.Elapsed >= Context.GetConfig<FADSKLookAtConfig>().Duration ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Running;
		};
		Node.OnEnd = [](FADSKNodeContext& Context, EADSKEndReason) { ReleaseFocalPoint(Context.GetHost<UADSKBrainComponent>(), Context.GetMemory<FADSKLookAtMemory>().Point); };
		return Node;
	}

	FADSKNodeDescriptor LookAtActor()
	{
		FADSKNodeDescriptor Node; Node.Name = TEXT("LookAtActor"); Node.ConfigStruct = FADSKLookAtConfig::StaticStruct(); Node.MemoryStruct = FADSKLookAtActorMemory::StaticStruct();
		Node.Fields.Add({TEXT("duration"), TEXT("Duration")}); Node.Ports.Add({TEXT("target"), EADSKValueType::Object, EADSKPortDirection::In});
		Node.ValidateConfig = [](const void* Value) { const float Duration = static_cast<const FADSKLookAtConfig*>(Value)->Duration; return FMath::IsFinite(Duration) && Duration >= 0.f; };
		Node.OnStart = [](FADSKNodeContext& Context)
		{
			FADSKLookAtActorMemory& Memory = Context.GetMemory<FADSKLookAtActorMemory>();
			Memory.Elapsed = 0;
			if (!TrackFocusActor(Context, Memory.Target)) return EADSKNodeStatus::Failed;
			return Context.GetConfig<FADSKLookAtConfig>().Duration == 0 ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Running;
		};
		Node.OnUpdate = [](FADSKNodeContext& Context, float Delta)
		{
			FADSKLookAtActorMemory& Memory = Context.GetMemory<FADSKLookAtActorMemory>();
			if (!TrackFocusActor(Context, Memory.Target)) return EADSKNodeStatus::Failed;
			Memory.Elapsed += Delta;
			return Memory.Elapsed >= Context.GetConfig<FADSKLookAtConfig>().Duration ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Running;
		};
		Node.OnEnd = [](FADSKNodeContext& Context, EADSKEndReason) { ReleaseFocusActor(Context.GetHost<UADSKBrainComponent>(), Context.GetMemory<FADSKLookAtActorMemory>().Target); };
		return Node;
	}

	FADSKNodeDescriptor Focus()
	{
		FADSKNodeDescriptor Node; Node.Name = TEXT("Focus"); Node.Kind = EADSKNodeKind::Scope; Node.MemoryStruct = FADSKFocusMemory::StaticStruct();
		Node.MinChildren = 1; Node.MaxChildren = 1;
		Node.Ports.Add({TEXT("point"), EADSKValueType::Vector, EADSKPortDirection::In});
		Node.OnStart = [](FADSKNodeContext& Context) { return TrackFocalPoint(Context, Context.GetMemory<FADSKFocusMemory>().Point) ? EADSKNodeStatus::Running : EADSKNodeStatus::Failed; };
		Node.OnUpdate = [](FADSKNodeContext& Context, float) { return TrackFocalPoint(Context, Context.GetMemory<FADSKFocusMemory>().Point) ? EADSKNodeStatus::Running : EADSKNodeStatus::Failed; };
		Node.OnEnd = [](FADSKNodeContext& Context, EADSKEndReason) { ReleaseFocalPoint(Context.GetHost<UADSKBrainComponent>(), Context.GetMemory<FADSKFocusMemory>().Point); };
		return Node;
	}

	FADSKNodeDescriptor FocusActor()
	{
		FADSKNodeDescriptor Node; Node.Name = TEXT("FocusActor"); Node.Kind = EADSKNodeKind::Scope; Node.MemoryStruct = FADSKFocusActorMemory::StaticStruct();
		Node.MinChildren = 1; Node.MaxChildren = 1;
		Node.Ports.Add({TEXT("target"), EADSKValueType::Object, EADSKPortDirection::In});
		Node.OnStart = [](FADSKNodeContext& Context) { return TrackFocusActor(Context, Context.GetMemory<FADSKFocusActorMemory>().Target) ? EADSKNodeStatus::Running : EADSKNodeStatus::Failed; };
		Node.OnUpdate = [](FADSKNodeContext& Context, float) { return TrackFocusActor(Context, Context.GetMemory<FADSKFocusActorMemory>().Target) ? EADSKNodeStatus::Running : EADSKNodeStatus::Failed; };
		Node.OnEnd = [](FADSKNodeContext& Context, EADSKEndReason) { ReleaseFocusActor(Context.GetHost<UADSKBrainComponent>(), Context.GetMemory<FADSKFocusActorMemory>().Target); };
		return Node;
	}

	FADSKNodeDescriptor MoveSpeed()
	{
		FADSKNodeDescriptor Node; Node.Name = TEXT("MoveSpeed"); Node.Kind = EADSKNodeKind::Scope; Node.ConfigStruct = FADSKMoveSpeedConfig::StaticStruct(); Node.MemoryStruct = FADSKMoveSpeedMemory::StaticStruct();
		Node.MinChildren = 1; Node.MaxChildren = 1;
		Node.Fields.Add({TEXT("speed"), TEXT("Speed")});
		Node.ValidateConfig = [](const void* Value) { const float Speed = static_cast<const FADSKMoveSpeedConfig*>(Value)->Speed; return FMath::IsFinite(Speed) && Speed >= 0.f; };
		Node.OnStart = [](FADSKNodeContext& Context)
		{
			// MoveSpeed requires an ACharacter host; other pawns and actors fail locally.
			UCharacterMovementComponent* Movement = FindCharacterMovement(Context);
			if (!Movement) { return EADSKNodeStatus::Failed; }
			Context.GetMemory<FADSKMoveSpeedMemory>().Previous = Movement->MaxWalkSpeed;
			Movement->MaxWalkSpeed = Context.GetConfig<FADSKMoveSpeedConfig>().Speed;
			return EADSKNodeStatus::Running;
		};
		Node.OnEnd = [](FADSKNodeContext& Context, EADSKEndReason)
		{
			UCharacterMovementComponent* Movement = FindCharacterMovement(Context);
			if (Movement && Movement->MaxWalkSpeed == Context.GetConfig<FADSKMoveSpeedConfig>().Speed) { Movement->MaxWalkSpeed = Context.GetMemory<FADSKMoveSpeedMemory>().Previous; }
		};
		return Node;
	}
}

void FADSKHostNodes::RegisterAll(FADSKNodeRegistry& Registry)
{
	using namespace ADSKHostNodesPrivate;
	static const TMap<FString, const TCHAR*> Guidance = {
		{TEXT("MoveTo"), TEXT("Walk to a point the tree or host computed; `repathDistance` follows a goal slot that keeps moving.\n")
			TEXT("To follow an actor, `MoveToActor` re-reads its location for you.\n")
			TEXT("Wrap it in `TimeLimit`; when it does not move, recipes/why-does-it-not-move.md walks through the causes.")},
		{TEXT("MoveToActor"), TEXT("Chase or approach an actor held in an Object slot; `acceptanceRadius` counts both capsules.\n")
			TEXT("It fails when the target is gone: handle tree.failure, or publish a new target and let it re-target.")},
		{TEXT("LookAt"), TEXT("Face a point for a fixed time, then let go.\n")
			TEXT("To keep facing it while doing something else, wrap that in `Focus` instead.")},
		{TEXT("LookAtActor"), TEXT("Face an actor for a fixed time, then let go.\n")
			TEXT("To keep facing it while doing something else, wrap that in `FocusActor` instead.")},
		{TEXT("Focus"), TEXT("Keep facing a point during a whole subtree: aim while moving.\n")
			TEXT("Focus is cleared when the child ends, cancelled included.")},
		{TEXT("FocusActor"), TEXT("Keep facing an actor during a whole subtree: watch the player while retreating.\n")
			TEXT("It fails when the actor is gone, which ends the child too.")},
		{TEXT("MoveSpeed"), TEXT("Change walk speed for a subtree (sprint while fleeing); the previous speed comes back when it ends, cancelled included.\n")
			TEXT("It needs a Character host.")},
		{TEXT("RunEQS"), TEXT("Pick a point (cover, flank, a spot near the target) and hand it to a following `MoveTo` through `out-point`.\n")
			TEXT("It fails when the query finds nothing: give that failure somewhere to go that changes something, or the state retries the same query forever.\n")
			TEXT("To see why it found nothing, record the Visual Logger: the agent's track has `found no point` next to the engine's LogEQS entry with `All Items` and `ValidItems`.\n")
			TEXT("It runs the query as the pawn; a query context that casts its querier to the controller must accept the pawn too.\n")
			TEXT("`<Param>` children set the query's named parameters; a name the query does not declare is a validation error.")},
	};
	const auto Described = [](FADSKNodeDescriptor Node, const TCHAR* Description)
	{
		Node.Description = Description;
		const TCHAR* const* Found = Guidance.Find(Node.Name);
		Node.Guidance = Found ? *Found : TEXT("");
		return Node;
	};
	FADSKNodeDescriptor Eqs = RunEQS();
	for (FADSKFieldMap& Field : Eqs.Fields) Field.bLegacy = Field.bLegacy || Field.XmlName == TEXT("randomBest25");
	// Register outside the assertion: check() and its argument compile out of Shipping and Test.
	const EADSKRegisterResult Results[] = {
		Registry.Register(Described(MoveTo(), TEXT("Path-follows the pawn to `in-goal` within `acceptanceRadius`, re-pathing when the slot moves farther than `repathDistance` (0: never). Succeeds on arrival; with `allowPartialPath` (default) a goal no path reaches is approached as far as possible and the move succeeds there, without it the move fails. A goal off the navmesh fails, and so does a brain without an AI controller."))),
		Registry.Register(Described(LookAt(), TEXT("Turns the controller's focus to `in-point` (re-read every tick) for `duration` seconds, then succeeds and clears it. Fails without an AI controller."))),
		Registry.Register(Described(MoveTemp(Eqs), TEXT("Runs the EQS `query` (with `<Param>` children) as the pawn and writes the chosen item's location to `out-point`. `pick` chooses Best, RandomBest5 or RandomBest25. Succeeds with an item, fails when the query returns none."))),
		Registry.Register(Described(Focus(), TEXT("Keeps the controller's focus on `in-point` (re-read every tick) while its one child runs, then clears it. Ends with the child; fails without an AI controller."))),
		Registry.Register(Described(MoveSpeed(), TEXT("Sets the character's walk speed to `speed` while its one child runs and restores the previous speed after. Ends with the child; fails on a host that is not a character."))),
		Registry.Register(Described(MoveToActor(), TEXT("Path-follows the actor in `in-target` (an Object) within `acceptanceRadius`, which counts both capsules, re-targeting when the slot names another actor. Fails when the target is none or destroyed; `allowPartialPath` as in MoveTo."))),
		Registry.Register(Described(LookAtActor(), TEXT("Turns the controller's focus to the actor in `in-target` for `duration` seconds, then succeeds and clears it. Fails when the actor is gone."))),
		Registry.Register(Described(FocusActor(), TEXT("Keeps the controller's focus on the actor in `in-target` while its one child runs, then clears it. Ends with the child; fails when the actor is gone.")))};
	for (const EADSKRegisterResult Result : Results)
	{
		checkf(Result == EADSKRegisterResult::Accepted, TEXT("host node registration failed: %s"), LexToString(Result));
	}
	RegisterSensors(Registry);
	RegisterActions(Registry);
	RegisterGroups(Registry);
}

EADSKNodeStatus ADSKRunEQS::MapResult(const FEnvQueryResult& Result, TOptional<FADSKValue>& OutPayload)
{
	OutPayload.Reset();
	if (!Result.IsSuccessful() || !Result.Items.IsValidIndex(0) || !Result.Items[0].IsValid()) { return EADSKNodeStatus::Failed; }
	OutPayload = FADSKValue::MakeVector(Result.GetItemAsLocation(0));
	return EADSKNodeStatus::Succeeded;
}

FString ADSKRunEQS::FindUnknownParams(const UEnvQuery& Query, TConstArrayView<FADSKNamedParam> Params)
{
	TArray<FAIDynamicParam> Declared;
	// The owner only scopes blackboard-key bindings; the query itself is a valid stand-in.
	Query.CollectQueryParams(const_cast<UEnvQuery&>(Query), Declared);
	FString Unknown;
	for (const FADSKNamedParam& Param : Params)
	{
		const EAIParamType Expected = Param.Type == EADSKParamType::Int ? EAIParamType::Int : Param.Type == EADSKParamType::Bool ? EAIParamType::Bool : EAIParamType::Float;
		const bool bDeclared = Declared.ContainsByPredicate([&Param, Expected](const FAIDynamicParam& D) { return D.ParamName == Param.Name && D.ParamType == Expected; });
		if (!bDeclared) { Unknown += FString::Printf(TEXT("%s'%s'"), Unknown.IsEmpty() ? TEXT("") : TEXT(", "), *Param.Name.ToString()); }
	}
	return Unknown;
}
