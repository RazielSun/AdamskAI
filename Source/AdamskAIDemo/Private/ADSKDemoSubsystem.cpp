// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "ADSKDemoSubsystem.h"

#include "ADSKDemoActors.h"
#include "ADSKDemoLayout.h"
#include "ADSKDemoModule.h"
#include "Algo/Find.h"
#include "Brain/ADSKBrainComponent.h"
#include "Engine/Engine.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Groups/ADSKGroupSubsystem.h"
#include "Misc/App.h"
#include "Settings/ADSKSettings.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Objects/ADSKObjectHandles.h"
#include "Runtime/ADSKInstance.h"
#include "Sources/ADSKBehaviorRoots.h"

namespace ADSKDemoSubsystemPrivate
{
	using FSetup = bool (UADSKDemoSubsystem::*)(FString&);

	struct FDemoScene
	{
		const TCHAR* Name;
		const TCHAR* Hint;
	};

	/** Name and the hint shown while it runs; StartScene maps each name to its setup. */
	const FDemoScene Scenes[] = {
		{TEXT("Patrol"), TEXT("Three guards walk the square, each from another corner; one stands a timed watch at the post and walks back.")},
		{TEXT("Chase"), TEXT("Guards walk their beat; one that sees the sphere chases it, strikes when close, and searches where it was last seen when it slips away.")},
		{TEXT("Interrupt"), TEXT("One tree, three options: back off from a close target, shout whenever the 5 s cooldown is ready, else patrol. The node label switches without leaving the state.")},
		{TEXT("Squad"), TEXT("Four guards watch from their posts. The first to see the sphere radios where; the others go there half a second later and look around.")},
		{TEXT("Tokens"), TEXT("Six attackers around a target: HoldToken lets two strike at a time (white flash); the others circle (purple) and try again.")},
		{TEXT("Fleet"), TEXT("500 brains of core nodes on the managed tick. adsk.Demo.Fleet <count> [lod] [parallel] [norecorder] [color] tries others.")},
		{TEXT("Reload"), TEXT("The Chase scene from a copy of its file: edit the copy while it runs (a wait, a radius, a state); the agents pick it up and keep where they are.")},
	};

	/** Message keys for GEngine's on-screen text, chosen not to collide with the engine's own. */
	constexpr int32 FirstMessageKey = 0x4144534B;

	void EnableLabels()
	{
		if (IConsoleVariable* Labels = IConsoleManager::Get().FindConsoleVariable(TEXT("adsk.Labels")))
		{
			Labels->Set(true, ECVF_SetByCode);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs DemoCommand(TEXT("adsk.Demo"), TEXT("adsk.Demo <Scene>: start an AdamskAI demo scene (adsk.Demo.List names them)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UADSKDemoSubsystem* Demo = UADSKDemoSubsystem::Get(World);
			if (!Demo || Args.IsEmpty())
			{
				UE_LOG(LogAdamskAIDemo, Display, TEXT("adsk.Demo <Scene>: %s"), *FString::Join(UADSKDemoSubsystem::GetSceneNames(), TEXT(", ")));
				return;
			}
			FString Error;
			if (!Demo->StartScene(Args[0], Error))
			{
				UE_LOG(LogAdamskAIDemo, Warning, TEXT("adsk.Demo %s: %s"), *Args[0], *Error);
			}
		}));

	FAutoConsoleCommand ListCommand(TEXT("adsk.Demo.List"), TEXT("Lists the AdamskAI demo scenes."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			for (const FDemoScene& Each : Scenes) { UE_LOG(LogAdamskAIDemo, Display, TEXT("%s: %s"), Each.Name, Each.Hint); }
		}));

	FAutoConsoleCommandWithWorld ClearCommand(TEXT("adsk.Demo.Clear"), TEXT("Removes what the current AdamskAI demo scene spawned."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UADSKDemoSubsystem* Demo = UADSKDemoSubsystem::Get(World)) { Demo->Clear(); }
		}));

	FAutoConsoleCommandWithWorldAndArgs FleetCommand(TEXT("adsk.Demo.Fleet"),
		TEXT("adsk.Demo.Fleet <count> [lod] [parallel] [norecorder] [color]: the Fleet scene with that many brains and those settings."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UADSKDemoSubsystem* Demo = UADSKDemoSubsystem::Get(World);
			if (!Demo) { return; }
			FADSKDemoFleetOptions Options;
			for (const FString& Arg : Args)
			{
				if (Arg.IsNumeric()) { Options.Count = FMath::Clamp(FCString::Atoi(*Arg), 1, 10000); }
				else if (Arg.Equals(TEXT("lod"), ESearchCase::IgnoreCase)) { Options.bTickLOD = true; }
				else if (Arg.Equals(TEXT("parallel"), ESearchCase::IgnoreCase)) { Options.bParallel = true; }
				else if (Arg.Equals(TEXT("norecorder"), ESearchCase::IgnoreCase)) { Options.bRecorder = false; }
				else if (Arg.Equals(TEXT("color"), ESearchCase::IgnoreCase)) { Options.bColor = true; }
				else { UE_LOG(LogAdamskAIDemo, Warning, TEXT("adsk.Demo.Fleet: unknown option '%s'"), *Arg); return; }
			}
			FString Error;
			if (!Demo->StartFleet(Options, Error)) { UE_LOG(LogAdamskAIDemo, Warning, TEXT("adsk.Demo.Fleet: %s"), *Error); }
		}));

	FAutoConsoleCommandWithWorldAndArgs TargetCommand(TEXT("adsk.Demo.Target"), TEXT("adsk.Demo.Target stop|go: halts or resumes the demo's stand-in for the player."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UADSKDemoSubsystem* Demo = UADSKDemoSubsystem::Get(World);
			if (Demo && Demo->GetTarget()) { Demo->GetTarget()->bWalking = Args.IsEmpty() || !Args[0].Equals(TEXT("stop"), ESearchCase::IgnoreCase); }
		}));
}

using namespace ADSKDemoSubsystemPrivate;

UADSKDemoSubsystem* UADSKDemoSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UADSKDemoSubsystem>() : nullptr;
}

bool UADSKDemoSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UADSKDemoSubsystem::Deinitialize()
{
	Clear();
	Super::Deinitialize();
}

TArray<FString> UADSKDemoSubsystem::GetSceneNames()
{
	TArray<FString> Names;
	for (const FDemoScene& Each : Scenes) { Names.Add(Each.Name); }
	return Names;
}

bool UADSKDemoSubsystem::StartScene(const FString& Name, FString& OutError)
{
	const FDemoScene* Found = Algo::FindByPredicate(Scenes, [&Name](const FDemoScene& Each) { return Name.Equals(Each.Name, ESearchCase::IgnoreCase); });
	if (!Found)
	{
		OutError = FString::Printf(TEXT("no scene '%s'; scenes: %s"), *Name, *FString::Join(GetSceneNames(), TEXT(", ")));
		return false;
	}
	Clear();
	Scene = Found->Name;
	Hint = Found->Hint;
	EnableLabels();
	const TMap<FString, FSetup> Setups = {
		{TEXT("Patrol"), &UADSKDemoSubsystem::SetupPatrol},
		{TEXT("Chase"), &UADSKDemoSubsystem::SetupChase},
		{TEXT("Interrupt"), &UADSKDemoSubsystem::SetupInterrupt},
		{TEXT("Reload"), &UADSKDemoSubsystem::SetupReload},
		{TEXT("Squad"), &UADSKDemoSubsystem::SetupSquad},
		{TEXT("Tokens"), &UADSKDemoSubsystem::SetupTokens},
		{TEXT("Fleet"), &UADSKDemoSubsystem::SetupFleet},
	};
	const bool bStarted = (this->*Setups.FindChecked(Scene))(OutError);
	if (!bStarted)
	{
		Clear();
	}
	return bStarted;
}

void UADSKDemoSubsystem::Clear()
{
	for (AActor* Actor : Spawned)
	{
		if (IsValid(Actor)) { Actor->Destroy(); }
	}
	Spawned.Reset();
	Agents.Reset();
	Dots.Reset();
	FleetLine.Reset();
	if (SavedTick.IsSet())
	{
		UADSKSettings* Settings = GetMutableDefault<UADSKSettings>();
		Settings->TickLOD = SavedTick->TickLOD;
		Settings->bParallelManagedTick = SavedTick->bParallelManagedTick;
		Settings->RecorderCapacity = SavedTick->RecorderCapacity;
		SavedTick.Reset();
	}
	Target = nullptr;
	bPublishTargetLocation = false;
	if (SavedStrikePool.IsSet())
	{
		TMap<FName, int32>& Pools = GetMutableDefault<UADSKSettings>()->TokenPools;
		if (SavedStrikePool->IsSet()) { Pools.Add(TEXT("DemoStrike"), SavedStrikePool->GetValue()); }
		else { Pools.Remove(TEXT("DemoStrike")); }
		SavedStrikePool.Reset();
	}
	Scene.Reset();
	Hint.Reset();
}

const TCHAR* UADSKDemoSubsystem::EditMount = TEXT("AdskDemoEdit");

FString UADSKDemoSubsystem::GetEditDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AdamskAI/DemoEdit"));
}

FString UADSKDemoSubsystem::Demo(const TCHAR* File)
{
	return FString::Printf(TEXT("@%s/%s"), FADSKDemoModule::Mount, File);
}

AADSKDemoController* UADSKDemoSubsystem::SpawnAgent(const FVector& Location, const FString& Source, TConstArrayView<FADSKNamedValue> Inputs, FString& OutError)
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AADSKDemoAgent* Pawn = World->SpawnActor<AADSKDemoAgent>(Location + FVector(0.0, 0.0, 90.0), FRotator::ZeroRotator, Params);
	AADSKDemoController* Controller = Pawn ? World->SpawnActor<AADSKDemoController>() : nullptr;
	if (!Controller)
	{
		OutError = TEXT("could not spawn an agent");
		return nullptr;
	}
	Spawned.Add(Pawn);
	Spawned.Add(Controller);
	Controller->Possess(Pawn);
	const FString& Path = Source;
	if (!Controller->Brain->InitializeFromSource(Path))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *Controller->Brain->GetLastError());
		return nullptr;
	}
	for (const FADSKNamedValue& Input : Inputs)
	{
		Controller->Brain->SetInput(Input.Name, Input.Value);
	}
	Controller->Brain->StartLogic();
	if (!Controller->Brain->IsADSKRunning())
	{
		OutError = FString::Printf(TEXT("%s did not start: %s"), *Path, *Controller->Brain->GetLastError());
		return nullptr;
	}
	Agents.Add(Controller);
	return Controller;
}

AADSKDemoTarget* UADSKDemoSubsystem::SpawnTarget()
{
	Target = GetWorld()->SpawnActor<AADSKDemoTarget>(ADSKDemoLayout::TargetLoop[0] + FVector(0.0, 0.0, AADSKDemoTarget::Height), FRotator::ZeroRotator);
	Spawned.Add(Target);
	return Target;
}

bool UADSKDemoSubsystem::SetupPatrol(FString& OutError)
{
	using namespace ADSKDemoLayout;
	constexpr int32 Corners = UE_ARRAY_COUNT(PatrolPoints);
	for (int32 Guard = 0; Guard < 3; ++Guard)
	{
		// Each guard walks the same square from another corner.
		TArray<FADSKNamedValue> Inputs;
		for (int32 Point = 0; Point < Corners; ++Point)
		{
			Inputs.Add({FString::Printf(TEXT("P%d"), Point + 1), FADSKValue::MakeVector(PatrolPoints[(Guard + Point) % Corners])});
		}
		if (!SpawnAgent(PatrolPoints[(Guard + Corners - 1) % Corners], Demo(TEXT("patrol.xml")), Inputs, OutError)) { return false; }
	}
	const FADSKNamedValue PostInput[] = {{TEXT("Post"), FADSKValue::MakeVector(Post)}};
	return SpawnAgent(Post, Demo(TEXT("post.xml")), PostInput, OutError) != nullptr;
}

bool UADSKDemoSubsystem::SpawnChase(const FString& Source, FString& OutError)
{
	SpawnTarget();
	bPublishTargetLocation = true;
	// Each beat crosses the target's loop somewhere, so every guard gets to see it.
	const TPair<FVector, FVector> Beats[] = {
		{FVector(1800.0, 1800.0, 0.0), FVector(1800.0, -200.0, 0.0)},
		{FVector(-1800.0, -300.0, 0.0), FVector(-200.0, -1800.0, 0.0)},
		{FVector(0.0, 1900.0, 0.0), FVector(-1800.0, 1200.0, 0.0)},
	};
	for (const TPair<FVector, FVector>& Beat : Beats)
	{
		const FADSKNamedValue Inputs[] = {
			{TEXT("Target"), FADSKObjectHandles::Make(Target)}, {TEXT("TargetLocation"), FADSKValue::MakeVector(Target->GetActorLocation())},
			{TEXT("A"), FADSKValue::MakeVector(Beat.Key)}, {TEXT("B"), FADSKValue::MakeVector(Beat.Value)}};
		if (!SpawnAgent(Beat.Key, Source, Inputs, OutError)) { return false; }
	}
	return true;
}

bool UADSKDemoSubsystem::SetupReload(FString& OutError)
{
	// A fresh copy each time the scene starts, mounted once per session.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AdamskAI"));
	const FString Original = Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Demo/chase.xml")) : FString();
	const FString Copy = GetEditDirectory() / TEXT("chase.xml");
	FString Existing;
	if (Original.IsEmpty() || IFileManager::Get().Copy(*Copy, *Original) != COPY_OK
		|| (!FADSKBehaviorRoots::Find(EditMount, Existing) && !FADSKBehaviorRoots::Register(EditMount, GetEditDirectory())))
	{
		OutError = FString::Printf(TEXT("cannot copy Demo/chase.xml to %s and mount it as @%s"), *Copy, EditMount);
		return false;
	}
	Hint += FString::Printf(TEXT(" File: %s (saved changes reload by themselves; adsk.Reload forces it)."), *Copy);
	return SpawnChase(FString::Printf(TEXT("@%s/chase.xml"), EditMount), OutError);
}

bool UADSKDemoSubsystem::SetupInterrupt(FString& OutError)
{
	SpawnTarget();
	const FVector Homes[] = {FVector(-600.0, -900.0, 0.0), FVector(600.0, 900.0, 0.0)};
	for (const FVector& Home : Homes)
	{
		const FADSKNamedValue Inputs[] = {
			{TEXT("Target"), FADSKObjectHandles::Make(Target)}, {TEXT("Home"), FADSKValue::MakeVector(Home)},
			{TEXT("A"), FADSKValue::MakeVector(Home + FVector(1200.0, 0.0, 0.0))}, {TEXT("B"), FADSKValue::MakeVector(Home - FVector(1200.0, 0.0, 0.0))}};
		if (!SpawnAgent(Home, Demo(TEXT("interrupt.xml")), Inputs, OutError)) { return false; }
	}
	return true;
}

bool UADSKDemoSubsystem::SetupSquad(FString& OutError)
{
	SpawnTarget();
	bPublishTargetLocation = true;
	UADSKGroupSubsystem* Groups = UADSKGroupSubsystem::Get(this);
	const FVector Posts[] = {FVector(-1800.0, 0.0, 0.0), FVector(0.0, -1800.0, 0.0), FVector(1800.0, 300.0, 0.0), FVector(300.0, 1800.0, 0.0)};
	for (const FVector& Post : Posts)
	{
		const FADSKNamedValue Inputs[] = {
			{TEXT("Target"), FADSKObjectHandles::Make(Target)}, {TEXT("TargetLocation"), FADSKValue::MakeVector(Target->GetActorLocation())},
			{TEXT("Post"), FADSKValue::MakeVector(Post)}};
		AADSKDemoController* Agent = SpawnAgent(Post, Demo(TEXT("squad.xml")), Inputs, OutError);
		if (!Agent || !Groups) { return false; }
		Groups->Join(*Agent->GetPawn(), TEXT("DemoSquad"));
	}
	return true;
}

bool UADSKDemoSubsystem::SetupTokens(FString& OutError)
{
	SpawnTarget();
	Target->bWalking = false;
	Target->SetActorLocation(FVector(0.0, 0.0, AADSKDemoTarget::Height));
	// Two strikers at a time; the project's own value comes back with Clear.
	TMap<FName, int32>& Pools = GetMutableDefault<UADSKSettings>()->TokenPools;
	const int32* Existing = Pools.Find(TEXT("DemoStrike"));
	SavedStrikePool.Emplace(Existing ? TOptional<int32>(*Existing) : TOptional<int32>());
	Pools.Add(TEXT("DemoStrike"), 2);
	constexpr int32 Count = 6;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double Angle = 2.0 * UE_DOUBLE_PI * Index / Count;
		const FADSKNamedValue Inputs[] = {{TEXT("Target"), FADSKObjectHandles::Make(Target)}};
		if (!SpawnAgent(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * 1500.0, Demo(TEXT("tokens.xml")), Inputs, OutError)) { return false; }
	}
	return true;
}

bool UADSKDemoSubsystem::SetupFleet(FString& OutError)
{
	return StartFleet(FADSKDemoFleetOptions(), OutError);
}

bool UADSKDemoSubsystem::StartFleet(const FADSKDemoFleetOptions& Options, FString& OutError)
{
	if (Scene != TEXT("Fleet"))
	{
		// From the console: clear and present the scene as StartScene would.
		Clear();
		Scene = TEXT("Fleet");
		Hint = Algo::FindByPredicate(Scenes, [](const FDemoScene& Each) { return FCString::Strcmp(Each.Name, TEXT("Fleet")) == 0; })->Hint;
	}
	FleetOptions = Options;
	UADSKSettings* Settings = GetMutableDefault<UADSKSettings>();
	SavedTick.Emplace(FSavedTickSettings{Settings->TickLOD, Settings->bParallelManagedTick, Settings->RecorderCapacity});
	Settings->TickLOD.Reset();
	if (Options.bTickLOD)
	{
		Settings->TickLOD.Add({1000.f, 0.2f});
		Settings->TickLOD.Add({2000.f, 0.5f});
	}
	Settings->bParallelManagedTick = Options.bParallel;
	Settings->RecorderCapacity = Options.bRecorder ? SavedTick->RecorderCapacity : 0;
	// Labels over hundreds of brains cost more than the brains; Gameplay Debugger and adsk.Status still work.
	if (IConsoleVariable* Labels = IConsoleManager::Get().FindConsoleVariable(TEXT("adsk.Labels")))
	{
		Labels->Set(Options.Count <= 100, ECVF_SetByCode);
	}
	// A square grid filling most of the floor, at most 3 m apart.
	const int32 Side = FMath::CeilToInt(FMath::Sqrt(double(Options.Count)));
	const double Spacing = FMath::Min(300.0, 1.8 * ADSKDemoLayout::FloorHalf / FMath::Max(1, Side - 1));
	const FVector Corner(-0.5 * Spacing * (Side - 1), -0.5 * Spacing * (Side - 1), 50.0);
	const FString Source = Demo(TEXT("fleet.xml"));
	for (int32 Index = 0; Index < Options.Count; ++Index)
	{
		const FVector Location = Corner + FVector(Spacing * (Index % Side), Spacing * (Index / Side), 0.0);
		AADSKDemoDot* Dot = GetWorld()->SpawnActor<AADSKDemoDot>(Location, FRotator::ZeroRotator);
		if (!Dot)
		{
			OutError = TEXT("could not spawn a fleet dot");
			return false;
		}
		Spawned.Add(Dot);
		// Seeded per dot: the fleet varies, and a rerun replays it.
		Dot->Brain->RandomSeed = Index + 1;
		if (!Dot->Brain->InitializeFromSource(Source))
		{
			OutError = FString::Printf(TEXT("%s: %s"), *Source, *Dot->Brain->GetLastError());
			return false;
		}
		Dot->Brain->StartLogic();
		Dots.Add(Dot);
	}
	if (UADSKWorldSubsystem* Brains = GetWorld()->GetSubsystem<UADSKWorldSubsystem>()) { Brains->ResetManagedTickStats(); }
	FleetMeasuredAt = FPlatformTime::Seconds();
	FleetFrames = 0;
	return true;
}

void UADSKDemoSubsystem::MeasureFleet()
{
	++FleetFrames;
	UADSKWorldSubsystem* Brains = GetWorld()->GetSubsystem<UADSKWorldSubsystem>();
	const double Now = FPlatformTime::Seconds();
	if (!Brains || Now - FleetMeasuredAt < 0.5 || FleetFrames == 0)
	{
		return;
	}
	const FADSKManagedTickStats& Pass = Brains->GetManagedTickStats();
	const double Frames = double(FleetFrames);
	const double PassMs = 1000.0 * Pass.Seconds / Frames;
	const double MicrosPerTick = Pass.BrainTicks > 0 ? 1.0e6 * Pass.Seconds / double(Pass.BrainTicks) : 0.0;
	FleetLine = FString::Printf(TEXT("%d brains: pass %.3f ms per frame, %.2f us per brain tick, %.2f ticks per brain per frame, %d%% on workers, frame %.1f ms%s"),
		Dots.Num(), PassMs, MicrosPerTick, Dots.IsEmpty() ? 0.0 : double(Pass.BrainTicks) / (Frames * Dots.Num()),
		Pass.BrainTicks > 0 ? int32(100 * Pass.ParallelTicks / Pass.BrainTicks) : 0, 1000.0 * (Now - FleetMeasuredAt) / Frames,
		FleetOptions.bRecorder ? TEXT(", recorders on") : TEXT(", recorders off"));
	Brains->ResetManagedTickStats();
	FleetMeasuredAt = Now;
	FleetFrames = 0;
}

void UADSKDemoSubsystem::Tick(float DeltaTime)
{
	if (Scene.IsEmpty() || !GEngine)
	{
		return;
	}
	if (bPublishTargetLocation && Target)
	{
		const FADSKValue Location = FADSKValue::MakeVector(Target->GetActorLocation());
		for (AADSKDemoController* Agent : Agents) { Agent->Brain->SetInput(TEXT("TargetLocation"), Location); }
	}
	// Refreshed every frame under fixed keys, so the text stays while the scene runs and goes with it.
	FString Live;
	if (Scene == TEXT("Fleet"))
	{
		MeasureFleet();
		Live = FleetLine;
		if (FleetOptions.bColor && !Dots.IsEmpty())
		{
			// A tenth of the fleet per frame, so tinting stays a fraction of the brains' own cost.
			const int32 Batch = FMath::Max(1, Dots.Num() / 10);
			for (int32 Step = 0; Step < Batch; ++Step)
			{
				AADSKDemoDot* Dot = Dots[NextTint++ % Dots.Num()];
				const FADSKInstance* Instance = Dot->Brain->GetInstance();
				const TArray<FString> Path = Instance ? Instance->GetActiveStatePath() : TArray<FString>();
				Dot->SetTint(Path.IsEmpty() ? FLinearColor::Gray : ADSKDemoColors::ForState(Path.Last()));
			}
		}
	}
	else if (Scene == TEXT("Tokens") && Target)
	{
		const UADSKGroupSubsystem* Groups = UADSKGroupSubsystem::Get(this);
		Live = FString::Printf(TEXT("Strikers now: %d of %d"), Groups ? Groups->CountHolders(TEXT("DemoStrike"), Target) : 0, UADSKGroupSubsystem::GetPoolSize(TEXT("DemoStrike")));
	}
	const FString Lines[] = {
		FString::Printf(TEXT("AdamskAI demo: %s, %d agents (adsk.Demo.List, adsk.Demo.Clear)"), *Scene, Agents.Num()),
		Hint,
		Live,
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Lines); ++Index)
	{
		GEngine->AddOnScreenDebugMessage(FirstMessageKey + Index, 0.f, Index == 0 ? FColor::Cyan : FColor::White, Lines[Index]);
	}
}

TStatId UADSKDemoSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UADSKDemoSubsystem, STATGROUP_Tickables);
}
