// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "Validate/ADSKStaging.h"
#include "CQTest.h"

TEST_CLASS_WITH_FLAGS(FStagingSpec, "AdamskAI.Validate.Staging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(StagingCoversRootOrAParentDirectory)
	{
		ASSERT_THAT(IsTrue(ADSKStaging::IsStagedBy(TEXT("AI"), {TEXT("(Path=\"/Game/Tests\")"), TEXT("(Path=\"AI\")")}), TEXT("AI is not staged by its own path")));
		ASSERT_THAT(IsTrue(ADSKStaging::IsStagedBy(TEXT("Game/AI"), {TEXT("(Path=\"Game\")")}), TEXT("Game/AI is not staged by its parent")));
		ASSERT_THAT(IsTrue(ADSKStaging::IsStagedBy(TEXT("Game/AI"), {TEXT("(Path=\"/game\\ai/\")")}), TEXT("Game/AI is not staged by a differently cased and separated path")));
		ASSERT_THAT(IsTrue(ADSKStaging::IsStagedBy(TEXT("AI"), {TEXT("(Path=\"\")")}), TEXT("AI is not staged by the empty path")));
		ASSERT_THAT(IsFalse(ADSKStaging::IsStagedBy(TEXT("AI"), {}), TEXT("AI is staged by nothing")));
		ASSERT_THAT(IsFalse(ADSKStaging::IsStagedBy(TEXT("AI"), {TEXT("(Path=\"AIExtra\")"), TEXT("(Path=\"AI/enemies\")"), TEXT("garbage")}), TEXT("AI is staged by a sibling, a child or garbage")));
		ASSERT_THAT(IsFalse(ADSKStaging::IsStagedBy(TEXT("AIExtra"), {TEXT("(Path=\"AI\")")}), TEXT("AIExtra is staged by AI")));
	}
};

#endif
