// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "World/WorldMapBuilder.h"
#include "World/WorldQueries.h"
#include "World/WorldScenario.h"
#include "ActorFactories/ActorFactory.h"
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Nodes/ADSKHostNodes.h"
#include "Validate/ADSKAssetValidation.h"
#include "Builders/CubeBuilder.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "NavigationPath.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/SavePackage.h"
#include "CQTest.h"

// Generated content: ADSK_GENERATE_WORLD_MAP=1 rewrites the map and EQS assets; a normal run only verifies them.
namespace FWorldContentSpecPrivate
{
	bool IsGenerating()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("ADSK_GENERATE_WORLD_MAP")) == TEXT("1");
	}

	/** An asset object named Name in its own package under Directory, made or reused. */
	template <typename AssetType>
	AssetType* MakeAsset(const TCHAR* Directory, const TCHAR* Name)
	{
		// A package created over a file on disk cannot be saved (partially loaded), nor can one loaded from a newer
		// engine's file, so a package not in memory replaces its file.
		const FString PackageName = FString(Directory) / Name;
		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (!Package)
		{
			IFileManager::Get().Delete(*FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), false, true, true);
			Package = CreatePackage(*PackageName);
		}
		Package->FullyLoad();
		AssetType* Asset = FindObject<AssetType>(Package, Name);
		return Asset ? Asset : NewObject<AssetType>(Package, Name, RF_Public | RF_Standalone | RF_Transactional);
	}

	bool SaveAsset(UObject& Asset)
	{
		UPackage* Package = Asset.GetPackage();
		Package->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(&Asset);
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, &Asset, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}
}

using namespace FWorldContentSpecPrivate;

TEST_CLASS_WITH_FLAGS(FWorldContentSpec, "AdamskAIWorld.Content",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(GenerateMap)
	{
		if (!IsGenerating())
		{
			TestRunner->AddInfo(TEXT("skipped: set ADSK_GENERATE_WORLD_MAP=1 to write the test map"));
			return;
		}
		using namespace ADSKWorldLayout;
		UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
		ASSERT_THAT(IsNotNull(World));
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		ASSERT_THAT(IsNotNull(Cube));

		using ADSKWorldMapBuilder::Block;
		Block(*World, Cube, TEXT("Floor"), FVector(0.0, 0.0, -50.0), FVector(2.0 * FloorHalf, 2.0 * FloorHalf, 100.0));
		Block(*World, Cube, TEXT("Island"), IslandCenter - FVector(0.0, 0.0, 50.0), FVector(2.0 * IslandHalf, 2.0 * IslandHalf, 100.0));
		Block(*World, Cube, TEXT("SightWall"), WallCenter, WallSize);
		World->SpawnActor<APlayerStart>(ADSKWorldLayout::PlayerStart, FRotator::ZeroRotator)->SetActorLabel(TEXT("PlayerStart"));
		World->SpawnActor<ADirectionalLight>(FVector(0.0, 0.0, 1000.0), FRotator(-50.0, 30.0, 0.0))->SetActorLabel(TEXT("Sun"));
		// An engine game mode: the project's own never runs here, and the map references nothing of this module.
		World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
		ASSERT_THAT(IsNotNull(ADSKWorldMapBuilder::BuildNavigation(*World, NavBoundsCenter, NavBoundsSize), TEXT("the navmesh built no tiles")));

		const FString Map = FString(MapDirectory) / MapName;
		ASSERT_THAT(IsTrue(UEditorLoadingAndSavingUtils::SaveMap(World, Map), *FString::Printf(TEXT("cannot save %s"), *Map)));
		TestRunner->AddInfo(FString::Printf(TEXT("wrote %s"), *Map));
	}

	TEST_METHOD(GenerateQueries)
	{
		if (!IsGenerating())
		{
			TestRunner->AddInfo(TEXT("skipped: set ADSK_GENERATE_WORLD_MAP=1 to write the test EQS assets"));
			return;
		}
		using namespace ADSKWorldQueries;
		UEnvQuery* Query = MakeAsset<UEnvQuery>(Directory, RingsName);
		Fill(*Query);
		ASSERT_THAT(IsTrue(SaveAsset(*Query), TEXT("cannot save EQ_AdskTest_Rings")));
		// What a rename without fixing up redirectors leaves behind: the old name, pointing at the asset.
		UObjectRedirector* Redirector = MakeAsset<UObjectRedirector>(Directory, OldName);
		Redirector->DestinationObject = Query;
		ASSERT_THAT(IsTrue(SaveAsset(*Redirector), TEXT("cannot save EQ_AdskTest_Old")));
		TestRunner->AddInfo(FString::Printf(TEXT("wrote %s and %s"), *AssetPath(RingsName), *AssetPath(OldName)));
	}

	/** The EQS asset declares exactly the params the specs pass, and the old name still leads to it. */
	TEST_METHOD(QueryAssetDeclaresItsParams)
	{
		using namespace ADSKWorldQueries;
		const UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *AssetPath(RingsName));
		ASSERT_THAT(IsNotNull(Query, TEXT("EQ_AdskTest_Rings is missing; generate it with ADSK_GENERATE_WORLD_MAP=1")));
		FADSKNamedParam Radius; Radius.Name = RadiusParam; Radius.Type = EADSKParamType::Float;
		FADSKNamedParam Rings; Rings.Name = RingsParam; Rings.Type = EADSKParamType::Int;
		FADSKNamedParam Reachable; Reachable.Name = ReachableParam; Reachable.Type = EADSKParamType::Bool;
		ASSERT_THAT(AreEqual(FString(), ADSKRunEQS::FindUnknownParams(*Query, {Radius, Rings, Reachable})));
		// The same names with the wrong types are not declared.
		Rings.Type = EADSKParamType::Float;
		ASSERT_THAT(AreEqual(FString(TEXT("'Rings'")), ADSKRunEQS::FindUnknownParams(*Query, {Rings})));
		ASSERT_THAT(IsTrue(LoadObject<UEnvQuery>(nullptr, *AssetPath(OldName)) == Query, TEXT("EQ_AdskTest_Old does not redirect to EQ_AdskTest_Rings")));
	}

	/** I18: a behavior that still names the old path compiles, and the editor validator says where the asset went. */
	TEST_METHOD(ValidatorWarnsOnARedirectedQuery)
	{
		using namespace ADSKWorldQueries;
		IAssetRegistry::GetChecked().WaitForCompletion();
		const FString Xml = FString::Printf(TEXT("<Agent version=\"1\" id=\"old\"><Locals><Local name=\"Point\" type=\"Vector\"/></Locals>"
			"<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><RunEQS id=\"q\" query=\"%s\" out-point=\"Point\"/></Tree></Agent>"),
			*AssetPath(OldName));
		FADSKCompileResult Result = FADSKCompiler::Compile(TEXT("old.xml"), Xml, ADSKTest::PluginCatalogue());
		ASSERT_THAT(IsTrue(Result.IsSuccess(), TEXT("does not compile:") + ADSKTest::FormatDiagnostics(Result)));
		FADSKAssetValidation::AppendMissingAssets(*Result.Definition, Result);
		const FString Expected = FString::Printf(TEXT("object '%s' was moved to '%s'"), *AssetPath(OldName), *AssetPath(RingsName));
		ASSERT_THAT(IsTrue(Result.Diagnostics.ContainsByPredicate([&Expected](const FADSKDiagnostic& D) { return D.Severity == EADSKSeverity::Warning && D.Message == Expected; }),
			TEXT("expected the warning \"") + Expected + TEXT("\", got:") + ADSKTest::FormatDiagnostics(Result)));
	}
};

TEST_CLASS_WITH_FLAGS(FWorldMapSpec, "AdamskAIWorld.Map",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKWorldScenario Scenario;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	/** What every AdamskAIWorld.* spec takes for granted about the map, checked in PIE where they run. */
	TEST_METHOD(HoldsWhatTheTestsAssume)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, FTimespan::FromSeconds(30))
			.Then(TEXT("layout"), [this]
			{
				UWorld& World = Scenario.GetWorld();
				ASSERT_THAT(IsTrue(World.GetAuthGameMode() && World.GetAuthGameMode()->GetClass() == AGameModeBase::StaticClass(), TEXT("the game mode is not the engine's AGameModeBase")));
				const ARecastNavMesh* NavMesh = ADSKWorldMapBuilder::FindNavMesh(World);
				ASSERT_THAT(IsNotNull(NavMesh));
				ASSERT_THAT(IsTrue(NavMesh->GetRuntimeGenerationMode() == ERuntimeGenerationType::Dynamic, TEXT("the navmesh is not dynamic")));
				const FVector Island = Scenario.OnNavmesh(ADSKWorldLayout::IslandCenter);
				const UNavigationPath* ToIsland = UNavigationSystemV1::FindPathToLocationSynchronously(&World, FVector(0.0, 0.0, ADSKWorldLayout::StandZ), Island);
				ASSERT_THAT(IsTrue(!ToIsland || !ToIsland->IsValid() || ToIsland->IsPartial(), TEXT("a full path reaches the island")));
				const FVector Corner = Scenario.OnNavmesh(FVector(ADSKWorldLayout::FloorHalf - 200.0, -(ADSKWorldLayout::FloorHalf - 200.0), 0.0));
				const UNavigationPath* ToCorner = UNavigationSystemV1::FindPathToLocationSynchronously(&World, FVector(0.0, 0.0, ADSKWorldLayout::StandZ), Corner);
				ASSERT_THAT(IsTrue(ToCorner && ToCorner->IsValid() && !ToCorner->IsPartial(), TEXT("no full path across the floor")));
			});
	}
};

#endif
