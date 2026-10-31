// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Nodes/ADSKBuiltinNodes.h"
#include "Support/ExecutionTestSupport.h"
#include "CQTest.h"

namespace FSnapshotSpecPrivate
{
	/**
	 * Go(V) binds V to Kept and enters work/inner; Roll rolls into Rolled. The cooldown's ready time is node state,
	 * Watched raises Changed.
	 */
	FString Agent(const TCHAR* KeptType = TEXT("Float"), const TCHAR* InnerId = TEXT("inner"), const TCHAR* MoodValues = TEXT("<Value name=\"Calm\"/><Value name=\"Angry\"/>"))
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"snap\"><Enums><Enum name=\"Mood\">%s</Enum></Enums>"
			"<Locals><Local name=\"Kept\" type=\"%s\"/><Local name=\"Rolled\" type=\"Int\"/><Local name=\"Feeling\" type=\"Mood\"/>"
			"<Local name=\"Watched\" type=\"Int\"/></Locals>"
			"<Events><Event name=\"Go\"><Param name=\"V\" type=\"%s\"/></Event><Event name=\"Angry\"><Param name=\"M\" type=\"Mood\"/></Event>"
			"<Event name=\"Roll\"/><Event name=\"Touch\"><Param name=\"W\" type=\"Int\"/></Event><Event name=\"Changed\" change=\"Watched\"/></Events>"
			"<StateMachine id=\"m\" initial=\"idle\"><On event=\"Changed\" goto=\"changed\"/>"
			"<On event=\"Angry\"><Bind param=\"M\" to=\"Feeling\"/></On><On event=\"Touch\"><Bind param=\"W\" to=\"Watched\"/></On>"
			"<State id=\"idle\" tree=\"cool\"><On event=\"Go\" goto=\"%s\"><Bind param=\"V\" to=\"Kept\"/></On></State>"
			"<State id=\"work\" initial=\"%s\"><State id=\"%s\" tree=\"wait\"><On event=\"Roll\" goto=\"roller\"/></State></State>"
			"<State id=\"roller\" tree=\"roll\"/><State id=\"changed\" tree=\"wait\"/></StateMachine>"
			"<Tree id=\"cool\"><Sequence id=\"cool-seq\"><Cooldown id=\"cd\" duration=\"100\"><Wait id=\"cd-wait\" duration=\"0\"/></Cooldown>"
			"<Wait id=\"idle-wait\" duration=\"100\"/></Sequence></Tree>"
			"<Tree id=\"wait\"><Wait id=\"w\" duration=\"100\"/></Tree>"
			"<Tree id=\"roll\"><Sequence id=\"roll-seq\"><Roll id=\"r\" count=\"1000000\" out-value=\"Rolled\"/><Wait id=\"rw\" duration=\"100\"/></Sequence></Tree></Agent>"),
			MoodValues, KeptType, KeptType, InnerId, InnerId, InnerId);
	}

	int32 RolledAfterRoll(FADSKInstance& Instance)
	{
		Instance.EnqueueEvent(TEXT("Roll"));
		Instance.Tick(0.f);
		FADSKValue Rolled;
		Instance.GetValue(TEXT("Rolled"), Rolled);
		return Rolled.Int;
	}
}

using namespace FSnapshotSpecPrivate;

TEST_CLASS_WITH_FLAGS(FSnapshotSpec, "AdamskAI.Execution.Snapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ARestoredInstanceContinuesWhereTheOriginalWas)
	{
		FADSKTestRuntime Original(Agent());
		Original.Instance->SetRandomSeed(5);
		Original.Instance->Start();
		Original.Instance->Tick(0.5f);
		Original.Instance->EnqueueEvent(TEXT("Angry"), {ADSKParam(TEXT("M"), FADSKValue::MakeEnum(0, 1))});
		Original.Instance->Tick(1.f);
		Original.Instance->EnqueueEvent(TEXT("Go"), {ADSKParam(TEXT("V"), FADSKValue::MakeFloat(7.f))});
		Original.Instance->Tick(1.f);
		ADSK_ASSERT(Original.PathIs({TEXT("m"), TEXT("work"), TEXT("inner")}));

		FADSKSnapshot Snapshot;
		ASSERT_THAT(IsTrue(Original.Instance->CaptureSnapshot(Snapshot), TEXT("capture refused")));
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("m"), TEXT("work"), TEXT("inner")}), Snapshot.StatePath));
		ASSERT_THAT(IsTrue(Snapshot.Clock == 2.5, FString::Printf(TEXT("clock %f"), Snapshot.Clock)));
		ASSERT_THAT(AreEqual(1, Snapshot.NodeStates.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("cd")), Snapshot.NodeStates[0].Node));

		FADSKTestRuntime Restored(Agent());
		Restored.Instance->SetRandomSeed(99);
		const FADSKStartReport Report = Restored.Instance->Start(Snapshot);
		ASSERT_THAT(IsTrue(Report.IsSuccess() && Report.Dropped.IsEmpty(), FString::Join(Report.Dropped, TEXT(", "))));
		ADSK_ASSERT(Restored.PathIs({TEXT("m"), TEXT("work"), TEXT("inner")}));
		ASSERT_THAT(IsTrue(Restored.Recorder->Snapshot()[0].Reason == EADSKRecordReason::Restored, Restored.Trace()));
		FADSKValue Kept;
		FADSKValue Feeling;
		Restored.Instance->GetValue(TEXT("Kept"), Kept);
		Restored.Instance->GetValue(TEXT("Feeling"), Feeling);
		ASSERT_THAT(IsTrue(Kept == FADSKValue::MakeFloat(7.f), LexToString(Kept)));
		ASSERT_THAT(IsTrue(Feeling == FADSKValue::MakeEnum(0, 1), LexToString(Feeling)));

		FADSKSnapshot Again;
		ASSERT_THAT(IsTrue(Restored.Instance->CaptureSnapshot(Again), TEXT("capture refused")));
		ASSERT_THAT(IsTrue(Again.Clock == Snapshot.Clock && Again.RandomState == Snapshot.RandomState, TEXT("clock or stream not continued")));
		ASSERT_THAT(IsTrue(Again.NodeStates[0].State.Identical(&Snapshot.NodeStates[0].State, PPF_None), TEXT("cooldown state not restored")));

		// Both draw the same next number: the stream continues where it was, whatever the new seed.
		ASSERT_THAT(AreEqual(RolledAfterRoll(*Original.Instance), RolledAfterRoll(*Restored.Instance)));
	}

	TEST_METHOD(AnEditedDefinitionTakesWhatStillMatchesByName)
	{
		FADSKTestRuntime Original(Agent());
		Original.Instance->Start();
		Original.Instance->EnqueueEvent(TEXT("Angry"), {ADSKParam(TEXT("M"), FADSKValue::MakeEnum(0, 1))});
		Original.Instance->Tick(0.f);
		Original.Instance->EnqueueEvent(TEXT("Go"), {ADSKParam(TEXT("V"), FADSKValue::MakeFloat(7.f))});
		Original.Instance->Tick(0.f);
		FADSKSnapshot Snapshot;
		Original.Instance->CaptureSnapshot(Snapshot);

		// Kept became an Int, inner was renamed, Mood's values were reordered: Feeling keeps its name, Angry.
		FADSKTestRuntime Edited(Agent(TEXT("Int"), TEXT("renamed"), TEXT("<Value name=\"Angry\"/><Value name=\"Calm\"/>")));
		const FADSKStartReport Report = Edited.Instance->Start(Snapshot);
		ASSERT_THAT(IsTrue(Report.IsSuccess(), LexToString(Report.Result)));
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("local Kept"), TEXT("state inner")}), Report.Dropped));
		ADSK_ASSERT(Edited.PathIs({TEXT("m"), TEXT("work"), TEXT("renamed")}));
		FADSKValue Feeling;
		Edited.Instance->GetValue(TEXT("Feeling"), Feeling);
		ASSERT_THAT(IsTrue(Feeling == FADSKValue::MakeEnum(0, 0), LexToString(Feeling)));
	}

	TEST_METHOD(ChangeBaselinesSurviveARestore)
	{
		FADSKTestRuntime Original(Agent());
		Original.Instance->Start();
		Original.Instance->EnqueueEvent(TEXT("Touch"), {ADSKParam(TEXT("W"), FADSKValue::MakeInt(3))});
		Original.Instance->Tick(0.f);
		// Watched is 3 and its change not raised yet: the restored instance still raises it.
		FADSKSnapshot Pending;
		Original.Instance->CaptureSnapshot(Pending);
		FADSKTestRuntime Raises(Agent());
		Raises.Instance->Start(Pending);
		Raises.Instance->Tick(0.f);
		ADSK_ASSERT(Raises.PathIs({TEXT("m"), TEXT("changed")}));

		Original.Instance->Tick(0.f);
		ADSK_ASSERT(Original.PathIs({TEXT("m"), TEXT("changed")}));
		FADSKSnapshot Snapshot;
		Original.Instance->CaptureSnapshot(Snapshot);
		// Back in idle, with Watched 3 already raised for.
		Snapshot.StatePath = {TEXT("m"), TEXT("idle")};

		FADSKTestRuntime Restored(Agent());
		Restored.Instance->Start(Snapshot);
		Restored.Instance->Tick(0.f);
		Restored.Instance->Tick(0.f);
		ADSK_ASSERT(Restored.PathIs({TEXT("m"), TEXT("idle")}));
	}

	TEST_METHOD(StopAndCaptureKeepsWhatTheCancelledNodesWrote)
	{
		// The cooldown's child is still running when the brain stops, as at a live reload mid-attack.
		const FString Xml = TEXT("<Agent version=\"1\" id=\"cd\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>"
			"<Tree id=\"t\"><Cooldown id=\"cd\" duration=\"100\"><Wait id=\"w\" duration=\"50\"/></Cooldown></Tree></Agent>");
		FADSKTestRuntime Original(Xml);
		Original.Instance->Start();
		Original.Instance->Tick(1.f);
		FADSKSnapshot Running;
		ASSERT_THAT(IsTrue(Original.Instance->CaptureSnapshot(Running), TEXT("capture refused")));
		FADSKSnapshot Stopped;
		ASSERT_THAT(IsTrue(Original.Instance->StopAndCaptureSnapshot(Stopped), TEXT("stop and capture refused")));
		ASSERT_THAT(IsFalse(Original.Instance->IsRunning(), TEXT("still running")));
		ASSERT_THAT(IsTrue(Running.NodeStates[0].State.Get<FADSKCooldownState>().ReadyAt == 0.0, TEXT("the running capture already has a ready time")));
		ASSERT_THAT(IsTrue(Stopped.NodeStates[0].State.Get<FADSKCooldownState>().ReadyAt == 101.0, FString::Printf(TEXT("ready at %f"), Stopped.NodeStates[0].State.Get<FADSKCooldownState>().ReadyAt)));

		FADSKTestRuntime Restored(Xml);
		Restored.Instance->Start(Stopped);
		Restored.Instance->Tick(1.f);
		const int32 Child = Restored.Definition->FindNode(TEXT("w"));
		ASSERT_THAT(AreEqual(0, Restored.Recorder->Snapshot().FilterByPredicate([Child](const FADSKRecord& Record)
			{ return Record.Kind == EADSKRecordKind::NodeStarted && Record.Node == Child; }).Num(), Restored.Trace()));
	}

	TEST_METHOD(AHeldInstanceStaysHeldAfterARestore)
	{
		const FString Xml = TEXT("<Agent version=\"1\" id=\"held\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>"
			"<Tree id=\"t\"><Wait id=\"w\" duration=\"0\"/></Tree></Agent>");
		FADSKTestRuntime Original(Xml);
		Original.Instance->Start();
		for (int32 Tick = 0; Tick < 3; ++Tick) { Original.Instance->Tick(0.1f); }
		ASSERT_THAT(IsTrue(Original.Instance->IsHolding(), Original.Trace()));
		FADSKSnapshot Snapshot;
		Original.Instance->CaptureSnapshot(Snapshot);
		ASSERT_THAT(IsTrue(Snapshot.bHolding, TEXT("the hold is not in the snapshot")));

		FADSKTestRuntime Restored(Xml);
		Restored.Instance->Start(Snapshot);
		Restored.Instance->Tick(0.1f);
		ASSERT_THAT(IsTrue(Restored.Instance->IsHolding(), Restored.Trace()));
		const int32 Leaf = Restored.Definition->FindNode(TEXT("w"));
		ASSERT_THAT(AreEqual(0, Restored.Recorder->Snapshot().FilterByPredicate([Leaf](const FADSKRecord& Record)
			{ return Record.Kind == EADSKRecordKind::NodeStarted && Record.Node == Leaf; }).Num(), Restored.Trace()));
	}

	TEST_METHOD(ABaselineIsDroppedWhenItsEventWatchesAnotherSlot)
	{
		FADSKTestRuntime Original(Agent());
		Original.Instance->Start();
		Original.Instance->EnqueueEvent(TEXT("Touch"), {ADSKParam(TEXT("W"), FADSKValue::MakeInt(3))});
		Original.Instance->Tick(0.f);
		Original.Instance->Tick(0.f);
		FADSKSnapshot Snapshot;
		Original.Instance->CaptureSnapshot(Snapshot);
		Snapshot.StatePath = {TEXT("m"), TEXT("idle")};

		// Changed now watches Rolled (an Int at 0): the old baseline of Watched (3) must not read as a change of it.
		FADSKTestRuntime Edited(Agent().Replace(TEXT("change=\"Watched\""), TEXT("change=\"Rolled\"")));
		Edited.Instance->Start(Snapshot);
		Edited.Instance->Tick(0.f);
		Edited.Instance->Tick(0.f);
		ADSK_ASSERT(Edited.PathIs({TEXT("m"), TEXT("idle")}));
	}

	TEST_METHOD(ARestoreDropsWhatChangedKindAndKeepsObjects)
	{
		// Who is an Object, Feeling a Mood, Count an Int; the cooldown gate keeps a ready time as node state.
		const FString Xml = TEXT("<Agent version=\"1\" id=\"kinds\"><Enums><Enum name=\"Mood\"><Value name=\"Calm\"/><Value name=\"Angry\"/></Enum></Enums>"
			"<Locals><Local name=\"Who\" type=\"Object\"/><Local name=\"Feeling\" type=\"Mood\"/><Local name=\"Count\" type=\"Int\"/></Locals>"
			"<Events><Event name=\"Set\"><Param name=\"W\" type=\"Object\"/><Param name=\"F\" type=\"Mood\"/><Param name=\"C\" type=\"Int\"/></Event></Events>"
			"<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"><On event=\"Set\"><Bind param=\"W\" to=\"Who\"/>"
			"<Bind param=\"F\" to=\"Feeling\"/><Bind param=\"C\" to=\"Count\"/></On></State></StateMachine>"
			"<Tree id=\"t\"><Cooldown id=\"gate\" duration=\"100\"><Wait id=\"w\" duration=\"100\"/></Cooldown></Tree></Agent>");
		FADSKTestRuntime Original(Xml);
		Original.Instance->Start();
		Original.Instance->EnqueueEvent(TEXT("Set"), {ADSKParam(TEXT("W"), FADSKValue::MakeObject(42)), ADSKParam(TEXT("F"), FADSKValue::MakeEnum(0, 1)),
			ADSKParam(TEXT("C"), FADSKValue::MakeInt(3))});
		Original.Instance->Tick(0.f);
		FADSKSnapshot Snapshot;
		ASSERT_THAT(IsTrue(Original.Instance->CaptureSnapshot(Snapshot), TEXT("capture refused")));

		// The enum was renamed (same values), Count became an Input, and gate is a TimeLimit now, which keeps no node state.
		const FString Edited = TEXT("<Agent version=\"1\" id=\"kinds\"><Enums><Enum name=\"Temper\"><Value name=\"Calm\"/><Value name=\"Angry\"/></Enum></Enums>"
			"<Inputs><Input name=\"Count\" type=\"Int\" default=\"0\"/></Inputs>"
			"<Locals><Local name=\"Who\" type=\"Object\"/><Local name=\"Feeling\" type=\"Temper\"/></Locals>"
			"<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>"
			"<Tree id=\"t\"><TimeLimit id=\"gate\" duration=\"100\"><Wait id=\"w\" duration=\"100\"/></TimeLimit></Tree></Agent>");
		FADSKTestRuntime Restored(Edited);
		const FADSKStartReport Report = Restored.Instance->Start(Snapshot);
		ASSERT_THAT(IsTrue(Report.IsSuccess(), LexToString(Report.Result)));
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("local Feeling"), TEXT("local Count"), TEXT("node-state gate")}), Report.Dropped));
		FADSKValue Who, Feeling, Count;
		Restored.Instance->GetValue(TEXT("Who"), Who);
		Restored.Instance->GetValue(TEXT("Feeling"), Feeling);
		Restored.Instance->GetValue(TEXT("Count"), Count);
		// An Object id is kept as it is; the dropped ones start at their zero values, the Input at its default.
		ASSERT_THAT(IsTrue(Who == FADSKValue::MakeObject(42), LexToString(Who)));
		ASSERT_THAT(IsTrue(Feeling == FADSKValue::MakeEnum(0, 0), LexToString(Feeling)));
		ASSERT_THAT(IsTrue(Count == FADSKValue::MakeInt(0), LexToString(Count)));
	}

	TEST_METHOD(ARenamedAgentIsRestoredAndReported)
	{
		FADSKTestRuntime Original(Agent());
		Original.Instance->Start();
		Original.Instance->EnqueueEvent(TEXT("Go"), {ADSKParam(TEXT("V"), FADSKValue::MakeFloat(7.f))});
		Original.Instance->Tick(0.f);
		FADSKSnapshot Snapshot;
		Original.Instance->CaptureSnapshot(Snapshot);

		FADSKTestRuntime Renamed(Agent().Replace(TEXT("id=\"snap\""), TEXT("id=\"renamed\"")));
		const FADSKStartReport Report = Renamed.Instance->Start(Snapshot);
		ASSERT_THAT(IsTrue(Report.IsSuccess(), LexToString(Report.Result)));
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("agent snap")}), Report.Dropped));
		ADSK_ASSERT(Renamed.PathIs({TEXT("m"), TEXT("work"), TEXT("inner")}));
		FADSKValue Kept;
		Renamed.Instance->GetValue(TEXT("Kept"), Kept);
		ASSERT_THAT(IsTrue(Kept == FADSKValue::MakeFloat(7.f), LexToString(Kept)));
	}

	TEST_METHOD(CaptureNeedsARunningInstanceAndAMissingPathStartsInitial)
	{
		FADSKTestRuntime Runtime(Agent());
		FADSKSnapshot Snapshot;
		Snapshot.StatePath = {TEXT("gone")};
		ASSERT_THAT(IsFalse(Runtime.Instance->CaptureSnapshot(Snapshot), TEXT("a stopped instance captured")));
		ASSERT_THAT(IsTrue(Snapshot.StatePath.IsEmpty(), TEXT("a refused capture left data")));
		Snapshot.StatePath = {TEXT("lost"), TEXT("gone")};
		const FADSKStartReport Report = Runtime.Instance->Start(Snapshot);
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("state gone")}), Report.Dropped));
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("idle")}));
	}
};

#endif
