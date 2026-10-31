// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Catalogue/ADSKRegistryAssembly.h"
#include "HAL/FileManager.h"
#include "Support/ScriptedRun.h"
#include "Support/TempTree.h"
#include "CQTest.h"

namespace FExamplesRunSpecPrivate
{
	FString ExamplePath(const TCHAR* File)
	{
		return FADSKTestPaths::PluginBase() / TEXT("Examples") / File;
	}

	/** Compiles the example with the plugin catalogue and starts it with the given Vector inputs. */
	FADSKScriptedRun Start(const TCHAR* File, std::initializer_list<TPair<const TCHAR*, FVector>> Inputs)
	{
		FADSKScriptedRun Run(ExamplePath(File), ADSKTest::PluginCatalogue());
		for (const TPair<const TCHAR*, FVector>& Input : Inputs)
		{
			const EADSKInputResult Set = Run.Instance->SetInput(Input.Key, FADSKValue::MakeVector(Input.Value));
			ADSKTest::Expect(Set == EADSKInputResult::Accepted, FString::Printf(TEXT("%s: input %s refused: %s"), File, Input.Key, LexToString(Set)));
		}
		ADSKTest::Expect(ADSKTest::Start(*Run.Instance));
		return Run;
	}
}

using namespace FExamplesRunSpecPrivate;

TEST_CLASS_WITH_FLAGS(FExamplesRunSpec, "AdamskAI.Examples.Run",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(EveryExampleHasAScenario)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *ExamplePath(TEXT("*.xml")), true, false);
		Files.Sort();
		const TArray<FString> Covered = {TEXT("chase_and_wait.xml"), TEXT("emit_handoff.xml"), TEXT("follow_target.xml"),
			TEXT("guard_patrol.xml"), TEXT("guard_post.xml"), TEXT("hunt_actor.xml"), TEXT("retreat_watching.xml"), TEXT("run_reuse.xml"), TEXT("selector_fallback.xml"), TEXT("skirmish.xml")};
		ASSERT_THAT(AreEqual(Covered, Files));
	}

	TEST_METHOD(GuardPatrolWalksPausesAndRestsWhileDisabled)
	{
		FADSKScriptedRun Run = Start(TEXT("guard_patrol.xml"), {{TEXT("PatrolPoint"), FVector(500, 0, 0)}});
		ASSERT_THAT(AreEqual(FString(TEXT("move")), Run.Active()));
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
		ASSERT_THAT(AreEqual(FString(TEXT("pause")), Run.Active()));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.Active() == TEXT("move"); }));
		Run.Instance->SetInput(TEXT("Enabled"), FADSKValue::MakeBool(false));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.State() == TEXT("rest"); }));
		// Disabled, it alternates rest and an immediately failing patrol, never starting a move.
		for (int32 Tick = 0; Tick < 40; ++Tick)
		{
			Run.Tick(0.25f);
			ASSERT_THAT(IsTrue(Run.Active() != TEXT("move"), FString::Printf(TEXT("a move started on tick %d while disabled"), Tick)));
		}
		Run.Instance->SetInput(TEXT("Enabled"), FADSKValue::MakeBool(true));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.Active() == TEXT("move"); }));
		ASSERT_THAT(IsFalse(Run.bEverHeld, TEXT("held in ") + Run.HeldIn));
		ADSK_ASSERT(Run.MatchesGolden(TEXT("guard_patrol")));
	}

	TEST_METHOD(ChaseAndWaitRestsChasesTheSpottedPointAndReturns)
	{
		FADSKScriptedRun Run = Start(TEXT("chase_and_wait.xml"), {{TEXT("Home"), FVector::ZeroVector}});
		ASSERT_THAT(IsFalse(static_cast<bool>(Run.RunUntil([] { return false; }, 0.25f, 20)), TEXT("RunUntil passed a condition that never holds")));
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Run.State()));
		const FADSKNamedValue Where{TEXT("Where"), FADSKValue::MakeVector(FVector(1, 2, 3))};
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Run.Instance->EnqueueEvent(TEXT("Spotted"), MakeArrayView(&Where, 1))));
		Run.Tick(0.f);
		FADSKValue Seen;
		ASSERT_THAT(AreEqual(FString(TEXT("chase")), Run.State()));
		ASSERT_THAT(IsTrue(Run.Instance->GetValue(TEXT("Seen"), Seen), TEXT("no slot Seen")));
		ASSERT_THAT(AreEqual(FVector(1, 2, 3), Seen.Vector));
		ASSERT_THAT(AreEqual(FString(TEXT("face")), Run.Active()));
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
		ASSERT_THAT(AreEqual(FString(TEXT("close")), Run.Active()));
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
		ASSERT_THAT(AreEqual(FString(TEXT("hold")), Run.Active()));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.State() == TEXT("idle"); }));
		// A failed approach and a Lost target both return to rest.
		Run.Instance->EnqueueEvent(TEXT("Spotted"), MakeArrayView(&Where, 1));
		Run.Tick(0.f);
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Failed));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.State() == TEXT("idle"); }));
		Run.Instance->EnqueueEvent(TEXT("Spotted"), MakeArrayView(&Where, 1));
		Run.Tick(0.f);
		Run.Instance->EnqueueEvent(TEXT("Lost"));
		Run.Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Run.State()));
		ASSERT_THAT(IsFalse(Run.bEverHeld, TEXT("held in ") + Run.HeldIn));
		ADSK_ASSERT(Run.MatchesGolden(TEXT("chase_and_wait")));
	}

	TEST_METHOD(SelectorFallbackMovesWhenEnabledAndIdlesOtherwise)
	{
		FADSKScriptedRun Run = Start(TEXT("selector_fallback.xml"), {{TEXT("Goal"), FVector(300, 0, 0)}});
		ASSERT_THAT(AreEqual(FString(TEXT("work-move")), Run.Active()));
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Failed));
		ASSERT_THAT(AreEqual(FString(TEXT("work-idle")), Run.Active()));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.Active() == TEXT("work-move"); }));
		Run.Instance->SetInput(TEXT("Enabled"), FADSKValue::MakeBool(false));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.Active() == TEXT("work-idle"); }));
		for (int32 Tick = 0; Tick < 20; ++Tick)
		{
			Run.Tick(0.25f);
			ASSERT_THAT(IsTrue(Run.Active() != TEXT("work-move"), FString::Printf(TEXT("a move started on tick %d while disabled"), Tick)));
		}
		ASSERT_THAT(IsFalse(Run.bEverHeld, TEXT("held in ") + Run.HeldIn));
		ADSK_ASSERT(Run.MatchesGolden(TEXT("selector_fallback")));
	}

	TEST_METHOD(EmitHandoffMovesOnTheNextTickAndLoops)
	{
		FADSKScriptedRun Run = Start(TEXT("emit_handoff.xml"), {});
		ASSERT_THAT(AreEqual(FString(TEXT("first")), Run.State()));
		Run.Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("second")), Run.State()));
		ASSERT_THAT(AreEqual(1, Run.Count(EADSKRecordKind::CompletionStale)));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.State() == TEXT("first"); }));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.State() == TEXT("second"); }));
		ASSERT_THAT(IsFalse(Run.bEverHeld, TEXT("held in ") + Run.HeldIn));
		ADSK_ASSERT(Run.MatchesGolden(TEXT("emit_handoff")));
	}

	TEST_METHOD(RunReuseVisitsBothCallSitesInOrder)
	{
		FADSKScriptedRun Run = Start(TEXT("run_reuse.xml"), {{TEXT("Goal"), FVector(10, 0, 0)}});
		ASSERT_THAT(AreEqual(FString(TEXT("first/approach-move")), Run.Active()));
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
		ASSERT_THAT(AreEqual(FString(TEXT("first/approach-pause")), Run.Active()));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.Active() == TEXT("second/approach-move"); }));
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.Active() == TEXT("first/approach-move"); }));
		ASSERT_THAT(IsFalse(Run.bEverHeld, TEXT("held in ") + Run.HeldIn));
		ADSK_ASSERT(Run.MatchesGolden(TEXT("run_reuse")));
	}

	TEST_METHOD(SkirmishClosesInFromAfarAndMixesStrafeAndHoldInReach)
	{
		FADSKScriptedRun Run = Start(TEXT("skirmish.xml"), {{TEXT("TargetLocation"), FVector(1000, 0, 0)}});
		ADSK_ASSERT(Run.RunUntil([&] { return Run.Active() == TEXT("close_move"); }));
		Run.Instance->SetInput(TEXT("TargetDistance"), FADSKValue::MakeFloat(50.f));
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
		int32 Strafes = 0, Holds = 0;
		for (int32 Round = 0; Round < 40; ++Round)
		{
			ADSK_ASSERT(Run.RunUntil([&] { return Run.State() == TEXT("strafe") || Run.State() == TEXT("hold"); }));
			ASSERT_THAT(IsTrue(Run.State() != TEXT("close"), FString::Printf(TEXT("back in close in round %d"), Round)));
			if (Run.State() == TEXT("strafe"))
			{
				++Strafes;
				ASSERT_THAT(AreEqual(FString(TEXT("strafe_move")), Run.Active()));
				ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
				ADSK_ASSERT(Run.RunUntil([&] { return Run.State() != TEXT("strafe"); }));
			}
			else
			{
				++Holds;
				ADSK_ASSERT(Run.RunUntil([&] { return Run.State() != TEXT("hold"); }));
			}
		}
		ASSERT_THAT(IsTrue(Strafes > 0 && Holds > 0, FString::Printf(TEXT("%d strafes and %d holds"), Strafes, Holds)));
		ASSERT_THAT(IsFalse(Run.bEverHeld, TEXT("held in ") + Run.HeldIn));
		ADSK_ASSERT(Run.MatchesGolden(TEXT("skirmish")));
	}

	TEST_METHOD(FollowTargetClosesInTauntsInReachAndFollowsWhenItLeaves)
	{
		FADSKScriptedRun Run = Start(TEXT("follow_target.xml"), {{TEXT("TargetLocation"), FVector(2000, 0, 0)}});
		ASSERT_THAT(AreEqual(FString(TEXT("follow_move")), Run.Active()));
		Run.Instance->SetInput(TEXT("TargetDistance"), FADSKValue::MakeFloat(80.f));
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
		int32 Taunts = 0, Watches = 0;
		for (int32 Round = 0; Round < 60 && (Taunts == 0 || Watches == 0); ++Round)
		{
			ADSK_ASSERT(Run.RunUntil([&] { return Run.Active() == TEXT("near_taunt_wait") || Run.Active() == TEXT("near_watch"); }));
			(Run.Active() == TEXT("near_taunt_wait") ? Taunts : Watches) += 1;
			const FString Current = Run.Active();
			ADSK_ASSERT(Run.RunUntil([&] { return Run.Active() != Current; }));
		}
		ASSERT_THAT(IsTrue(Taunts > 0 && Watches > 0, FString::Printf(TEXT("%d taunts and %d watches"), Taunts, Watches)));
		// The guard is reactive: leaving Reach ends the near routine mid-wait.
		Run.Instance->SetInput(TEXT("TargetDistance"), FADSKValue::MakeFloat(900.f));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.Active() == TEXT("follow_move"); }, 0.f, 4));
		// An unreachable target pauses instead of re-pathing every tick.
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Failed));
		ASSERT_THAT(AreEqual(FString(TEXT("follow_pause")), Run.Active()));
		ASSERT_THAT(IsFalse(Run.bEverHeld, TEXT("held in ") + Run.HeldIn));
		ADSK_ASSERT(Run.MatchesGolden(TEXT("follow_target")));
	}

	TEST_METHOD(GuardPostScansAtMostEveryFourSecondsAndEndsItsShiftAtTen)
	{
		FADSKScriptedRun Run = Start(TEXT("guard_post.xml"), {{TEXT("Post"), FVector(300, 0, 0)}});
		TArray<double> Scans;
		double Time = 0.0;
		while (Run.State() == TEXT("watch") && Time < 12.0)
		{
			// The scan is a host double that only the test finishes; it looks for one tick.
			if (Run.Active() == TEXT("watch_scan"))
			{
				Scans.Add(Time);
				ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
			}
			Run.Tick(0.25f);
			Time += 0.25;
		}
		ASSERT_THAT(IsTrue(Scans.Num() >= 1, TEXT("the post was never scanned")));
		for (int32 Index = 1; Index < Scans.Num(); ++Index)
		{
			ASSERT_THAT(IsTrue(Scans[Index] - Scans[Index - 1] >= 4.0, FString::Printf(TEXT("scans at %.2f s and %.2f s"), Scans[Index - 1], Scans[Index])));
		}
		// TimeLimit ended the shift at ten seconds, cancelling whatever ran.
		ASSERT_THAT(IsTrue(Time >= 10.0 && Time <= 10.75, FString::Printf(TEXT("the shift ended at %.2f s"), Time)));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.Active() == TEXT("return_move"); }, 0.f, 4));
		// A blocked walk still counts as back at the post.
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Failed));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.State() == TEXT("watch"); }, 0.f, 4));
		ASSERT_THAT(IsFalse(Run.bEverHeld, TEXT("held in ") + Run.HeldIn));
		ADSK_ASSERT(Run.MatchesGolden(TEXT("guard_post")));
	}

	TEST_METHOD(RetreatWatchingMovesAndWatchesSideBySide)
	{
		FADSKScriptedRun Run = Start(TEXT("retreat_watching.xml"), {{TEXT("Cover"), FVector(-500, 0, 0)}});
		TArray<FName> Ids;
		TArray<FADSKActivationHandle> Handles;
		// No threat: only the move runs; the guarded watch fails and loops quietly beside it.
		Run.Tick(0.1f);
		Run.Instance->GetActiveNodes(Ids, Handles);
		ASSERT_THAT(AreEqual(TArray<FName>({TEXT("retreat_move")}), Ids));
		Run.Instance->SetInput(TEXT("Threat"), FADSKValue::MakeObject(9));
		ADSK_ASSERT(Run.RunUntil([&] { Run.Instance->GetActiveNodes(Ids, Handles); return Ids.Num() == 2; }, 0.1f, 3));
		ASSERT_THAT(AreEqual(TArray<FName>({TEXT("retreat_move"), TEXT("retreat_look")}), Ids));
		// The watch ends on its own and starts again; the move is untouched.
		const FADSKActivationHandle Move = Run.Instance->GetActiveLeafHandle();
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Run.Instance->PostActionResult(Handles[1], EADSKNodeStatus::Succeeded)));
		Run.Tick(0.1f);
		ADSK_ASSERT(Run.RunUntil([&] { Run.Instance->GetActiveNodes(Ids, Handles); return Ids.Num() == 2; }, 0.1f, 3));
		ASSERT_THAT(IsTrue(Run.Instance->GetActiveLeafHandle() == Move, TEXT("the move was restarted")));
		// Reaching cover ends the retreat and the watch with it.
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.State() == TEXT("hold"); }, 0.f, 4));
		Run.Instance->GetActiveNodes(Ids, Handles);
		ASSERT_THAT(AreEqual(TArray<FName>({TEXT("hold_wait")}), Ids));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.Active() == TEXT("retreat_move"); }, 0.5f, 10));
		ASSERT_THAT(IsFalse(Run.bEverHeld, TEXT("held in ") + Run.HeldIn));
		ADSK_ASSERT(Run.MatchesGolden(TEXT("retreat_watching")));
	}

	TEST_METHOD(HuntActorChasesTheActorAndKeepsAtTheMarkedOne)
	{
		FADSKScriptedRun Run = Start(TEXT("hunt_actor.xml"), {});
		ASSERT_THAT(IsFalse(static_cast<bool>(Run.RunUntil([&] { return Run.State() != TEXT("search"); }, 0.25f, 12)), TEXT("left search before a target was set")));
		const FADSKValue Actor = FADSKValue::MakeObject(3);
		const auto ActiveIs = [&Run](const TCHAR* Node) { return Run.RunUntil([&Run, Node] { return Run.Active() == Node; }, 0.f, 4); };
		// The change event starts the chase on the next tick, not when the search wait ends.
		Run.Instance->SetInput(TEXT("Target"), Actor);
		Run.Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("chase_move")), Run.Active()));
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
		ADSK_ASSERT(ActiveIs(TEXT("strike_look")));
		// Not marked: back to the chase.
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
		ADSK_ASSERT(ActiveIs(TEXT("chase_move")));
		const FADSKNamedValue Who{TEXT("who"), Actor};
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Run.Instance->EnqueueEvent(TEXT("Mark"), MakeArrayView(&Who, 1))));
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
		ADSK_ASSERT(ActiveIs(TEXT("strike_look")));
		// Marked: it keeps at this one, so the strike state re-enters.
		const uint64 Before = Run.Instance->GetActiveLeafHandle().ActivationId;
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Succeeded));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.Active() == TEXT("strike_look") && Run.Instance->GetActiveLeafHandle().ActivationId != Before; }, 0.f, 4));
		ASSERT_THAT(AreEqual(FString(TEXT("strike")), Run.State()));
		ASSERT_THAT(AreEqual(FString(TEXT("strike_look")), Run.Active()));
		// A target that is gone fails the facing: search again.
		ADSK_ASSERT(Run.Finish(EADSKNodeStatus::Failed));
		ADSK_ASSERT(Run.RunUntil([&] { return Run.State() == TEXT("search"); }, 0.f, 4));
		// Losing the target mid-chase ends the chase without waiting for the move.
		Run.Instance->SetInput(TEXT("Target"), FADSKValue::MakeObject(4));
		Run.Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("chase_move")), Run.Active()));
		Run.Instance->SetInput(TEXT("Target"), FADSKValue::MakeObject(0));
		Run.Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("search")), Run.State()));
		ASSERT_THAT(IsFalse(Run.bEverHeld, TEXT("held in ") + Run.HeldIn));
		ADSK_ASSERT(Run.MatchesGolden(TEXT("hunt_actor")));
	}
};

#endif
