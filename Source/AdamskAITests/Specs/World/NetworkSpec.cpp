// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#if defined(WITH_GAMEPLAY_DEBUGGER_MENU) && WITH_GAMEPLAY_DEBUGGER_MENU
#include "GameplayDebuggerCategory.h"
#include "GameplayDebuggerCategoryReplicator.h"
#include "GameplayDebuggerPlayerManager.h"
#endif
#include "Misc/StringOutputDevice.h"
#include "NativeGameplayTags.h"
#include "Settings/ADSKSettings.h"
#include "Support/TestChecks.h"
#include "UObject/UObjectIterator.h"
#include "World/WorldScenario.h"
#include "World/WorldTestActors.h"
#include "CQTest.h"

// A listen server with one client. The brain lives on the AI controller, which no client ever gets, so only the
// server runs behavior; the client sees the replicated pawn and nothing of AdamskAI.
namespace FNetworkSpecPrivate
{
	struct FState : FBasePIENetworkComponentState
	{
		AADSKWorldTestPawn* Pawn = nullptr;
		AADSKWorldBrainPawn* BrainPawn = nullptr;
	};

	const TCHAR* PrewarmFile = TEXT("net_prewarm.xml");

	const TCHAR* TaggedXml = TEXT("<Agent version=\"1\" id=\"net_tags\"><Events><Event name=\"Calm\"/></Events><StateMachine id=\"m\" initial=\"combat\">")
		TEXT("<State id=\"combat\" tree=\"t\" tags=\"AdamskAI.Test.NetCombat\"><On event=\"Calm\" goto=\"calm\"/></State><State id=\"calm\" tree=\"t\"/></StateMachine>")
		TEXT("<Tree id=\"t\"><Wait id=\"hold\" duration=\"100\"/></Tree></Agent>");

	/** Restores the settings a test changes. */
	struct FSettingsGuard
	{
		TArray<FString> PrewarmFiles = GetDefault<UADSKSettings>()->PrewarmFiles;
		bool bPrewarmOnClients = GetDefault<UADSKSettings>()->bPrewarmOnClients;
		~FSettingsGuard()
		{
			GetMutableDefault<UADSKSettings>()->PrewarmFiles = PrewarmFiles;
			GetMutableDefault<UADSKSettings>()->bPrewarmOnClients = bPrewarmOnClients;
		}
	};

	UADSKWorldSubsystem& WorldSubsystem(UWorld* World) { return *World->GetSubsystem<UADSKWorldSubsystem>(); }

	FString Run(UWorld* World, const TCHAR* Command)
	{
		FStringOutputDevice Output;
		ADSKTest::Expect(GEngine->Exec(World, Command, Output), FString(TEXT("no console command handled: ")) + Command);
		return Output;
	}

	const TCHAR* const HoldXml = TEXT("<Agent version=\"1\" id=\"net\"><StateMachine id=\"m\" initial=\"w\"><State id=\"w\" tree=\"t\"/></StateMachine>"
		"<Tree id=\"t\"><Wait id=\"hold\" duration=\"100\"/></Tree></Agent>");

	int32 CountBrains(const UWorld* World, bool bRunningOnly)
	{
		int32 Count = 0;
		for (TObjectIterator<UADSKBrainComponent> It; It; ++It)
		{
			Count += It->GetWorld() == World && (!bRunningOnly || It->IsADSKRunning()) ? 1 : 0;
		}
		return Count;
	}

	int32 CountControllers(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AADSKWorldTestController> It(World); It; ++It) { ++Count; }
		return Count;
	}
}

using namespace FNetworkSpecPrivate;

NETWORK_TEST_CLASS(FWorldNetworkSpec, "AdamskAIWorld.Network")
{
	FPIENetworkComponent<FState> Network{TestRunner, TestCommandBuilder, bInitializing};
	FADSKWorldScenario Files;
	TUniquePtr<FSettingsGuard> Settings;
	FString Source;
	AADSKWorldTestController* Controller = nullptr;
	/** Native tags must come from a runtime module, so it is filed under the plugin's runtime host module. */
	FNativeGameplayTag NetCombat{TEXT("AdamskAI"), TEXT("AdamskAIHost"), TEXT("AdamskAI.Test.NetCombat"), TEXT("AdamskAI test tag"), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD};

	BEFORE_EACH()
	{
		// The settings are read when the worlds are created, which the component does in a later latent command.
		Settings = MakeUnique<FSettingsGuard>();
		Source = Files.WriteBehavior(PrewarmFile, HoldXml);
		GetMutableDefault<UADSKSettings>()->PrewarmFiles.Add(Source);
		FNetworkComponentBuilder<FState>()
			.AsListenServer()
			.WithClients(1)
			.WithGameMode(AGameModeBase::StaticClass())
			.Build(Network);
	}

	AFTER_EACH()
	{
		Settings.Reset();
	}

	void SpawnServerBrain()
	{
		Network.SpawnAndReplicate<AADSKWorldTestPawn, &FState::Pawn>()
			.ThenServer(TEXT("possess and start"), [this](FState& Server)
			{
				Controller = Server.World->SpawnActor<AADSKWorldTestController>();
				ASSERT_THAT(IsNotNull(Controller));
				Controller->Possess(Server.Pawn);
				Controller->Brain->InitializeDefinition(ADSKTest::LoadOrStandIn(TEXT("net.xml"), HoldXml, ADSKTest::PluginCatalogue()));
				Controller->Brain->StartLogic();
				ASSERT_THAT(IsTrue(Controller->Brain->IsADSKRunning(), *Controller->Brain->GetLastError()));
			});
	}

	/** The server compiles the prewarm list, the client compiles nothing (it runs no server brains). */
	TEST_METHOD(OnlyTheServerPrewarms)
	{
		Network.UntilServer(TEXT("server prewarmed"), [this](FState& Server) { return !WorldSubsystem(Server.World).IsPrewarming(Source); })
			.ThenServer(TEXT("published on the server"), [this](FState& Server)
			{
				ASSERT_THAT(IsTrue(WorldSubsystem(Server.World).GetActiveDefinition(Source).IsValid(), TEXT("the server did not prewarm ") + Source));
			})
			.ThenClients(TEXT("nothing on the client"), [this](FState& Client)
			{
				ASSERT_THAT(IsFalse(WorldSubsystem(Client.World).IsPrewarming(Source), TEXT("the client is prewarming")));
				ASSERT_THAT(IsFalse(WorldSubsystem(Client.World).GetActiveDefinition(Source).IsValid(), TEXT("the client compiled ") + Source));
			});
	}

	/** A brain on the replicated pawn itself runs on the server; the client's copy is refused and never ticks. */
	TEST_METHOD(ABrainOnAReplicatedPawnRunsOnlyOnTheServer)
	{
		Network.SpawnAndReplicate<AADSKWorldBrainPawn, &FState::BrainPawn>()
			.ThenServer(TEXT("start on the server"), [this](FState& Server)
			{
				UADSKBrainComponent* Brain = Server.BrainPawn->Brain;
				Brain->InitializeDefinition(ADSKTest::LoadOrStandIn(TEXT("net.xml"), HoldXml, ADSKTest::PluginCatalogue()));
				Brain->StartLogic();
				ASSERT_THAT(IsTrue(Brain->IsADSKRunning(), *Brain->GetLastError()));
				ASSERT_THAT(IsTrue(Brain->IsComponentTickEnabled(), TEXT("the server's bAutoTick brain does not tick")));
			})
			.UntilServer(TEXT("the server brain has ticked"), [](FState& Server)
			{
				return Server.BrainPawn->Brain->GetInstance() && Server.BrainPawn->Brain->GetInstance()->GetTickCount() > 2;
			})
			.ThenClients(TEXT("the client's copy is refused"), [this](FState& Client)
			{
				ASSERT_THAT(IsNotNull(Client.BrainPawn, TEXT("the pawn did not replicate")));
				UADSKBrainComponent* Brain = Client.BrainPawn->Brain;
				ASSERT_THAT(IsFalse(Brain->IsComponentTickEnabled(), TEXT("the client's bAutoTick brain ticks")));
				TestRunner->AddExpectedError(TEXT("refused on a copy without authority"), EAutomationExpectedErrorFlags::Contains, 1);
				Brain->InitializeDefinition(ADSKTest::LoadOrStandIn(TEXT("net.xml"), HoldXml, ADSKTest::PluginCatalogue()));
				Brain->StartLogic();
				ASSERT_THAT(IsFalse(Brain->IsADSKRunning(), TEXT("the client's copy started")));
				ASSERT_THAT(AreEqual(FString(TEXT("not-authority")), Brain->GetLastError()));
			});
	}

	/** The tags of the server brain's active states reach the client through the pawn's replicated container. */
	TEST_METHOD(StateTagsReachTheClient)
	{
		Network.SpawnAndReplicate<AADSKWorldBrainPawn, &FState::BrainPawn>()
			.ThenServer(TEXT("start a tagged behavior"), [this](FState& Server)
			{
				UADSKBrainComponent* Brain = Server.BrainPawn->Brain;
				Brain->InitializeDefinition(ADSKTest::LoadOrStandIn(TEXT("net_tags.xml"), TaggedXml, ADSKTest::PluginCatalogue()));
				Brain->StartLogic();
				ASSERT_THAT(IsTrue(Brain->IsADSKRunning(), *Brain->GetLastError()));
			})
			.UntilClients(TEXT("the combat tag arrives"), [](FState& Client)
			{
				return Client.BrainPawn && Client.BrainPawn->StateTags.HasTagExact(FGameplayTag::RequestGameplayTag(TEXT("AdamskAI.Test.NetCombat")));
			})
			.ThenServer(TEXT("calm down"), [](FState& Server) { Server.BrainPawn->Brain->EnqueueEvent(TEXT("Calm")); })
			.UntilClients(TEXT("the tags clear"), [](FState& Client) { return Client.BrainPawn->StateTags.IsEmpty(); });
	}

	/** adsk.Status in the client world says where the brains are; in the server world it lists the brain. */
	TEST_METHOD(StatusInTheClientWorldPointsToTheServer)
	{
		SpawnServerBrain();
		Network.UntilServer(TEXT("the brain has ticked"), [this](FState&)
			{
				return Controller->Brain->GetInstance() && Controller->Brain->GetInstance()->GetActiveNodeId() == FName(TEXT("hold"));
			})
			.ThenServer(TEXT("listed on the server"), [this](FState& Server)
			{
				const FString Status = Run(Server.World, TEXT("adsk.Status"));
				ASSERT_THAT(IsTrue(Status.Contains(TEXT("1 brain(s)")) && !Status.Contains(TEXT("client world")), TEXT("server adsk.Status:\n") + Status));
			})
			.ThenClients(TEXT("the hint on the client"), [this](FState& Client)
			{
				const FString Status = Run(Client.World, TEXT("adsk.Status"));
				ASSERT_THAT(IsTrue(Status.Contains(TEXT("0 brain(s)")) && Status.Contains(TEXT("client world: brains run on the server")), TEXT("client adsk.Status:\n") + Status));
				const FString Restart = Run(Client.World, TEXT("adsk.Restart all"));
				ASSERT_THAT(IsTrue(Restart.Contains(TEXT("client world")), TEXT("client adsk.Restart:\n") + Restart));
			});
	}

	TEST_METHOD(OnlyTheServerRunsTheBrain)
	{
		Network.SpawnAndReplicate<AADSKWorldTestPawn, &FState::Pawn>()
			.ThenServer(TEXT("possess and start"), [this](FState& Server)
			{
				Controller = Server.World->SpawnActor<AADSKWorldTestController>();
				ASSERT_THAT(IsNotNull(Controller));
				Controller->Possess(Server.Pawn);
				Controller->Brain->InitializeDefinition(ADSKTest::LoadOrStandIn(TEXT("net.xml"), HoldXml, ADSKTest::PluginCatalogue()));
				Controller->Brain->StartLogic();
				ASSERT_THAT(IsTrue(Controller->Brain->IsADSKRunning(), *Controller->Brain->GetLastError()));
			})
			.UntilServer(TEXT("the brain has ticked"), [this](FState&)
			{
				return Controller->Brain->GetInstance() && Controller->Brain->GetInstance()->GetActiveNodeId() == FName(TEXT("hold"));
			})
			.ThenServer(TEXT("one brain, on the server"), [this](FState& Server)
			{
				ASSERT_THAT(AreEqual(1, CountBrains(Server.World, true)));
				ASSERT_THAT(AreEqual(1, Server.World->GetSubsystem<UADSKWorldSubsystem>()->GetWorldBrains().Num()));
			})
			.ThenClients(TEXT("none on the client"), [this](FState& Client)
			{
				ASSERT_THAT(IsNotNull(Client.Pawn, TEXT("the pawn did not replicate")));
				ASSERT_THAT(AreEqual(0, CountControllers(Client.World), TEXT("an AI controller reached the client")));
				ASSERT_THAT(AreEqual(0, CountBrains(Client.World, false), TEXT("a brain exists in the client world")));
				const UADSKWorldSubsystem* Subsystem = Client.World->GetSubsystem<UADSKWorldSubsystem>();
				ASSERT_THAT(IsTrue(!Subsystem || Subsystem->GetWorldBrains().IsEmpty()));
			});
	}
};

// Prewarming with bPrewarmOnClients. CQTest runs a test's body after BEFORE_EACH's latent commands, so settings the
// worlds read at creation are set here, not in the body.
NETWORK_TEST_CLASS(FWorldNetworkPrewarmOnClientsSpec, "AdamskAIWorld.Network.PrewarmOnClients")
{
	FPIENetworkComponent<FState> Network{TestRunner, TestCommandBuilder, bInitializing};
	FADSKWorldScenario Files;
	TUniquePtr<FSettingsGuard> Settings;
	FString Source;

	BEFORE_EACH()
	{
		Settings = MakeUnique<FSettingsGuard>();
		Source = Files.WriteBehavior(PrewarmFile, HoldXml);
		GetMutableDefault<UADSKSettings>()->PrewarmFiles.Add(Source);
		GetMutableDefault<UADSKSettings>()->bPrewarmOnClients = true;
		FNetworkComponentBuilder<FState>()
			.AsListenServer()
			.WithClients(1)
			.WithGameMode(AGameModeBase::StaticClass())
			.Build(Network);
	}

	AFTER_EACH()
	{
		Settings.Reset();
	}

	TEST_METHOD(BothWorldsCompileTheFile)
	{
		Network.UntilServer(TEXT("server prewarmed"), [this](FState& Server) { return !WorldSubsystem(Server.World).IsPrewarming(Source); })
			.UntilClients(TEXT("client prewarmed"), [this](FState& Client) { return !WorldSubsystem(Client.World).IsPrewarming(Source); })
			.ThenServer(TEXT("published on the server"), [this](FState& Server)
			{
				ASSERT_THAT(IsTrue(WorldSubsystem(Server.World).GetActiveDefinition(Source).IsValid(), TEXT("the server did not prewarm ") + Source));
			})
			.ThenClients(TEXT("published on the client"), [this](FState& Client)
			{
				ASSERT_THAT(IsTrue(WorldSubsystem(Client.World).GetActiveDefinition(Source).IsValid(), TEXT("the client did not prewarm ") + Source));
			});
	}
};

namespace FNetworkSpecPrivate
{
	/** Far from the origin, where the game mode puts the client's first pawn in a map with no player start. */
	const FVector ViewerSpot(20000.0, 0.0, 0.0);

	/** A pawn that floats: the component's new map has no floor. */
	template <typename TPawn>
	TPawn* SpawnFloating(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TPawn* Pawn = World->SpawnActor<TPawn>(Location, FRotator::ZeroRotator, Parameters);
		if (Pawn) { Pawn->GetCharacterMovement()->GravityScale = 0.f; Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Flying); }
		return Pawn;
	}

	AADSKWorldTestController* SpawnAgent(UWorld* World, const FVector& Location, UClass* ControllerClass)
	{
		AADSKWorldTestPawn* Pawn = SpawnFloating<AADSKWorldTestPawn>(World, Location);
		AADSKWorldTestController* Agent = World->SpawnActor<AADSKWorldTestController>(ControllerClass, Location, FRotator::ZeroRotator);
		if (!Pawn || !Agent) return nullptr;
		Agent->Possess(Pawn);
		Agent->Brain->InitializeDefinition(ADSKTest::LoadOrStandIn(TEXT("net.xml"), HoldXml, ADSKTest::PluginCatalogue()));
		Agent->Brain->StartLogic();
		return Agent;
	}

	uint64 Ticks(const AADSKWorldTestController* Agent)
	{
		return Agent && Agent->Brain->GetInstance() ? Agent->Brain->GetInstance()->GetTickCount() : 0;
	}
}

// A dedicated server with one remote client. The server's only view is that client's.
NETWORK_TEST_CLASS(FWorldDedicatedServerSpec, "AdamskAIWorld.Network.Dedicated")
{
	FPIENetworkComponent<FState> Network{TestRunner, TestCommandBuilder, bInitializing};
	TArray<FADSKTickLODLevel> SavedLOD;
	AADSKWorldTestController* Near = nullptr;
	AADSKWorldTestController* Far = nullptr;
	uint64 NearMark = 0, FarMark = 0;
	double Mark = 0.0;

	BEFORE_EACH()
	{
		SavedLOD = GetDefault<UADSKSettings>()->TickLOD;
		FNetworkComponentBuilder<FState>()
			.AsDedicatedServer()
			.WithClients(1)
			.WithGameMode(AGameModeBase::StaticClass())
			.Build(Network);
	}

	AFTER_EACH()
	{
		GetMutableDefault<UADSKSettings>()->TickLOD = SavedLOD;
	}

	/** Gives the remote client a pawn at ViewerSpot and starts a managed agent 3 m and one 60 m from it. */
	void SpawnViewerAndAgents()
	{
		Network.ThenServer(TEXT("viewer pawn and two agents"), [this](FState& Server)
			{
				ASSERT_THAT(AreEqual(1, Server.ClientConnections.Num()));
				APlayerController* Remote = Server.ClientConnections[0]->PlayerController;
				ASSERT_THAT(IsNotNull(Remote, TEXT("the client has no controller on the server")));
				AADSKWorldTestPawn* Viewer = SpawnFloating<AADSKWorldTestPawn>(Server.World, ViewerSpot);
				ASSERT_THAT(IsNotNull(Viewer));
				Remote->Possess(Viewer);
				Near = SpawnAgent(Server.World, ViewerSpot + FVector(300.0, 0.0, 0.0), AADSKWorldManagedController::StaticClass());
				Far = SpawnAgent(Server.World, ViewerSpot + FVector(6000.0, 0.0, 0.0), AADSKWorldManagedController::StaticClass());
				ASSERT_THAT(IsTrue(Near && Far && Near->Brain->IsADSKRunning() && Far->Brain->IsADSKRunning(), TEXT("an agent did not start")));
			});
	}

	/** On a dedicated server the LOD distance is measured from the remote client's view. */
	TEST_METHOD(ManagedLodFollowsTheRemoteClientsView)
	{
		GetMutableDefault<UADSKSettings>()->TickLOD = {{2000.f, 0.5f}};
		SpawnViewerAndAgents();
		Network.ThenServer(TEXT("mark"), [this](FState& Server) { NearMark = Ticks(Near); FarMark = Ticks(Far); Mark = Server.World->GetTimeSeconds(); })
			.UntilServer(TEXT("two seconds"), [this](FState& Server) { return Server.World->GetTimeSeconds() - Mark >= 2.0; })
			.ThenServer(TEXT("near every frame, far at 2 Hz"), [this](FState& Server)
			{
				const double Elapsed = Server.World->GetTimeSeconds() - Mark;
				const uint64 NearTicks = Ticks(Near) - NearMark, FarTicks = Ticks(Far) - FarMark;
				ASSERT_THAT(IsTrue(FarTicks >= 2 && FarTicks <= 6, FString::Printf(TEXT("far ticked %llu times in %.2f s, expected about 4"), FarTicks, Elapsed)));
				ASSERT_THAT(IsTrue(NearTicks > FarTicks * 4, FString::Printf(TEXT("near ticked %llu times in %.2f s, far %llu"), NearTicks, Elapsed, FarTicks)));
			});
	}

#if defined(WITH_GAMEPLAY_DEBUGGER_MENU) && WITH_GAMEPLAY_DEBUGGER_MENU
	/** The server collects the AdamskAI category for the remote client's debug actor, and the lines reach the client. */
	TEST_METHOD(GameplayDebuggerLinesReachTheClient)
	{
		SpawnViewerAndAgents();
		Network.ThenServer(TEXT("debug the near agent"), [this](FState& Server)
			{
				APlayerController* Remote = Server.ClientConnections[0]->PlayerController;
				AGameplayDebuggerPlayerManager& Manager = AGameplayDebuggerPlayerManager::GetCurrent(Server.World);
				Manager.UpdateAuthReplicators();
				AGameplayDebuggerCategoryReplicator* Replicator = Manager.GetReplicator(*Remote);
				ASSERT_THAT(IsNotNull(Replicator, TEXT("no Gameplay Debugger replicator for the remote client")));
				const int32 Category = FindCategory(*Replicator);
				ASSERT_THAT(IsTrue(Category != INDEX_NONE, TEXT("no AdamskAI category")));
				Replicator->SetEnabled(true);
				Replicator->SetCategoryEnabled(Category, true);
				Replicator->SetDebugActor(Near->GetPawn());
			})
			.UntilClients(TEXT("the lines arrive"), [this](FState& Client)
			{
				return ClientLines(Client).Contains(TEXT("m/w"));
			})
			.ThenClients(TEXT("they describe the agent"), [this](FState& Client)
			{
				const FString Lines = ClientLines(Client);
				ASSERT_THAT(IsTrue(Lines.Contains(TEXT("hold")), TEXT("client lines:\n") + Lines));
			});
	}

	static int32 FindCategory(const AGameplayDebuggerCategoryReplicator& Replicator)
	{
		for (int32 Index = 0; Index < Replicator.GetNumCategories(); ++Index)
		{
			if (Replicator.GetCategory(Index)->GetCategoryName() == FName(TEXT("AdamskAI"))) return Index;
		}
		return INDEX_NONE;
	}

	static FString ClientLines(FState& Client)
	{
		APlayerController* Local = Client.World->GetFirstPlayerController();
		AGameplayDebuggerCategoryReplicator* Replicator = Local ? AGameplayDebuggerPlayerManager::GetCurrent(Client.World).GetReplicator(*Local) : nullptr;
		const int32 Category = Replicator ? FindCategory(*Replicator) : INDEX_NONE;
		return Category == INDEX_NONE ? FString() : FString::Join(Replicator->GetCategory(Category)->GetReplicatedLinesCopy(), TEXT("\n"));
	}
#endif
};

#endif // ENABLE_PIE_NETWORK_TEST
#endif
