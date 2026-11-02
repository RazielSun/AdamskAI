// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Debug/ADSKDebugSnapshot.h"
#include "Compiler/ADSKCompiler.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Trace/ADSKTrace.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "AIController.h"
#include "CQTest.h"
#include "Support/TestChecks.h"
#include "Components/ActorTestSpawner.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
// Before 5.7 FStringOutputDevice lives in Containers/UnrealString.h.
#if __has_include("Misc/StringOutputDevice.h")
#include "Misc/StringOutputDevice.h"
#endif
#include "HAL/FileManager.h"

namespace FDebugSnapshotSpecPrivate
{
	const TCHAR* SyntheticSource = TEXT("debug/snapshot.xml");

	FString SnapshotXml()
	{
		return TEXT("<Agent version=\"1\" id=\"snap\">")
			TEXT("<Inputs><Input name=\"Speed\" type=\"Float\" default=\"1\"/><Input name=\"Goal\" type=\"Vector\" default=\"0,0,0\"/></Inputs>")
			TEXT("<Locals><Local name=\"Flag\" type=\"Bool\"/></Locals>")
			TEXT("<Events><Event name=\"low\" priority=\"0\"/>")
			TEXT("<Event name=\"high\" priority=\"5\"><Param name=\"Amount\" type=\"Float\"/><Param name=\"Dir\" type=\"Vector\"/></Event></Events>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"><On event=\"low\"/><On event=\"high\"/></State></StateMachine>")
			TEXT("<Tree id=\"t\"><Wait id=\"snapWait\" duration=\"100\"/></Tree></Agent>");
	}

	/** Verbatim former `adsk.Status` brain line, built independently of the production formatter. */
	FString ExpectedLegacyLine(const FADSKBrainDebugRow& Row)
	{
		return FString::Printf(TEXT("adsk: brain=%s pawn=%s source=%s running=%s state=%s ticks=%llu hash=%s error=%s diagnostics=%s"),
			*Row.OwnerName, *Row.PawnName, *Row.SourcePath, Row.bHostRunning ? TEXT("true") : TEXT("false"),
			*FString::Join(Row.ActiveStatePath, TEXT("/")), Row.TickCount, *Row.CompositeHash, *Row.LastError, *Row.Diagnostics);
	}

	struct FFixture
	{
		FActorTestSpawner Spawner;
		AAIController* Controller = &Spawner.SpawnActor<AAIController>();
		APawn* Pawn = &Spawner.SpawnActor<APawn>();
		TSharedPtr<const FADSKDefinition> Definition;
		TArray<UADSKBrainComponent*> Brains;
		UADSKWorldSubsystem* Subsystem = nullptr;

		FFixture()
		{
			Controller->Possess(Pawn);
			Definition = ADSKTest::LoadOrStandIn(TEXT("snapshot.xml"), SnapshotXml(), ADSKTest::PluginCatalogue());
			Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
			ADSKTest::Expect(Subsystem != nullptr, TEXT("the test world has no AdamskAI subsystem"));
		}

		~FFixture()
		{
			for (UADSKBrainComponent* Brain : Brains)
			{
				if (Subsystem)
				{
					Subsystem->UnregisterBrain(SyntheticSource, Brain);
				}
				Brain->Cleanup();
			}
		}

		UADSKBrainComponent* AddBrain(bool bInitialize, int32 RecorderCapacity = 128)
		{
			UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(Controller);
			Controller->AddInstanceComponent(Brain);
			Brain->RegisterComponent();
			if (bInitialize)
			{
				Brain->InitializeDefinition(Definition.ToSharedRef(), FADSKRecorder::Create(RecorderCapacity));
			}
			if (Subsystem)
			{
				Subsystem->RegisterBrain(SyntheticSource, Brain);
			}
			Brains.Add(Brain);
			return Brain;
		}

		FADSKDebugSnapshot Collect() { return FADSKDebugSnapshot::Collect(&Spawner.GetWorld()); }
	};

	void EnqueueMixed(UADSKBrainComponent& Brain)
	{
		Brain.GetInstance()->EnqueueEvent(TEXT("low"));
		Brain.GetInstance()->EnqueueEvent(TEXT("high"),
			{{TEXT("Amount"), FADSKValue::MakeFloat(1.f)}, {TEXT("Dir"), FADSKValue::MakeVector(FVector::ZeroVector)}});
		Brain.GetInstance()->EnqueueEvent(TEXT("low"));
	}
}

using namespace FDebugSnapshotSpecPrivate;

TEST_CLASS_WITH_FLAGS(FDebugSnapshotCollectSpec, "AdamskAI.Host.DebugSnapshot.Collect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(EmptyWorldYieldsNoRows)
	{
		ASSERT_THAT(IsTrue(FADSKDebugSnapshot::Collect(nullptr).Rows.IsEmpty(), TEXT("rows without a world")));
		UWorld* Inactive = UWorld::CreateWorld(EWorldType::Inactive, false);
		const bool bEmpty = FADSKDebugSnapshot::Collect(Inactive).Rows.IsEmpty();
		Inactive->DestroyWorld(false);
		ASSERT_THAT(IsTrue(bEmpty, TEXT("rows for an inactive world")));
	}

	TEST_METHOD(RowCarriesExecutionFields)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.AddBrain(true);
		Brain->SetInput(TEXT("Speed"), FADSKValue::MakeFloat(2.f));
		Brain->SetInput(TEXT("Goal"), FADSKValue::MakeVector(FVector(1, 2, 3)));
		Brain->StartLogic();
		Brain->TickBrain(0.f);
		const FADSKDebugSnapshot Snapshot = F.Collect();
		ASSERT_THAT(AreEqual(1, Snapshot.Rows.Num()));
		const FADSKBrainDebugRow& Row = Snapshot.Rows[0];
		ASSERT_THAT(AreEqual(EADSKDebugExecState::Running, Row.ExecState));
		ASSERT_THAT(IsTrue(Row.bHostRunning, TEXT("the host is not running")));
		ASSERT_THAT(AreEqual(Brain->GetInstance()->GetGeneration(), Row.Generation));
		ASSERT_THAT(AreEqual(Brain->GetInstance()->GetTickCount(), Row.TickCount));
		ASSERT_THAT(AreEqual(TArray<FString>{TEXT("m"), TEXT("s")}, Row.ActiveStatePath));
		ASSERT_THAT(AreEqual(FName(TEXT("snapWait")), Row.ActiveNodeId));
		ASSERT_THAT(AreEqual(2, Row.Inputs.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("Speed")), Row.Inputs[0].Name));
		ASSERT_THAT(AreEqual(FString(TEXT("Goal")), Row.Inputs[1].Name));
		ASSERT_THAT(AreEqual(FString(TEXT("Float")), Row.Inputs[0].Type));
		ASSERT_THAT(AreEqual(1, Row.Locals.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("Flag")), Row.Locals[0].Name));
		ASSERT_THAT(AreEqual(2, Row.CachedInputs.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("Goal")), Row.CachedInputs[0].Name));
		ASSERT_THAT(AreEqual(FString(TEXT("Speed")), Row.CachedInputs[1].Name));
		ASSERT_THAT(AreEqual(0, Row.PendingOps.Num()));
		ASSERT_THAT(AreEqual(0u, Row.StaleAsyncCompletions));
		ASSERT_THAT(AreEqual(FString(TEXT("none")), FADSKDebugSnapshot::FormatOps(Row)));
		ASSERT_THAT(IsTrue(Row.bHasLabelLocation, TEXT("the row has no label location")));
		ASSERT_THAT(AreEqual(F.Pawn->GetName(), Row.PawnName));
		ASSERT_THAT(AreEqual(F.Controller->GetName(), Row.OwnerName));
		ASSERT_THAT(IsTrue(Row.LabelLocation.Z >= F.Pawn->GetActorLocation().Z + 20.0, FString::Printf(TEXT("the label is at z=%.1f, the pawn at z=%.1f"), Row.LabelLocation.Z, F.Pawn->GetActorLocation().Z)));
	}

	TEST_METHOD(QueuedEventsInDispatchOrder)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.AddBrain(true);
		Brain->SetInput(TEXT("Speed"), FADSKValue::MakeFloat(2.f));
		Brain->StartLogic();
		Brain->TickBrain(0.f);
		EnqueueMixed(*Brain);
		const TArray<FString> Expected = {TEXT("high"), TEXT("low"), TEXT("low")};
		ASSERT_THAT(AreEqual(Expected, F.Collect().Rows[0].QueuedEvents));
		ASSERT_THAT(AreEqual(Expected, F.Collect().Rows[0].QueuedEvents));
	}

	TEST_METHOD(EventSchemasExcludeReserved)
	{
		FFixture F;
		F.AddBrain(true);
		const FADSKDebugSnapshot Snapshot = F.Collect();
		const TArray<FADSKDebugEventSchema>& Events = Snapshot.Rows[0].Events;
		ASSERT_THAT(AreEqual(2, Events.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("low")), Events[0].Name));
		ASSERT_THAT(IsTrue(Events[0].Params.IsEmpty(), TEXT("low has parameters")));
		ASSERT_THAT(AreEqual(FString(TEXT("high")), Events[1].Name));
		ASSERT_THAT(AreEqual(5, Events[1].Priority));
		ASSERT_THAT(AreEqual(2, Events[1].Params.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("Amount")), Events[1].Params[0].Name));
		ASSERT_THAT(AreEqual(FString(TEXT("Dir")), Events[1].Params[1].Name));
		ASSERT_THAT(IsTrue(Events[1].Params[0].Value.IsEmpty(), Events[1].Params[0].Value));
	}

	TEST_METHOD(RecordTailIsBounded)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.AddBrain(true, 256);
		Brain->SetInput(TEXT("Speed"), FADSKValue::MakeFloat(2.f));
		Brain->StartLogic();
		for (int32 Index = 0; Index < 12; ++Index)
		{
			Brain->GetInstance()->EnqueueEvent(TEXT("low"));
			Brain->TickBrain(0.f);
		}
		const TArray<FADSKRecord> Records = Brain->GetRecorder()->Snapshot();
		ASSERT_THAT(IsTrue(Records.Num() > FADSKDebugSnapshot::RecordTail, FString::Printf(TEXT("only %d records"), Records.Num())));
		const FADSKDebugSnapshot Snapshot = F.Collect();
		const TArray<FString>& Tail = Snapshot.Rows[0].RecentRecords;
		ASSERT_THAT(AreEqual(FADSKDebugSnapshot::RecordTail, Tail.Num()));
		const int32 First = Records.Num() - FADSKDebugSnapshot::RecordTail;
		for (int32 Index = 0; Index < Tail.Num(); ++Index)
		{
			ASSERT_THAT(IsTrue(Tail[Index].StartsWith(FString::Printf(TEXT("#%llu "), Records[First + Index].Sequence)), Tail[Index]));
		}
	}

	TEST_METHOD(LabelDetailReadsTheStateButNotSlotsOrRecords)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.AddBrain(true);
		Brain->StartLogic();
		const FADSKDebugSnapshot Labels = FADSKDebugSnapshot::Collect(&F.Spawner.GetWorld(), EADSKDebugDetail::Labels);
		ASSERT_THAT(AreEqual(1, Labels.Rows.Num()));
		ASSERT_THAT(IsFalse(Labels.Rows[0].ActiveStatePath.IsEmpty(), TEXT("a label row has no state path")));
		ASSERT_THAT(IsTrue(Labels.Rows[0].Inputs.IsEmpty() && Labels.Rows[0].Locals.IsEmpty() && Labels.Rows[0].RecentRecords.IsEmpty(),
			TEXT("a label row read slots or records")));
		ASSERT_THAT(IsFalse(F.Collect().Rows[0].RecentRecords.IsEmpty(), TEXT("a full row has no records")));
	}

	TEST_METHOD(ABrainGivenADefinitionDirectlyIsListedToo)
	{
		FFixture F;
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(F.Controller);
		F.Controller->AddInstanceComponent(Brain);
		Brain->RegisterComponent();
		Brain->InitializeDefinition(F.Definition.ToSharedRef(), FADSKRecorder::Create(16));
		ON_SCOPE_EXIT { Brain->Cleanup(); };
		ASSERT_THAT(IsFalse(F.Subsystem->GetAllBrains().Contains(Brain), TEXT("an unregistered brain is listed")));
		const FADSKDebugSnapshot Snapshot = F.Collect();
		ASSERT_THAT(AreEqual(1, Snapshot.Rows.Num()));
	}

	TEST_METHOD(PendingOpsListEveryEntryUntilTheNodeEnds)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.AddBrain(true);
		Brain->SetInput(TEXT("Speed"), FADSKValue::MakeFloat(2.f));
		Brain->StartLogic();
		const FADSKActivationHandle Handle = Brain->GetInstance()->GetActiveLeafHandle();
		ASSERT_THAT(IsTrue(Brain->GetAsyncOps().Begin(Handle, TEXT("Probe"), TEXT("snapWait"), 1.0e6), TEXT("the probe op did not begin")));
		ASSERT_THAT(IsTrue(Brain->BindMove(Handle, FAIRequestID(9)), TEXT("the move was not bound")));
		FADSKDebugSnapshot Snapshot = F.Collect();
		const FADSKBrainDebugRow* Row = &Snapshot.Rows[0];
		ASSERT_THAT(AreEqual(1, Row->PendingOps.Num()));
		ASSERT_THAT(AreEqual(FName(TEXT("Probe")), Row->PendingOps[0].Kind));
		ASSERT_THAT(AreEqual(FName(TEXT("snapWait")), Row->PendingOps[0].NodeId));
		ASSERT_THAT(AreEqual(FString::Printf(TEXT("%u.%u"), Handle.Generation, Handle.ActivationId), Row->PendingOps[0].Handle));
		ASSERT_THAT(AreEqual(FAIRequestID(9).ToString(), Row->PendingOps[0].MoveRequest));
		// A start time after the world clock clamps to age zero.
		ASSERT_THAT(IsTrue(Row->PendingOps[0].AgeSeconds == 0.f, FString::Printf(TEXT("age %f"), Row->PendingOps[0].AgeSeconds)));
		ASSERT_THAT(IsTrue(Snapshot.ToLines()[1].Contains(TEXT("ops=Probe@snapWait(")) && Snapshot.ToLines()[1].Contains(TEXT("s) stale=0 queued=")), Snapshot.ToLines()[1]));

		// An accepted result waits for the next tick but the entry stays visible; its move tag is already gone.
		Brain->CompleteMoveForTest(FAIRequestID(9), EPathFollowingResult::Success);
		Snapshot = F.Collect();
		Row = &Snapshot.Rows[0];
		ASSERT_THAT(AreEqual(1, Row->PendingOps.Num()));
		ASSERT_THAT(IsTrue(Row->PendingOps[0].MoveRequest.IsEmpty(), Row->PendingOps[0].MoveRequest));

		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::Stale, Brain->CompleteAsync({Handle.Generation, Handle.ActivationId + 50}, EADSKNodeStatus::Succeeded)));
		Snapshot = F.Collect();
		ASSERT_THAT(AreEqual<uint32>(1, Snapshot.Rows[0].StaleAsyncCompletions));
		ASSERT_THAT(IsTrue(Snapshot.ToLines()[1].Contains(TEXT(" stale=1 queued=")), Snapshot.ToLines()[1]));
		Brain->GetAsyncOps().CancelAll();
	}

	TEST_METHOD(OpsTextFormatsAgeAndSeparatesEntries)
	{
		FADSKBrainDebugRow Row;
		ASSERT_THAT(AreEqual(FString(TEXT("none")), FADSKDebugSnapshot::FormatOps(Row)));
		Row.PendingOps.Add({TEXT("MoveTo"), TEXT("go"), TEXT("1.2"), 1.25f, FString()});
		Row.PendingOps.Add({TEXT("TacticalMove"), TEXT("tm"), TEXT("1.3"), 0.f, FString()});
		ASSERT_THAT(AreEqual(FString(TEXT("MoveTo@go(1.2s),TacticalMove@tm(0.0s)")), FADSKDebugSnapshot::FormatOps(Row)));
	}

	TEST_METHOD(StoppedBrainIsNeutral)
	{
		FFixture F;
		F.AddBrain(false);
		F.AddBrain(true);
		const FADSKDebugSnapshot Snapshot = F.Collect();
		ASSERT_THAT(AreEqual(2, Snapshot.Rows.Num()));
		for (const FADSKBrainDebugRow& Row : Snapshot.Rows)
		{
			ASSERT_THAT(IsTrue(Row.ExecState == EADSKDebugExecState::Stopped && !Row.bHostRunning && Row.PendingOps.IsEmpty() && Row.StaleAsyncCompletions == 0, FADSKDebugSnapshot::FormatLegacyLine(Row)));
			ASSERT_THAT(IsTrue(Row.ActiveNodeId.IsNone() && Row.QueuedEvents.IsEmpty() && Row.TickCount == 0, FADSKDebugSnapshot::FormatLegacyLine(Row)));
		}
		ASSERT_THAT(IsTrue(Snapshot.Rows[0].CompositeHash.IsEmpty() && Snapshot.Rows[0].Events.IsEmpty() && Snapshot.Rows[0].Inputs.IsEmpty(), TEXT("an uninitialized row has a hash, events or inputs")));
		ASSERT_THAT(IsFalse(Snapshot.Rows[1].Events.IsEmpty(), TEXT("the initialized row has no events")));
	}
};

TEST_CLASS_WITH_FLAGS(FDebugSnapshotTextSpec, "AdamskAI.Host.DebugSnapshot.Text",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(LegacyLineIsUnchanged)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.AddBrain(true);
		Brain->SetInput(TEXT("Speed"), FADSKValue::MakeFloat(2.f));
		Brain->StartLogic();
		Brain->TickBrain(0.f);
		F.AddBrain(false);
		const FADSKDebugSnapshot Snapshot = F.Collect();
		const TArray<FString> Lines = Snapshot.ToLines();
		ASSERT_THAT(IsTrue(Lines.Num() > 2, FString::Join(Lines, TEXT("\n"))));
		ASSERT_THAT(AreEqual(ExpectedLegacyLine(Snapshot.Rows[0]), Lines[0]));
		ASSERT_THAT(AreEqual(ExpectedLegacyLine(Snapshot.Rows[1]), FADSKDebugSnapshot::FormatLegacyLine(Snapshot.Rows[1])));
		int32 Legacy = 0;
		for (const FString& Line : Lines)
		{
			Legacy += Line.StartsWith(TEXT("adsk: brain=")) ? 1 : 0;
			ASSERT_THAT(IsTrue(Line.StartsWith(TEXT("adsk: brain=")) || Line.StartsWith(TEXT("  ")), Line));
		}
		ASSERT_THAT(AreEqual(2, Legacy));
		ASSERT_THAT(IsTrue(Lines[1].StartsWith(TEXT("  exec=running gen=")) && Lines[1].Contains(TEXT("node=snapWait")), Lines[1]));
		ASSERT_THAT(IsTrue(Snapshot.ToText().Contains(TEXT("  input Speed:Float=")), Snapshot.ToText()));
	}

	TEST_METHOD(StatusEmitsOneAggregate)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.AddBrain(true);
		Brain->SetInput(TEXT("Speed"), FADSKValue::MakeFloat(2.f));
		Brain->StartLogic();
		FStringOutputDevice Output;
		Output.SetAutoEmitLineTerminator(true);
		ASSERT_THAT(IsTrue(GEngine->Exec(&F.Spawner.GetWorld(), TEXT("adsk.Status"), Output), TEXT("adsk.Status was not handled")));
		TArray<FString> Lines;
		FString(Output).ParseIntoArrayLines(Lines);
		int32 Aggregates = 0;
		int32 Legacy = 0;
		for (const FString& Line : Lines)
		{
			Aggregates += Line == TEXT("adsk: 1 brain(s)") ? 1 : 0;
			Legacy += Line.StartsWith(TEXT("adsk: brain=")) ? 1 : 0;
		}
		ASSERT_THAT(AreEqual(1, Aggregates));
		ASSERT_THAT(AreEqual(1, Legacy));
		ASSERT_THAT(IsTrue(Lines.Last() == TEXT("adsk: 1 brain(s)") || Lines.Last().StartsWith(TEXT("  ")), Lines.Last()));
	}

	TEST_METHOD(EveryConsoleCommandReachesItsAction)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.AddBrain(true);
		Brain->StartLogic();
		const auto Run = [&F](const FString& Command)
		{
			FStringOutputDevice Output;
			Output.SetAutoEmitLineTerminator(true);
			ADSKTest::Expect(GEngine->Exec(&F.Spawner.GetWorld(), *Command, Output), TEXT("no console command handled: ") + Command);
			return FString(Output);
		};
		const FString LowOutput = Run(TEXT("adsk.Event all low"));
		ASSERT_THAT(IsTrue(LowOutput.Contains(TEXT("adsk: low queued for 1 of 1 brain(s)")), LowOutput));
		const FString HighOutput = Run(TEXT("adsk.Event all high Amount=2 Dir=1,0,0"));
		ASSERT_THAT(IsTrue(HighOutput.Contains(TEXT("adsk: high queued for 1 of 1 brain(s)")), HighOutput));
		const FString PartialOutput = Run(TEXT("adsk.Event all high Amount=2"));
		ASSERT_THAT(IsFalse(PartialOutput.Contains(TEXT("queued")), PartialOutput));
		TArray<FString> Queued;
		Brain->GetInstance()->GetQueuedEvents(Queued);
		ASSERT_THAT(AreEqual(2, Queued.Num()));
		const FString NobodyOutput = Run(TEXT("adsk.Restart nobody"));
		ASSERT_THAT(IsTrue(NobodyOutput.Contains(TEXT("adsk: unknown target 'nobody'")), NobodyOutput));
		// The brain was given its definition directly, so it restarts on it; the synthetic source has nothing to reload.
		const FString Restarted = Run(TEXT("adsk.Restart all"));
		ASSERT_THAT(IsTrue(Restarted.Contains(TEXT("adsk: restarted")), Restarted));
		const FString ReloadOutput = Run(FString(TEXT("adsk.Reload ")) + SyntheticSource);
		ASSERT_THAT(IsTrue(ReloadOutput.Contains(FString(SyntheticSource) + TEXT(": not-loaded")), ReloadOutput));
		const FString Dump = Run(FString(TEXT("adsk.DumpTrace ")) + Brain->GetName());
		FString Path;
		ASSERT_THAT(IsTrue(Dump.Split(TEXT("adsk: "), nullptr, &Path), Dump));
		Path.TrimEndInline();
		ASSERT_THAT(IsTrue(Path.EndsWith(TEXT(".jsonl")) && IFileManager::Get().FileExists(*Path), TEXT("no trace file at ") + Path));
		IFileManager::Get().Delete(*Path);
	}

	TEST_METHOD(GameplayDebuggerLinesFindTheControllersBrain)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.AddBrain(true);
		Brain->StartLogic();
		// The debug actor is usually the pawn; the brain lives on its controller.
		ASSERT_THAT(IsTrue(FADSKDebugSnapshot::FindBrainFor(F.Pawn) == Brain && FADSKDebugSnapshot::FindBrainFor(F.Controller) == Brain, TEXT("FindBrainFor does not find the controller's brain")));
		const TArray<FString> Lines = FADSKDebugSnapshot::GameplayDebuggerLines(F.Pawn);
		const TArray<FString> Row = FADSKDebugSnapshot::FormatRow(FADSKDebugSnapshot::Describe(*Brain));
		ASSERT_THAT(AreEqual(Row.Num(), Lines.Num()));
		ASSERT_THAT(AreEqual(TEXT("{green}") + Row[0].TrimStart(), Lines[0]));
		ASSERT_THAT(IsTrue(Lines.Last().StartsWith(TEXT("{white}")) && !Lines.Last().StartsWith(TEXT("{white} ")), Lines.Last()));
		ASSERT_THAT(IsTrue(Lines.ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("snapWait")); }), FString::Join(Lines, TEXT("\n"))));
		AActor& Bare = F.Spawner.SpawnActor<AActor>();
		ASSERT_THAT(AreEqual(TArray<FString>{TEXT("{grey}no AdamskAI brain")}, FADSKDebugSnapshot::GameplayDebuggerLines(&Bare)));
		ASSERT_THAT(AreEqual(TArray<FString>{TEXT("{grey}no AdamskAI brain")}, FADSKDebugSnapshot::GameplayDebuggerLines(nullptr)));
	}
};

#endif
