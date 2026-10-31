// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Components/ActorTestSpawner.h"
#include "Support/CompilerSpecSupport.h"
#include "Support/TempTree.h"
#include "CQTest.h"

namespace FReloadCommitSpecPrivate
{
	const TCHAR* SourceFile = TEXT("reload_commit.xml");

	FString MakeXml(const TCHAR* Duration)
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"rc\"><Inputs><Input name=\"Speed\" type=\"Float\" default=\"1\"/></Inputs><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
			TEXT("<Tree id=\"t\"><Wait id=\"w\" duration=\"%s\"/></Tree></Agent>"), Duration);
	}

	struct FFixture
	{
		FADSKTempTree Tree;
		FActorTestSpawner Spawner;
		UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();

		FFixture()
		{
			IFileManager::Get().MakeDirectory(*Tree.Root, true);
			Tree.Write(SourceFile, MakeXml(TEXT("100")));
			Subsystem->SetTestRoot(Tree.Root);
			Subsystem->SetTestRegistry(FCompilerSpecSupport::MakePathProbeRegistry());
		}

		UADSKBrainComponent* StartBrain()
		{
			AActor& Actor = Spawner.SpawnActor<AActor>();
			UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Actor);
			Actor.AddInstanceComponent(Brain);
			Brain->RegisterComponent();
			ADSKTest::Expect(Brain->InitializeFromSource(SourceFile), TEXT("brain did not initialize from ") + FString(SourceFile));
			Brain->StartLogic();
			return Brain;
		}

		void QueueEdit()
		{
			Tree.Write(SourceFile, MakeXml(TEXT("50")));
			FString Error;
			const EADSKReloadResult Result = Subsystem->RequestReload(SourceFile, Error);
			ADSKTest::Expect(Result == EADSKReloadResult::Queued, FString::Printf(TEXT("reload not queued (%d): %s"), static_cast<int32>(Result), *Error));
		}
	};
}

using namespace FReloadCommitSpecPrivate;

TEST_CLASS_WITH_FLAGS(FReloadCommitSpec, "AdamskAI.Host.ReloadCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(BindingsAreValidatedBeforeAnyBrainStopsAndNeverAfter)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.StartBrain();
		// Accepts the request and the commit check, then refuses: a revalidation after the stop would strand the brain.
		int32 Calls = 0;
		FString Error;
		ASSERT_THAT(IsTrue(Brain->AddBindingValidator(Brain, [&Calls](const FADSKDefinition&, FString& Out) { Out = TEXT("late"); return ++Calls <= 3; }, Error), Error));
		F.QueueEdit();
		const int32 BeforeCommit = Calls;
		F.Subsystem->CommitPendingReloads();
		// Exactly one check per brain at commit: the prepare phase, before any stop.
		ASSERT_THAT(AreEqual(BeforeCommit + 1, Calls));
		ASSERT_THAT(IsTrue(Brain->IsRunning(), Brain->GetLastError()));
		ASSERT_THAT(IsTrue(&Brain->GetDefinition().Get() == F.Subsystem->GetActiveDefinition(SourceFile).Get(), TEXT("the brain does not run the published definition")));
		Brain->Cleanup();
	}

	TEST_METHOD(OneRefusalRejectsTheReloadForEveryBrain)
	{
		FFixture F;
		UADSKBrainComponent* First = F.StartBrain();
		UADSKBrainComponent* Second = F.StartBrain();
		const FADSKDefinition* Old = &First->GetDefinition().Get();
		bool bReject = false;
		FString Error;
		ASSERT_THAT(IsTrue(Second->AddBindingValidator(Second, [&bReject](const FADSKDefinition&, FString& Out) { Out = TEXT("refused"); return !bReject; }, Error), Error));
		F.QueueEdit();
		bReject = true;
		TestRunner->AddExpectedMessagePlain(TEXT("reload rejected by"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		F.Subsystem->CommitPendingReloads();
		ASSERT_THAT(IsTrue(First->IsRunning(), First->GetLastError()));
		ASSERT_THAT(IsTrue(Second->IsRunning(), Second->GetLastError()));
		ASSERT_THAT(IsTrue(&First->GetDefinition().Get() == Old && &Second->GetDefinition().Get() == Old, TEXT("a rejected reload replaced a definition")));
		First->Cleanup();
		Second->Cleanup();
	}

	TEST_METHOD(AnInputWrittenByIdIsReplayedAndTheIdExpiresWithItsDefinition)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.StartBrain();
		const FADSKInputId Speed = Brain->FindInput(TEXT("Speed"));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Brain->SetInput(Speed, FADSKValue::MakeFloat(5.f))));
		F.QueueEdit();
		F.Subsystem->CommitPendingReloads();
		FADSKValue Value;
		ASSERT_THAT(IsTrue(Brain->GetInstance()->GetValue(TEXT("Speed"), Value), TEXT("no slot Speed")));
		ASSERT_THAT(IsTrue(Value.Float == 5.f, LexToString(Value)));
		ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Brain->SetInput(Speed, FADSKValue::MakeFloat(6.f))));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Brain->SetInput(Brain->FindInput(TEXT("Speed")), FADSKValue::MakeFloat(6.f))));
		Brain->Cleanup();
	}

	TEST_METHOD(AValidatorMayRemoveValidatorsWhileTheyRun)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.StartBrain();
		int32 Calls = 0;
		FString Error;
		ASSERT_THAT(IsTrue(Brain->AddBindingValidator(Brain, [Brain, &Calls](const FADSKDefinition&, FString&)
		{
			if (++Calls > 1) { Brain->RemoveBindingValidators(Brain); }
			return true;
		}, Error), Error));
		ASSERT_THAT(IsTrue(Brain->RunBindingValidators(Brain->GetDefinition().Get(), Error), Error));
		ASSERT_THAT(IsTrue(Brain->RunBindingValidators(Brain->GetDefinition().Get(), Error), Error));
		ASSERT_THAT(AreEqual(2, Calls));
		Brain->Cleanup();
	}

	TEST_METHOD(ABrainEndedByAnotherBrainsStopIsNotRestarted)
	{
		FFixture F;
		UADSKBrainComponent* First = F.StartBrain();
		UADSKBrainComponent* Second = F.StartBrain();
		// The way a death handler may destroy a neighbour: First's stop ends Second, whichever stops first.
		TWeakObjectPtr<UADSKBrainComponent> WeakSecond(Second);
		First->OnStopped.AddLambda([WeakSecond] { if (UADSKBrainComponent* B = WeakSecond.Get()) { B->Cleanup(); } });
		F.QueueEdit();
		F.Subsystem->CommitPendingReloads();
		ASSERT_THAT(IsTrue(First->IsRunning(), First->GetLastError()));
		ASSERT_THAT(IsTrue(&First->GetDefinition().Get() == F.Subsystem->GetActiveDefinition(SourceFile).Get(), TEXT("the brain does not run the published definition")));
		ASSERT_THAT(IsTrue(Second->IsCleanedUp(), TEXT("the second brain was not cleaned up")));
		ASSERT_THAT(IsFalse(Second->IsRunning(), TEXT("the second brain still runs")));
		First->OnStopped.Clear();
		First->Cleanup();
		Second->Cleanup();
	}
};

TEST_CLASS_WITH_FLAGS(FAsyncReloadSpec, "AdamskAI.Host.AsyncReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(TheCompileRunsOffTheCallAndTheCandidateQueuesLater)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.StartBrain();
		const TSharedPtr<const FADSKDefinition> Before = F.Subsystem->GetActiveDefinition(SourceFile);
		F.Tree.Write(SourceFile, MakeXml(TEXT("50")));
		ASSERT_THAT(IsTrue(F.Subsystem->RequestReloadAsync(SourceFile), TEXT("the async reload was refused")));
		ASSERT_THAT(IsTrue(F.Subsystem->IsReloadCompiling(SourceFile), TEXT("the reload is not compiling")));
		F.Subsystem->FinishReloadsForTest();
		ASSERT_THAT(IsFalse(F.Subsystem->IsReloadCompiling(SourceFile), TEXT("the reload is still compiling")));
		ASSERT_THAT(IsTrue(F.Subsystem->GetLastAsyncReloadResult(SourceFile) == TOptional<EADSKReloadResult>(EADSKReloadResult::Queued),
			FString::Printf(TEXT("the last async reload is %s"), F.Subsystem->GetLastAsyncReloadResult(SourceFile).IsSet() ? LexToString(*F.Subsystem->GetLastAsyncReloadResult(SourceFile)) : TEXT("unset"))));
		// Prepared, not published: the brain still runs the old definition until the commit.
		ASSERT_THAT(IsTrue(F.Subsystem->GetActiveDefinition(SourceFile) == Before, TEXT("the prepared reload was published before the commit")));
		F.Subsystem->CommitPendingReloads();
		ASSERT_THAT(IsTrue(F.Subsystem->GetActiveDefinition(SourceFile) != Before, TEXT("the commit did not publish the reload")));
		ASSERT_THAT(IsTrue(&Brain->GetDefinition().Get() == F.Subsystem->GetActiveDefinition(SourceFile).Get(), TEXT("the brain does not run the published definition")));
		Brain->Cleanup();
	}

	TEST_METHOD(ARequestDuringACompileWinsAndBadOrUnknownFilesAreReported)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.StartBrain();
		ASSERT_THAT(IsFalse(F.Subsystem->RequestReloadAsync(TEXT("never_loaded.xml")), TEXT("a never-loaded file was queued")));
		ASSERT_THAT(IsFalse(F.Subsystem->RequestReloadAsync(TEXT("../escape.xml")), TEXT("an escaping path was queued")));
		F.Tree.Write(SourceFile, TEXT("<Agent"));
		ASSERT_THAT(IsTrue(F.Subsystem->RequestReloadAsync(SourceFile), TEXT("the async reload was refused")));
		// The second request lands while the broken edit compiles; only the edit after it counts.
		F.Tree.Write(SourceFile, MakeXml(TEXT("100")));
		ASSERT_THAT(IsTrue(F.Subsystem->RequestReloadAsync(SourceFile), TEXT("the async reload was refused")));
		F.Subsystem->FinishReloadsForTest();
		ASSERT_THAT(IsTrue(F.Subsystem->GetLastAsyncReloadResult(SourceFile) == TOptional<EADSKReloadResult>(EADSKReloadResult::Unchanged),
			FString::Printf(TEXT("the last async reload is %s"), F.Subsystem->GetLastAsyncReloadResult(SourceFile).IsSet() ? LexToString(*F.Subsystem->GetLastAsyncReloadResult(SourceFile)) : TEXT("unset"))));
		F.Tree.Write(SourceFile, TEXT("<Agent"));
		ASSERT_THAT(IsTrue(F.Subsystem->RequestReloadAsync(SourceFile), TEXT("the async reload was refused")));
		F.Subsystem->FinishReloadsForTest();
		ASSERT_THAT(IsTrue(F.Subsystem->GetLastAsyncReloadResult(SourceFile) == TOptional<EADSKReloadResult>(EADSKReloadResult::Rejected),
			FString::Printf(TEXT("the last async reload is %s"), F.Subsystem->GetLastAsyncReloadResult(SourceFile).IsSet() ? LexToString(*F.Subsystem->GetLastAsyncReloadResult(SourceFile)) : TEXT("unset"))));
		ASSERT_THAT(AreEqual(FString(TEXT("compile")), F.Subsystem->GetLastDiagnostics(SourceFile)));
		Brain->Cleanup();
	}

	TEST_METHOD(ASynchronousRequestSupersedesACompileInFlight)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.StartBrain();
		F.Tree.Write(SourceFile, MakeXml(TEXT("50")));
		ASSERT_THAT(IsTrue(F.Subsystem->RequestReloadAsync(SourceFile), TEXT("the async reload was refused")));
		// The synchronous request reads a newer edit while the first one may still be compiling.
		F.Tree.Write(SourceFile, MakeXml(TEXT("25")));
		FString Error;
		ASSERT_THAT(IsTrue(F.Subsystem->RequestReload(SourceFile, Error) == EADSKReloadResult::Queued, Error));
		F.Subsystem->FinishReloadsForTest();
		ASSERT_THAT(IsFalse(F.Subsystem->GetLastAsyncReloadResult(SourceFile).IsSet(), TEXT("the synchronous reload left an async result")));
		F.Subsystem->CommitPendingReloads();
		const FADSKCompileResult Latest = FCompilerSpecSupport::LoadPathProbe(MakeXml(TEXT("25")));
		ASSERT_THAT(IsTrue(Latest.IsSuccess(), ADSKTest::FormatDiagnostics(Latest)));
		ASSERT_THAT(AreEqual(Latest.Definition->GetSourceHash(), F.Subsystem->GetActiveDefinition(SourceFile)->GetSourceHash()));
		ASSERT_THAT(IsTrue(&Brain->GetDefinition().Get() == F.Subsystem->GetActiveDefinition(SourceFile).Get(), TEXT("the brain does not run the published definition")));
		Brain->Cleanup();
	}
};

#endif
