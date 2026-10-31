// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Trace/ADSKTrace.h"
#include "Misc/ScopeExit.h"
#include "Trace/Trace.h"
#include "CQTest.h"

namespace FTraceSpecPrivate
{
	/** Records Count entries whose Tick equals their 1-based ordinal. */
	void Fill(FADSKRecorder& Recorder, int32 Count)
	{
		for (int32 Ordinal = 1; Ordinal <= Count; ++Ordinal)
		{
			FADSKRecord Record;
			Record.Tick = 100 + Ordinal;
			Record.Kind = EADSKRecordKind::NodeStarted;
			Record.Node = Ordinal;
			Recorder.Record(Record);
		}
	}

	FADSKCheck IsGap(const FADSKRecord& Record, uint64 Lost, uint64 FirstSequence)
	{
		if (Record.Kind == EADSKRecordKind::Gap && Record.LostCount == Lost && Record.Sequence == FirstSequence
			&& Record.Tick == 100 + FirstSequence)
		{
			return FADSKCheck::Pass();
		}
		return FADSKCheck::Fail(FString::Printf(TEXT("record is %s #%llu t=%llu lost=%llu, expected a gap #%llu lost=%llu"),
			LexToString(Record.Kind), Record.Sequence, Record.Tick, Record.LostCount, FirstSequence, Lost));
	}
}

using namespace FTraceSpecPrivate;

TEST_CLASS_WITH_FLAGS(FTraceRingSpec, "AdamskAI.Trace.Ring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(CapacityBelowTwoIsRejected)
	{
		ASSERT_THAT(IsNull(FADSKRecorder::Create(0).Get()));
		ASSERT_THAT(IsNull(FADSKRecorder::Create(1).Get()));
		ASSERT_THAT(IsNull(FADSKRecorder::Create(-4).Get()));
		ASSERT_THAT(IsNotNull(FADSKRecorder::Create(2).Get()));
	}

	TEST_METHOD(BelowCapacityKeepsEveryRecordInOrder)
	{
		const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(8);
		Fill(*Recorder, 8);
		const TArray<FADSKRecord> Snapshot = Recorder->Snapshot();
		ASSERT_THAT(AreEqual(8, Snapshot.Num()));
		for (int32 Index = 0; Index < 8; ++Index)
		{
			ASSERT_THAT(AreEqual(static_cast<uint64>(Index + 1), Snapshot[Index].Sequence));
			ASSERT_THAT(AreEqual(Index + 1, Snapshot[Index].Node));
		}
		ASSERT_THAT(AreEqual(static_cast<uint64>(0), Recorder->GetTotalLost()));
		ASSERT_THAT(AreEqual(static_cast<uint64>(8), Recorder->GetTotalRecorded()));
	}

	TEST_METHOD(CapacityTwoFirstOverflowLeavesAGapAndTheNewestRecord)
	{
		const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(2);
		Fill(*Recorder, 3);
		const TArray<FADSKRecord> Snapshot = Recorder->Snapshot();
		ASSERT_THAT(AreEqual(2, Snapshot.Num()));
		ADSK_ASSERT(IsGap(Snapshot[0], 2, 1));
		ASSERT_THAT(AreEqual(static_cast<uint64>(3), Snapshot[1].Sequence));
		ASSERT_THAT(AreEqual(EADSKRecordKind::NodeStarted, Snapshot[1].Kind, FString::Printf(TEXT("kind is %s"), LexToString(Snapshot[1].Kind))));
	}

	TEST_METHOD(CapacityFourAfterTenIsGapSevenThenRecordsEightToTen)
	{
		const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(4);
		Fill(*Recorder, 10);
		const TArray<FADSKRecord> Snapshot = Recorder->Snapshot();
		ASSERT_THAT(AreEqual(4, Snapshot.Num()));
		ADSK_ASSERT(IsGap(Snapshot[0], 7, 1));
		ASSERT_THAT(AreEqual(static_cast<uint64>(8), Snapshot[1].Sequence));
		ASSERT_THAT(AreEqual(static_cast<uint64>(9), Snapshot[2].Sequence));
		ASSERT_THAT(AreEqual(static_cast<uint64>(10), Snapshot[3].Sequence));
		ASSERT_THAT(AreEqual(static_cast<uint64>(7), Recorder->GetTotalLost()));
		ASSERT_THAT(AreEqual(static_cast<uint64>(10), Recorder->GetTotalRecorded()));
	}

	TEST_METHOD(TailIsTheNewestRecordsOfTheSnapshot)
	{
		const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(4);
		Fill(*Recorder, 10);
		const TArray<FADSKRecord> Two = Recorder->Tail(2);
		ASSERT_THAT(AreEqual(2, Two.Num()));
		ASSERT_THAT(AreEqual(static_cast<uint64>(9), Two[0].Sequence));
		ASSERT_THAT(AreEqual(static_cast<uint64>(10), Two[1].Sequence));
		ASSERT_THAT(AreEqual(4, Recorder->Tail(100).Num()));
		ADSK_ASSERT(IsGap(Recorder->Tail(100)[0], 7, 1));
		ASSERT_THAT(AreEqual(0, Recorder->Tail(0).Num()));
		ASSERT_THAT(AreEqual(0, Recorder->Tail(-3).Num()));
	}
};

TEST_CLASS_WITH_FLAGS(FTraceBoundSpec, "AdamskAI.Trace.Bounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ThousandWritesStayBoundedWithOneOldestGap)
	{
		const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(16);
		Fill(*Recorder, 1000);
		const TArray<FADSKRecord> Snapshot = Recorder->Snapshot();
		ASSERT_THAT(AreEqual(16, Snapshot.Num()));
		ADSK_ASSERT(IsGap(Snapshot[0], 1000 - 16 + 1, 1));
		int32 Gaps = 0;
		for (int32 Index = 0; Index < Snapshot.Num(); ++Index)
		{
			Gaps += Snapshot[Index].Kind == EADSKRecordKind::Gap ? 1 : 0;
			ASSERT_THAT(IsTrue(Index == 0 || Snapshot[Index].Sequence == Snapshot[Index - 1].Sequence + 1 || Index == 1,
				FString::Printf(TEXT("record %d has sequence %llu out of order"), Index, Snapshot[Index].Sequence)));
		}
		ASSERT_THAT(AreEqual(1, Gaps));
		ASSERT_THAT(AreEqual(static_cast<uint64>(986), Snapshot[1].Sequence));
		ASSERT_THAT(AreEqual(static_cast<uint64>(1000), Snapshot[15].Sequence));
		ASSERT_THAT(AreEqual(static_cast<uint64>(1000), Recorder->GetTotalRecorded()));
		ASSERT_THAT(AreEqual(static_cast<uint64>(985), Recorder->GetTotalLost()));
	}

	TEST_METHOD(ResetClearsCountersAndReusesCapacity)
	{
		const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(4);
		Fill(*Recorder, 10);
		Recorder->Reset();
		ASSERT_THAT(AreEqual(0, Recorder->Num()));
		ASSERT_THAT(AreEqual(static_cast<uint64>(0), Recorder->GetTotalRecorded()));
		ASSERT_THAT(AreEqual(static_cast<uint64>(0), Recorder->GetTotalLost()));
		ASSERT_THAT(AreEqual(4, Recorder->GetCapacity()));
		Fill(*Recorder, 3);
		const TArray<FADSKRecord> Snapshot = Recorder->Snapshot();
		ASSERT_THAT(AreEqual(3, Snapshot.Num()));
		ASSERT_THAT(AreEqual(static_cast<uint64>(1), Snapshot[0].Sequence));
		ASSERT_THAT(AreEqual(static_cast<uint64>(3), Snapshot[2].Sequence));
		ASSERT_THAT(AreEqual(EADSKRecordKind::NodeStarted, Snapshot[0].Kind, FString::Printf(TEXT("kind is %s"), LexToString(Snapshot[0].Kind))));
	}

	TEST_METHOD(CallerSuppliedSequenceIsIgnoredAndNamesAreStable)
	{
		const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(4);
		FADSKRecord Record;
		Record.Sequence = 999;
		Record.Reason = EADSKRecordReason::QueueFull;
		Record.bHasValue = true;
		Record.Value = FADSKValue::MakeFloat(2.f);
		Recorder->Record(Record);
		const TArray<FADSKRecord> Snapshot = Recorder->Snapshot();
		ASSERT_THAT(AreEqual(static_cast<uint64>(1), Snapshot[0].Sequence));
		ASSERT_THAT(IsTrue(Snapshot[0].Value == FADSKValue::MakeFloat(2.f), LexToString(Snapshot[0].Value)));
		ASSERT_THAT(AreEqual(FString(TEXT("queue-full")), FString(LexToString(EADSKRecordReason::QueueFull))));
		ASSERT_THAT(AreEqual(FString(TEXT("budget-deferred")), FString(LexToString(EADSKRecordReason::BudgetDeferred))));
		ASSERT_THAT(AreEqual(FString(TEXT("gap")), FString(LexToString(EADSKRecordKind::Gap))));
	}
};

TEST_CLASS_WITH_FLAGS(FTraceVocabularySpec, "AdamskAI.Trace.Vocabulary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(EveryRecordKindHasAStableNonUnknownName)
	{
		const EADSKRecordKind Kinds[] = {EADSKRecordKind::InstanceStarted, EADSKRecordKind::StartFailed,
			EADSKRecordKind::InstanceStopped, EADSKRecordKind::InputSet, EADSKRecordKind::EventEnqueued,
			EADSKRecordKind::InputRejected, EADSKRecordKind::EventConsumed, EADSKRecordKind::EventUnhandled,
			EADSKRecordKind::StateEntered, EADSKRecordKind::StateExited, EADSKRecordKind::GuardValue,
			EADSKRecordKind::NodeStarted, EADSKRecordKind::NodeEnded, EADSKRecordKind::ResultAccepted,
			EADSKRecordKind::ResultStale, EADSKRecordKind::ResultDuplicate, EADSKRecordKind::CompletionStale,
			EADSKRecordKind::Hold, EADSKRecordKind::BudgetDeferred, EADSKRecordKind::Gap,
			EADSKRecordKind::TransitionTaken, EADSKRecordKind::SlotWritten, EADSKRecordKind::StateSelected};
		for (const EADSKRecordKind Kind : Kinds)
		{
			ASSERT_THAT(IsFalse(FString(LexToString(Kind)) == TEXT("unknown"), FString::Printf(TEXT("record kind %d has no name"), static_cast<int32>(Kind))));
		}
		ASSERT_THAT(AreEqual(23, static_cast<int32>(UE_ARRAY_COUNT(Kinds))));
	}
};

TEST_CLASS_WITH_FLAGS(FTraceWritesSpec, "AdamskAI.Trace.Writes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	/** Ping binds P to L and, while Armed, leads to a tree whose Sense writes (1,2,3) to Seen. */
	FString Agent() const
	{
		return TEXT("<Agent version=\"1\" id=\"writes\">"
			"<Inputs><Input name=\"Armed\" type=\"Bool\" default=\"true\"/></Inputs>"
			"<Locals><Local name=\"L\" type=\"Vector\"/><Local name=\"Seen\" type=\"Vector\"/></Locals>"
			"<Events><Event name=\"Ping\"><Param name=\"P\" type=\"Vector\"/></Event></Events>"
			"<StateMachine id=\"m\" initial=\"idle\">"
			"<State id=\"idle\" tree=\"t\"><On event=\"Ping\" if=\"Armed\" goto=\"sensing\"><Bind param=\"P\" to=\"L\"/></On></State>"
			"<State id=\"sensing\" tree=\"s\"><On event=\"Ping\" goto=\"sensing\"><Bind param=\"P\" to=\"L\"/></On></State>"
			"</StateMachine>"
			"<Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree>"
			"<Tree id=\"s\"><Sequence id=\"q\"><Sense id=\"sense\" out-point=\"Seen\"/><Wait id=\"hold\" duration=\"100\"/></Sequence></Tree>"
			"</Agent>");
	}

	TArray<FString> Lines(const FADSKTestRuntime& Runtime, EADSKRecordKind Kind) const
	{
		TArray<FString> Result;
		for (const FADSKRecord& Record : Runtime.Recorder->Snapshot())
		{
			if (Record.Kind == Kind)
			{
				// Drop "#<sequence> t=<tick> ", which other records shift.
				FString Line = ADSK::FormatRecord(*Runtime.Definition, Record);
				int32 Space = INDEX_NONE;
				Line.FindChar(TEXT(' '), Space);
				Result.Add(Line.Mid(Space + 1));
			}
		}
		return Result;
	}

	TEST_METHOD(ATransitionRecordsItsIndexTargetAndConditionValue)
	{
		FADSKTestRuntime Runtime(Agent());
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Instance->EnqueueEvent(TEXT("Ping"), {{TEXT("P"), FADSKValue::MakeVector(FVector(4.0, 5.0, 6.0))}});
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("sensing")}));
		const TArray<FString> Taken = Lines(Runtime, EADSKRecordKind::TransitionTaken);
		ASSERT_THAT(AreEqual(1, Taken.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("t=1 transition-taken state=idle event=Ping transition=0 target=sensing slot=Armed value=true")), Taken[0]));
	}

	TEST_METHOD(BindsAndPortWritesAreRecordedOnlyWhenTheyChangeTheSlot)
	{
		FADSKTestRuntime Runtime(Agent());
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		const FADSKNamedValue Ping[] = {{TEXT("P"), FADSKValue::MakeVector(FVector(4.0, 5.0, 6.0))}};
		Runtime.Instance->EnqueueEvent(TEXT("Ping"), Ping);
		Runtime.Instance->Tick(0.f);
		// The same value again: the self-transition restarts Sense, which writes Seen unchanged.
		Runtime.Instance->EnqueueEvent(TEXT("Ping"), Ping);
		Runtime.Instance->Tick(0.f);
		const TArray<FString> Written = Lines(Runtime, EADSKRecordKind::SlotWritten);
		ASSERT_THAT(AreEqual(2, Written.Num(), *FString::Join(Written, TEXT("\n"))));
		ASSERT_THAT(IsTrue(Written[0].Contains(TEXT("slot-written event=Ping transition=0 target=sensing slot=L value=")), *Written[0]));
		ASSERT_THAT(IsTrue(Written[1].Contains(TEXT("slot-written node=sense handle=")) && Written[1].Contains(TEXT("slot=Seen value=")), *Written[1]));
		ASSERT_THAT(AreEqual(2, Lines(Runtime, EADSKRecordKind::TransitionTaken).Num()));
	}

	/** The Insights channel on: regions open and close with the states and the run is unchanged (Insights itself is checked by hand). */
	TEST_METHOD(TheInsightsChannelChangesNothing)
	{
		ASSERT_THAT(IsTrue(UE::Trace::IsChannel(TEXT("AdamskAI")), TEXT("no AdamskAI trace channel")));
		UE::Trace::ToggleChannel(TEXT("AdamskAI"), true);
		ON_SCOPE_EXIT { UE::Trace::ToggleChannel(TEXT("AdamskAI"), false); };
		FADSKTestRuntime Runtime(Agent());
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Instance->EnqueueEvent(TEXT("Ping"), {{TEXT("P"), FADSKValue::MakeVector(FVector(4.0, 5.0, 6.0))}});
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("sensing")}));
		Runtime.Instance->Stop();
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("idle")}));
	}

	TEST_METHOD(WithoutARecorderPortWritesStillLand)
	{
		FADSKTestRuntime Runtime(Agent(), 0);
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Instance->EnqueueEvent(TEXT("Ping"), {{TEXT("P"), FADSKValue::MakeVector(FVector(4.0, 5.0, 6.0))}});
		Runtime.Instance->Tick(0.f);
		FADSKValue Seen;
		ASSERT_THAT(IsTrue(Runtime.Instance->GetValue(TEXT("Seen"), Seen), TEXT("no slot Seen")));
		ASSERT_THAT(IsTrue(Seen == FADSKValue::MakeVector(FVector(1.0, 2.0, 3.0)), LexToString(Seen)));
	}
};

#endif
