// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "World/WorldScenario.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Settings/ADSKSettings.h"
#include "CQTest.h"

// Reload and prewarm in a live world: the editor's directory watcher sees a file write (no InjectChangesForTest), the
// subsystem compiles on a worker and publishes in the world's pre-actor tick (no CommitPendingReloads).
namespace FReloadSpecPrivate
{
	const FTimespan Timeout = FTimespan::FromSeconds(30);
	const TCHAR* LiveFile = TEXT("live.xml");

	/** Starts in Initial and waits there; the Marker input shows whether inputs survive a reload. */
	FString LiveXml(const TCHAR* Initial)
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"live\"><Inputs><Input name=\"Marker\" type=\"Float\" default=\"0\"/></Inputs>"
			"<StateMachine id=\"m\" initial=\"%s\"><State id=\"a\" tree=\"t\"/><State id=\"b\" tree=\"t\"/></StateMachine>"
			"<Tree id=\"t\"><Wait id=\"w\" duration=\"1000\"/></Tree></Agent>"), Initial);
	}
}

using namespace FReloadSpecPrivate;

TEST_CLASS_WITH_FLAGS(FWorldReloadSpec, "AdamskAIWorld.Reload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKWorldScenario Scenario;
	FADSKWorldAgent Agent;
	FString Source;
	TSharedPtr<const FADSKDefinition> Published;
	TArray<FString> SavedPrewarm;
	double Mark = 0.0;

	BEFORE_EACH()
	{
		SavedPrewarm = GetDefault<UADSKSettings>()->PrewarmFiles;
		// Mounted and written before PIE starts, so the world's prewarm and the editor's watcher both see it.
		Source = Scenario.WriteBehavior(LiveFile, LiveXml(TEXT("a")));
		GetMutableDefault<UADSKSettings>()->PrewarmFiles.Add(Source);
		Scenario.Start(TestRunner);
	}

	AFTER_EACH()
	{
		GetMutableDefault<UADSKSettings>()->PrewarmFiles = SavedPrewarm;
	}

	UADSKWorldSubsystem& Subsystem() { return *Scenario.GetWorld().GetSubsystem<UADSKWorldSubsystem>(); }

	/** I14: the world compiled the file before any brain asked for it, and the first Acquire hands out that one. */
	TEST_METHOD(PrewarmFilesAreReadyBeforeTheFirstBrain)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("prewarmed"), [this] { return !Subsystem().IsPrewarming(Source); }, Timeout)
			.Then(TEXT("published, and Acquire returns it"), [this]
			{
				const TSharedPtr<const FADSKDefinition> Prewarmed = Subsystem().GetActiveDefinition(Source);
				ASSERT_THAT(IsTrue(Prewarmed.IsValid(), TEXT("nothing was prewarmed for ") + Source));
				ASSERT_THAT(IsTrue(Subsystem().GetBrains(Source).IsEmpty(), FString::Printf(TEXT("%d brains run %s"), Subsystem().GetBrains(Source).Num(), *Source)));
				FString Error;
				ASSERT_THAT(IsTrue(Subsystem().Acquire(Source, Error) == Prewarmed, TEXT("Acquire compiled again: ") + Error));
				ASSERT_THAT(AreEqual(0, Subsystem().GetSynchronousCompileCount(), TEXT("a prewarmed file counted as a synchronous compile")));
				// A file nobody prewarmed compiles at its first Acquire, and is counted (and logged) once.
				const FString Cold = FADSKWorldScenario::Behavior(TEXT("world_wait.xml"));
				ASSERT_THAT(IsTrue(Subsystem().Acquire(Cold, Error).IsValid(), TEXT("cannot acquire ") + Cold + TEXT(": ") + Error));
				ASSERT_THAT(IsTrue(Subsystem().Acquire(Cold, Error).IsValid(), TEXT("cannot acquire ") + Cold + TEXT(" again: ") + Error));
				ASSERT_THAT(AreEqual(1, Subsystem().GetSynchronousCompileCount(), TEXT("synchronous compiles")));
			});
	}

	/** I13: a save restarts the agent on the new file with its inputs kept; a broken save keeps the old definition. */
	TEST_METHOD(ASavedFileReloadsTheAgentAndABrokenOneIsRejected)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("spawn on the first version"), [this]
			{
				Agent = Scenario.SpawnAgent(FVector(-1500.0, -1500.0, ADSKWorldLayout::StandZ), Source, {.Inputs = {{TEXT("Marker"), FADSKValue::MakeFloat(7.f)}}});
				Published = Subsystem().GetActiveDefinition(Source);
				Mark = Scenario.Now();
			})
			.Until(TEXT("the watcher settles"), [this] { return Scenario.Now() - Mark >= 0.5; }, Timeout)
			.Then(TEXT("save the second version"), [this]
			{
				ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("a")}));
				Scenario.WriteBehavior(LiveFile, LiveXml(TEXT("b")));
			})
			.Until(TEXT("reloaded"), [this] { return Agent.Brain->GetInstance() && &*Agent.Brain->GetInstance()->GetDefinition() != Published.Get(); }, Timeout)
			.Then(TEXT("restarted where it was with the inputs kept; save a broken file"), [this]
			{
				// bKeepStateOnReload: the new initial state b does not move a running agent out of a.
				ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("a")}));
				ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Agent.Value(TEXT("Marker")).Float, 7.f), TEXT("the reload dropped the Marker input")));
				ASSERT_THAT(IsTrue(Subsystem().GetLastAsyncReloadResult(Source) == EADSKReloadResult::Queued, TEXT("the reload was not queued; diagnostics: ") + Subsystem().GetLastDiagnostics(Source)));
				Published = Subsystem().GetActiveDefinition(Source);
				Scenario.WriteBehavior(LiveFile, TEXT("<Agent"));
			})
			.Until(TEXT("rejected"), [this] { return Subsystem().GetLastAsyncReloadResult(Source) == EADSKReloadResult::Rejected; }, Timeout)
			.Then(TEXT("still on the second version"), [this]
			{
				ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("a")}));
				ASSERT_THAT(IsTrue(Subsystem().GetActiveDefinition(Source) == Published, TEXT("the broken save replaced the published definition")));
				ASSERT_THAT(IsTrue(&*Agent.Brain->GetInstance()->GetDefinition() == Published.Get(), TEXT("the agent left the published definition")));
				ASSERT_THAT(IsFalse(Subsystem().GetLastDiagnostics(Source).IsEmpty(), TEXT("a rejected reload left no diagnostics")));
			});
	}
};

#endif
