// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "AssetRegistry/AssetData.h"
#include "Cook/ADSKAssetRenames.h"
#include "Cook/ADSKCookReferences.h"
#include "IDirectoryWatcher.h"
#include "Reload/ADSKLiveReload.h"
#include "Sources/ADSKBehaviorRoots.h"
#include "Support/SpawnerNode.h"
#include "Support/TempTree.h"
#include "CQTest.h"

namespace FEditorHooksSpecPrivate
{
	struct FScopedMount
	{
		FScopedMount(const FString& InName, const FString& Directory) : Name(InName) { bRegistered = FADSKBehaviorRoots::Register(Name, Directory); }
		~FScopedMount() { if (bRegistered) FADSKBehaviorRoots::Unregister(Name); }
		FString Name;
		bool bRegistered = false;
	};

	TArray<FFileChangeData> Changes(std::initializer_list<TPair<FString, FFileChangeData::EFileChangeAction>> Files)
	{
		TArray<FFileChangeData> Out;
		for (const TPair<FString, FFileChangeData::EFileChangeAction>& File : Files) Out.Emplace(File.Key, File.Value);
		return Out;
	}

	FString SpawnerXml(const TCHAR* Spawn)
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"cook\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
			TEXT("<Tree id=\"t\"><Spawner id=\"a\" spawn=\"%s\"/></Tree></Agent>"), Spawn);
	}
}

using namespace FEditorHooksSpecPrivate;

TEST_CLASS_WITH_FLAGS(FLiveReloadSpec, "AdamskAI.Editor.LiveReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ChangedFilesAreNamedByTheirRootOrMount)
	{
		const FString Root = TEXT("D:/Project/Content/AI");
		FString Path;
		ASSERT_THAT(IsTrue(FADSKLiveReload::MapChange(FString(), Root, Root / TEXT("enemy.xml"), Path), TEXT("enemy.xml was not mapped")));
		ASSERT_THAT(AreEqual(FString(TEXT("enemy.xml")), Path));
		ASSERT_THAT(IsTrue(FADSKLiveReload::MapChange(FString(), Root, Root / TEXT("boss/phase2.xml"), Path), TEXT("boss/phase2.xml was not mapped")));
		ASSERT_THAT(AreEqual(FString(TEXT("boss/phase2.xml")), Path));
		ASSERT_THAT(IsTrue(FADSKLiveReload::MapChange(TEXT("@Zombies/"), TEXT("D:/Feature/AI"), TEXT("D:/Feature/AI/horde.xml"), Path), TEXT("horde.xml was not mapped")));
		ASSERT_THAT(AreEqual(FString(TEXT("@Zombies/horde.xml")), Path));
		// Not a behavior file: another extension, case or place.
		ASSERT_THAT(IsFalse(FADSKLiveReload::MapChange(FString(), Root, Root / TEXT("node-catalog.md"), Path), TEXT("node-catalog.md mapped to ") + Path));
		ASSERT_THAT(IsFalse(FADSKLiveReload::MapChange(FString(), Root, Root / TEXT("Enemy.XML"), Path), TEXT("Enemy.XML mapped to ") + Path));
		ASSERT_THAT(IsFalse(FADSKLiveReload::MapChange(FString(), Root, TEXT("D:/Project/Content/enemy.xml"), Path), TEXT("a file outside the root mapped to ") + Path));
	}

	TEST_METHOD(ChangesCollectUntilTheyQuietDownAndOnlyEditsCount)
	{
		FADSKLiveReload Reload;
		const FString Root = TEXT("D:/Project/Content/AI");
		Reload.InjectChangesForTest(Changes({{Root / TEXT("a.xml"), FFileChangeData::FCA_Modified}, {Root / TEXT("a.xml"), FFileChangeData::FCA_Modified},
			{Root / TEXT("gone.xml"), FFileChangeData::FCA_Removed}, {Root / TEXT("b.xml"), FFileChangeData::FCA_Added}}), FString(), Root);
		TArray<FString> Pending = Reload.GetPendingForTest();
		Pending.Sort();
		ASSERT_THAT(AreEqual(TArray<FString>{TEXT("a.xml"), TEXT("b.xml")}, Pending));
		const double Now = FPlatformTime::Seconds();
		// An editor saves in bursts: nothing is reloaded until 0.15 s pass without another change.
		Reload.TickForTest(Now + 0.05);
		ASSERT_THAT(AreEqual(2, Reload.GetPendingForTest().Num()));
		Reload.TickForTest(Now + 0.3);
		ASSERT_THAT(AreEqual(0, Reload.GetPendingForTest().Num()));
	}

	TEST_METHOD(TheWatchFollowsMountsAsTheyComeAndGo)
	{
		const FADSKTempTree Tree;
		Tree.Write(TEXT("feature/ai/horde.xml"), FADSKTestPaths::ValidAgentXml());
		FADSKLiveReload Reload;
		Reload.Start();
		ON_SCOPE_EXIT { Reload.Stop(); };
		const auto Watches = [&Reload](const TCHAR* Prefix) { return Reload.GetWatchesForTest().ContainsByPredicate([Prefix](const TPair<FString, FString>& Watch) { return Watch.Key == Prefix; }); };
		ASSERT_THAT(IsFalse(Watches(TEXT("@EditorHooks/")), TEXT("@EditorHooks/ is watched before it is mounted")));
		{
			const FScopedMount Mount(TEXT("EditorHooks"), Tree.Path(TEXT("feature/ai")));
			ASSERT_THAT(IsTrue(Mount.bRegistered, TEXT("the EditorHooks mount was refused")));
			ASSERT_THAT(IsTrue(Watches(TEXT("@EditorHooks/")), TEXT("the new mount is not watched")));
		}
		ASSERT_THAT(IsFalse(Watches(TEXT("@EditorHooks/")), TEXT("the watch outlived its mount")));
	}
};

TEST_CLASS_WITH_FLAGS(FEditorRenameCookSpec, "AdamskAI.Editor.RenameAndCook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ARenameRewritesFilesInEveryMountToTheAssetsNewPath)
	{
		const FADSKTempTree Tree;
		Tree.Write(TEXT("feature/ai/horde.xml"), TEXT("<RunEQS query=\"/Game/AdskRenameTest/Old.Old\"/><Spawner spawn=\"/Game/AdskRenameTest/Old.Old_C\"/>"));
		const FScopedMount Mount(TEXT("EditorHooks"), Tree.Path(TEXT("feature/ai")));
		ASSERT_THAT(IsTrue(Mount.bRegistered, TEXT("the EditorHooks mount was refused")));
		const FAssetData Renamed(FName(TEXT("/Game/AdskRenameTest/Moved/New")), FName(TEXT("/Game/AdskRenameTest/Moved")), FName(TEXT("New")),
			FTopLevelAssetPath(TEXT("/Script/AIModule"), TEXT("EnvQuery")));
		const TArray<FString> Changed = FADSKAssetRenames::HandleRename(Renamed, TEXT("/Game/AdskRenameTest/Old.Old"));
		ASSERT_THAT(AreEqual(1, Changed.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("<RunEQS query=\"/Game/AdskRenameTest/Moved/New.New\"/><Spawner spawn=\"/Game/AdskRenameTest/Moved/New.New_C\"/>")),
			Tree.Read(TEXT("feature/ai/horde.xml"))));
	}

	TEST_METHOD(TheCookTakesBlueprintClassesAndMountsButNeverNativeClasses)
	{
		const FADSKTempTree Tree;
		Tree.Write(TEXT("feature/ai/blueprint.xml"), SpawnerXml(TEXT("/Game/AdskCookTest/BP_Grunt.BP_Grunt_C")));
		Tree.Write(TEXT("feature/ai/native.xml"), SpawnerXml(TEXT("/Script/Engine.Character")));
		TArray<FString> Skipped;
		const TArray<FName> Packages = FADSKCookReferences::CollectPackages(Tree.Path(TEXT("feature/ai")), FADSKSpawnerNode::MakeRegistry(), Skipped);
		ASSERT_THAT(IsTrue(Skipped.IsEmpty(), TEXT("skipped: ") + FString::Join(Skipped, TEXT(", "))));
		ASSERT_THAT(AreEqual(TArray<FName>{FName(TEXT("/Game/AdskCookTest/BP_Grunt"))}, Packages));
		const FScopedMount Mount(TEXT("EditorHooks"), Tree.Path(TEXT("feature/ai")));
		ASSERT_THAT(IsTrue(FADSKBehaviorRoots::GetAllDirectories().ContainsByPredicate([&Tree](const FString& Directory) { return Directory.EndsWith(TEXT("feature/ai")); }),
			TEXT("the cook does not see the mount; directories: ") + FString::Join(FADSKBehaviorRoots::GetAllDirectories(), TEXT(", "))));
	}
};

#endif
