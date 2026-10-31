// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "Cook/ADSKAssetRenames.h"
#include "Support/TempTree.h"
#include "CQTest.h"

TEST_CLASS_WITH_FLAGS(FAssetRenamesSpec, "AdamskAI.Validate.AssetRenames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(OnlyWholeQuotedPathsAndTheirClassesAreRewritten)
	{
		FString Text = TEXT("<RunEQS query=\"/Game/AI/Q.Q\"/><RunEQS query='/Game/AI/Q.Q'/><Spawn spawn=\"/Game/AI/Q.Q_C\"/>"
			"<RunEQS query=\"/Game/AI/Q.Q2\"/><Note text=\"see /Game/AI/Q.Q\"/>");
		ASSERT_THAT(AreEqual(3, FADSKAssetRenames::RewriteText(Text, TEXT("/Game/AI/Q.Q"), TEXT("/Game/EQS/Flee.Flee"))));
		ASSERT_THAT(AreEqual(FString(TEXT("<RunEQS query=\"/Game/EQS/Flee.Flee\"/><RunEQS query='/Game/EQS/Flee.Flee'/><Spawn spawn=\"/Game/EQS/Flee.Flee_C\"/>"
			"<RunEQS query=\"/Game/AI/Q.Q2\"/><Note text=\"see /Game/AI/Q.Q\"/>")), Text));
		ASSERT_THAT(AreEqual(0, FADSKAssetRenames::RewriteText(Text, TEXT("/Game/EQS/Flee.Flee"), TEXT("/Game/EQS/Flee.Flee"))));
		ASSERT_THAT(AreEqual(0, FADSKAssetRenames::RewriteText(Text, FString(), TEXT("/Game/X.X"))));
	}

	TEST_METHOD(FilesThatNameTheOldPathAreRewrittenAndOthersAreLeftAlone)
	{
		const FADSKTempTree Tree;
		Tree.Write(TEXT("main/a.xml"), TEXT("<RunEQS query=\"/Game/AI/Q.Q\"/>"));
		Tree.Write(TEXT("main/sub/b.xml"), TEXT("<RunEQS query=\"/Game/AI/Other.Other\"/>"));
		Tree.Write(TEXT("mount/c.xml"), TEXT("<RunEQS query=\"/Game/AI/Q.Q\"/>"));
		const TArray<FString> Roots = {Tree.Path(TEXT("main")), Tree.Path(TEXT("mount"))};
		const TArray<FString> Changed = FADSKAssetRenames::RewriteFiles(Roots, TEXT("/Game/AI/Q.Q"), TEXT("/Game/EQS/Q.Q"));
		ASSERT_THAT(AreEqual(2, Changed.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("<RunEQS query=\"/Game/EQS/Q.Q\"/>")), Tree.Read(TEXT("main/a.xml"))));
		ASSERT_THAT(AreEqual(FString(TEXT("<RunEQS query=\"/Game/AI/Other.Other\"/>")), Tree.Read(TEXT("main/sub/b.xml"))));
		ASSERT_THAT(AreEqual(FString(TEXT("<RunEQS query=\"/Game/EQS/Q.Q\"/>")), Tree.Read(TEXT("mount/c.xml"))));
	}
};

#endif
