// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Settings/ADSKSettings.h"
#include "Trace/ADSKTrace.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "CQTest.h"
#include "Misc/Paths.h"

namespace FSettingsSpecPrivate
{
	/** Restores the mutable settings CDO, which every other test and the running editor share. */
	struct FSettingsRestore
	{
		UADSKSettings* Settings = GetMutableDefault<UADSKSettings>();
		FString BehaviorRoot = Settings->BehaviorRoot;
		int32 RecorderCapacity = Settings->RecorderCapacity;
		~FSettingsRestore()
		{
			Settings->BehaviorRoot = BehaviorRoot;
			Settings->RecorderCapacity = RecorderCapacity;
		}
	};

	FString RootFor(UADSKSettings* Settings, const TCHAR* Value)
	{
		Settings->BehaviorRoot = Value;
		return UADSKSettings::GetBehaviorRoot();
	}
}

using namespace FSettingsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FSettingsSpec, "AdamskAI.Host.Settings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(BehaviorRootAcceptsContentRelativeDirectories)
	{
		FSettingsRestore Restore;
		ASSERT_THAT(AreEqual(FString(TEXT("AI")), RootFor(Restore.Settings, TEXT("AI"))));
		ASSERT_THAT(AreEqual(FString(TEXT("Game/Behaviors")), RootFor(Restore.Settings, TEXT("Game/Behaviors/"))));
		ASSERT_THAT(AreEqual(FString(TEXT("Game/Behaviors")), RootFor(Restore.Settings, TEXT("Game\\Behaviors"))));
		ASSERT_THAT(AreEqual(FString(TEXT("Game/AI")), RootFor(Restore.Settings, TEXT("Game//AI"))));
		ASSERT_THAT(AreEqual(FPaths::ProjectContentDir() / TEXT("Game/AI"), UADSKSettings::GetBehaviorRootDir()));
	}

	TEST_METHOD(BehaviorRootFallsBackToDefaultOutsideContent)
	{
		FSettingsRestore Restore;
		for (const TCHAR* Value : {TEXT(""), TEXT("/"), TEXT("/abs/AI"), TEXT("C:/AI"), TEXT("C:AI"), TEXT(".."), TEXT("../AI"), TEXT("Game/../AI"), TEXT("./AI"), TEXT("Game\\..\\AI")})
		{
			ASSERT_THAT(AreEqual(FString(TEXT("AI")), RootFor(Restore.Settings, Value)));
		}
	}

	TEST_METHOD(RecorderCapacityControlsBrainRecorder)
	{
		FSettingsRestore Restore;
		const UADSKWorldSubsystem* Subsystem = NewObject<UADSKWorldSubsystem>();

		Restore.Settings->RecorderCapacity = 64;
		ASSERT_THAT(AreEqual(64, UADSKSettings::GetRecorderCapacity()));
		const TSharedPtr<FADSKRecorder> Recorder = Subsystem->CreateBrainRecorder();
		ASSERT_THAT(IsTrue(Recorder.IsValid(), TEXT("no recorder at capacity 64")));
		ASSERT_THAT(AreEqual(64, Recorder->GetCapacity()));

		Restore.Settings->RecorderCapacity = 0;
		ASSERT_THAT(IsFalse(Subsystem->CreateBrainRecorder().IsValid(), TEXT("a recorder at capacity 0")));

		// The ini is not clamped by the editor's ClampMin; a hand-written negative value means off.
		Restore.Settings->RecorderCapacity = -5;
		ASSERT_THAT(AreEqual(0, UADSKSettings::GetRecorderCapacity()));
		ASSERT_THAT(IsFalse(Subsystem->CreateBrainRecorder().IsValid(), TEXT("a recorder at capacity -5")));
	}
};

#endif
