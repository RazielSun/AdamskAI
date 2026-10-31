// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Catalogue/ADSKNodeProvider.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Features/IModularFeatures.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Program/ADSKDefinition.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Settings/ADSKSettings.h"
#include "Sources/ADSKBehaviorRoots.h"
#include "Misc/Paths.h"
#include "Sources/ADSKSourcePath.h"
#include "Sources/ADSKValidation.h"
#include "Support/TempTree.h"
#include "Support/TestChecks.h"
#include "CQTest.h"

namespace FGameFeatureSpecPrivate
{
	class FLateProvider final : public IADSKNodeProvider
	{
	public:
		virtual FString GetProviderName() const override { return TEXT("AdamskAITests.Late"); }
		virtual void RegisterNodes(FADSKNodeRegistry& Registry, TArray<FString>& OutErrors) override
		{
			FADSKNodeDescriptor Node;
			Node.Name = TEXT("LateNode");
			Node.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
			if (Registry.Register(MoveTemp(Node)) != EADSKRegisterResult::Accepted) OutErrors.Add(TEXT("LateNode refused"));
		}
	};

	struct FScopedMount
	{
		explicit FScopedMount(const FString& Directory) { bRegistered = FADSKBehaviorRoots::Register(TEXT("Zombies"), Directory); }
		~FScopedMount() { if (bRegistered) FADSKBehaviorRoots::Unregister(TEXT("Zombies")); }
		bool bRegistered = false;
	};
}

using namespace FGameFeatureSpecPrivate;

TEST_CLASS_WITH_FLAGS(FGameFeatureSpec, "AdamskAI.Host.GameFeature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(MountedPathsNormalizeAndSplit)
	{
		FString Path, Mount, Relative;
		ASSERT_THAT(IsTrue(FADSKSourcePath::Normalize(TEXT("@Zombies/boss\\horde.xml"), Path), TEXT("the mount path was rejected")));
		ASSERT_THAT(AreEqual(FString(TEXT("@Zombies/boss/horde.xml")), Path));
		ASSERT_THAT(IsTrue(FADSKSourcePath::SplitMount(Path, Mount, Relative), TEXT("the mount path did not split")));
		ASSERT_THAT(AreEqual(FString(TEXT("Zombies")), Mount));
		ASSERT_THAT(AreEqual(FString(TEXT("boss/horde.xml")), Relative));
		ASSERT_THAT(IsFalse(FADSKSourcePath::SplitMount(TEXT("boss/horde.xml"), Mount, Relative), TEXT("a plain path split as a mount")));
		for (const TCHAR* Bad : {TEXT("@Zombies"), TEXT("@/x.xml"), TEXT("@1st/x.xml"), TEXT("@Zom bies/x.xml"), TEXT("@Zombies/../x.xml"), TEXT("@Zombies//x.txt")})
		{
			ASSERT_THAT(IsFalse(FADSKSourcePath::Normalize(Bad, Path), FString::Printf(TEXT("accepted [%s]"), Bad)));
		}
	}

	TEST_METHOD(MountsRegisterOnceAndAnnounceChanges)
	{
		const FADSKTempTree Tree;
		int32 Changes = 0;
		const FDelegateHandle Handle = FADSKBehaviorRoots::OnChanged().AddLambda([&Changes]() { ++Changes; });
		ASSERT_THAT(IsFalse(FADSKBehaviorRoots::Register(TEXT("9lives"), Tree.Path(TEXT("ai"))), TEXT("an invalid mount name registered")));
		ASSERT_THAT(IsTrue(FADSKBehaviorRoots::Register(TEXT("Zombies"), Tree.Path(TEXT("ai"))), TEXT("Zombies did not register")));
		ASSERT_THAT(IsFalse(FADSKBehaviorRoots::Register(TEXT("Zombies"), Tree.Path(TEXT("other"))), TEXT("Zombies registered twice")));
		FString Directory;
		ASSERT_THAT(IsTrue(FADSKBehaviorRoots::Find(TEXT("Zombies"), Directory), TEXT("Zombies is not found")));
		ASSERT_THAT(IsTrue(Directory.EndsWith(TEXT("/ai")), Directory));
		ASSERT_THAT(IsFalse(FADSKBehaviorRoots::Find(TEXT("zombies"), Directory), TEXT("mount names matched ignoring case")));
		ASSERT_THAT(IsTrue(FADSKBehaviorRoots::GetAll().ContainsByPredicate([](const TPair<FString, FString>& Mount) { return Mount.Key == TEXT("Zombies"); }), TEXT("Zombies is not listed")));
		ASSERT_THAT(IsTrue(FADSKBehaviorRoots::Unregister(TEXT("Zombies")), TEXT("Zombies did not unregister")));
		ASSERT_THAT(IsFalse(FADSKBehaviorRoots::Unregister(TEXT("Zombies")), TEXT("Zombies unregistered twice")));
		FADSKBehaviorRoots::OnChanged().Remove(Handle);
		ASSERT_THAT(AreEqual(2, Changes));
	}

	TEST_METHOD(AMountedFileReadsFromItsMountUnderItsMountedName)
	{
		const FADSKTempTree Tree;
		Tree.Write(TEXT("feature/ai/horde.xml"), FADSKTestPaths::ValidAgentXml());
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		Registry->Freeze();
		const FString MainRoot = Tree.Path(TEXT("main"));
		const FADSKSourceReadResult Unmounted = FADSKValidation::ReadSource(MainRoot, TEXT("@Zombies/horde.xml"), Registry);
		ASSERT_THAT(IsTrue(Unmounted.bReadFailed, TEXT("an unmounted path was read")));
		ASSERT_THAT(AreEqual(FString(TEXT("source-read")), Unmounted.ReportCode));
		const FScopedMount Mount(Tree.Path(TEXT("feature/ai")));
		ASSERT_THAT(IsTrue(Mount.bRegistered, TEXT("the mount was refused")));
		const FADSKSourceReadResult Read = FADSKValidation::ReadSource(MainRoot, TEXT("@Zombies/horde.xml"), Registry);
		ASSERT_THAT(IsFalse(Read.bReadFailed, TEXT("the mounted file was not read")));
		ASSERT_THAT(IsTrue(Read.Result.IsSuccess(), ADSKTest::FormatDiagnostics(Read.Result)));
		ASSERT_THAT(AreEqual(FString(TEXT("@Zombies/horde.xml")), Read.Result.Definition->GetSourceName()));
		// The main root is untouched by a mount, and a mounted path cannot climb out of it.
		ASSERT_THAT(IsTrue(FADSKValidation::ReadSource(MainRoot, TEXT("horde.xml"), Registry).bReadFailed, TEXT("horde.xml was read from the main root")));
		ASSERT_THAT(IsTrue(FADSKValidation::ReadSource(MainRoot, TEXT("@Zombies/../ai/horde.xml"), Registry).bReadFailed, TEXT("a mounted path climbed out of its mount")));
	}

	TEST_METHOD(ALateProviderGetsANewCatalogueAndOldDefinitionsKeepTheirs)
	{
		TArray<FString> Errors;
		const TSharedPtr<const FADSKNodeRegistry> Before = FADSKRegistryAssembly::TryGet(Errors);
		ASSERT_THAT(IsTrue(Before.IsValid(), TEXT("no catalogue: ") + FString::Join(Errors, TEXT("; "))));
		ASSERT_THAT(IsNull(Before->Find(TEXT("LateNode")), TEXT("LateNode is registered before its provider")));
		const uint32 Generation = FADSKRegistryAssembly::GetGeneration();
		ASSERT_THAT(IsTrue(FADSKRegistryAssembly::TryGet(Errors) == Before, TEXT("an unchanged catalogue was rebuilt")));
		ASSERT_THAT(AreEqual(Generation, FADSKRegistryAssembly::GetGeneration()));

		FLateProvider Provider;
		IModularFeatures::Get().RegisterModularFeature(IADSKNodeProvider::GetFeatureName(), &Provider);
		const TSharedPtr<const FADSKNodeRegistry> With = FADSKRegistryAssembly::TryGet(Errors);
		IModularFeatures::Get().UnregisterModularFeature(IADSKNodeProvider::GetFeatureName(), &Provider);
		ASSERT_THAT(IsTrue(With.IsValid() && With != Before, TEXT("the catalogue was not rebuilt")));
		ASSERT_THAT(IsNotNull(With->Find(TEXT("LateNode")), TEXT("LateNode is missing")));
		ASSERT_THAT(AreEqual(Generation + 1, FADSKRegistryAssembly::GetGeneration()));
		ASSERT_THAT(AreNotEqual(Before->GetCatalogueFingerprint(), With->GetCatalogueFingerprint()));
		ASSERT_THAT(IsTrue(Before->IsFrozen(), TEXT("the old catalogue thawed")));
		ASSERT_THAT(IsNull(Before->Find(TEXT("LateNode")), TEXT("the old catalogue gained LateNode")));

		const TSharedPtr<const FADSKNodeRegistry> After = FADSKRegistryAssembly::TryGet(Errors);
		ASSERT_THAT(IsTrue(After.IsValid(), TEXT("no catalogue after the provider left")));
		ASSERT_THAT(IsNull(After->Find(TEXT("LateNode")), TEXT("LateNode outlived its provider")));
		ASSERT_THAT(AreEqual(Before->GetCatalogueFingerprint(), After->GetCatalogueFingerprint()));
		ASSERT_THAT(IsTrue(Errors.IsEmpty(), FString::Join(Errors, TEXT("; "))));
	}

	TEST_METHOD(ConfiguredMountsResolveFromTheProjectAndCannotBeRegisteredAgain)
	{
		UADSKSettings* Settings = GetMutableDefault<UADSKSettings>();
		const TMap<FString, FString> Saved = Settings->BehaviorMounts;
		ON_SCOPE_EXIT { GetMutableDefault<UADSKSettings>()->BehaviorMounts = Saved; };
		Settings->BehaviorMounts.Add(TEXT("Configured"), TEXT("Saved/AdamskAIMount"));
		Settings->BehaviorMounts.Add(TEXT("9bad"), TEXT("Saved/Nowhere"));
		FString Directory;
		ASSERT_THAT(IsTrue(FADSKBehaviorRoots::Find(TEXT("Configured"), Directory), TEXT("the configured mount is not found")));
		ASSERT_THAT(AreEqual(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Saved/AdamskAIMount")), Directory));
		ASSERT_THAT(IsFalse(FADSKBehaviorRoots::Find(TEXT("configured"), Directory), TEXT("mount names matched ignoring case")));
		// Configured names are taken: a Game Feature cannot shadow one at runtime.
		ASSERT_THAT(IsFalse(FADSKBehaviorRoots::Register(TEXT("Configured"), FPaths::ProjectSavedDir()), TEXT("a runtime mount shadowed a configured one")));
		const TArray<TPair<FString, FString>> Mounts = FADSKBehaviorRoots::GetAll();
		ASSERT_THAT(IsTrue(Mounts.ContainsByPredicate([](const TPair<FString, FString>& Mount) { return Mount.Key == TEXT("Configured"); }), TEXT("Configured is not listed")));
		ASSERT_THAT(IsFalse(Mounts.ContainsByPredicate([](const TPair<FString, FString>& Mount) { return Mount.Key == TEXT("9bad"); }), TEXT("the invalid 9bad mount is listed")));
		const TArray<FString> Directories = FADSKBehaviorRoots::GetAllDirectories();
		ASSERT_THAT(AreEqual(UADSKSettings::GetBehaviorRootDir(), Directories[0]));
		ASSERT_THAT(IsTrue(Directories.Contains(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Saved/AdamskAIMount"))), FString::Join(Directories, TEXT(", "))));
	}
};

#endif
