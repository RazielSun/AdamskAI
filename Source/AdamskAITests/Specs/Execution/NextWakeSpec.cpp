// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Runtime/ADSKInstance.h"
#include "CQTest.h"
#include "Support/ExecutionTestSupport.h"
#include "Support/TestChecks.h"

// FADSKInstance::GetNextWake: when an instance is next due, and that ticking only then runs the same as every frame.
namespace FNextWakeSpecPrivate
{
	/** Patrol (count steps through Locals, two waits) until Alert, then alert; a Cooldown option interrupts the patrol. */
	const TCHAR* Xml = TEXT(
		"<Agent version=\"1\" id=\"wake\">"
		"<Inputs><Input name=\"Alert\" type=\"Bool\" default=\"false\"/></Inputs>"
		"<Locals><Local name=\"Step\" type=\"Int\"/></Locals>"
		"<Events><Event name=\"AlertChanged\" change=\"Alert\"/></Events>"
		"<StateMachine id=\"m\" initial=\"patrol\">"
		"<State id=\"patrol\" tree=\"patrol\"><On event=\"AlertChanged\" goto=\"alert\" if=\"Alert\"/><On event=\"tree.success\" goto=\"patrol\"/></State>"
		"<State id=\"alert\" tree=\"alert\"><On event=\"AlertChanged\" goto=\"patrol\" unless=\"Alert\"/><On event=\"tree.success\" goto=\"rest\"/></State>"
		"<State id=\"rest\"/>"
		"</StateMachine>"
		"<Tree id=\"patrol\"><ReactiveSelector id=\"pick\">"
		"<Cooldown id=\"shout_ready\" duration=\"1.5\"><Set id=\"shout\" to=\"Step\" value=\"9\"/></Cooldown>"
		"<Sequence id=\"walk\"><Set id=\"walk_a\" to=\"Step\" value=\"1\"/><Wait id=\"wait_a\" duration=\"0.7\"/>"
		"<Set id=\"walk_b\" to=\"Step\" value=\"2\"/><Wait id=\"wait_b\" duration=\"0.4\"/></Sequence>"
		"</ReactiveSelector></Tree>"
		"<Tree id=\"alert\"><Wait id=\"alert_wait\" duration=\"2\"/></Tree>"
		"</Agent>");

	const TCHAR* WaitXml = TEXT(
		"<Agent version=\"1\" id=\"w\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>"
		"<Tree id=\"t\"><Wait id=\"w\" duration=\"2\"/></Tree></Agent>");

	/** Frames of 1/60 s for Seconds; Alert raised at RaiseAt and lowered at LowerAt seconds. */
	struct FRun
	{
		FADSKTestRuntime Runtime{Xml};
		int32 Ticks = 0;

		void Drive(float Seconds, float RaiseAt, float LowerAt, bool bSleep)
		{
			Runtime.Instance->Start();
			const float Frame = 1.f / 60.f;
			float Slept = 0.f;
			float Next = 0.f;
			bool bRaised = false, bLowered = false;
			for (float Time = Frame; Time <= Seconds + KINDA_SMALL_NUMBER; Time += Frame)
			{
				if (!bRaised && Time >= RaiseAt) { bRaised = true; Runtime.Instance->SetInput(TEXT("Alert"), FADSKValue::MakeBool(true)); Next = 0.f; }
				if (!bLowered && Time >= LowerAt) { bLowered = true; Runtime.Instance->SetInput(TEXT("Alert"), FADSKValue::MakeBool(false)); Next = 0.f; }
				Slept += Frame;
				if (bSleep && Slept + KINDA_SMALL_NUMBER < Next) continue;
				Runtime.Instance->Tick(Slept);
				++Ticks;
				Slept = 0.f;
				Next = Runtime.Instance->GetNextWake();
			}
		}

		FString Summary() const
		{
			FADSKValue Step;
			Runtime.Instance->GetValue(TEXT("Step"), Step);
			return FString::Join(Runtime.Instance->GetActiveStatePath(), TEXT("/")) + TEXT(" step ") + LexToString(Step);
		}
	};
}

using namespace FNextWakeSpecPrivate;

TEST_CLASS_WITH_FLAGS(FNextWakeSpec, "AdamskAI.Execution.NextWake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AWaitIsDueWhenItsTimeRunsOut)
	{
		FADSKTestRuntime Runtime(WaitXml);
		ASSERT_THAT(IsTrue(Runtime.Instance->GetNextWake() == ADSK::NeverWake, FString::SanitizeFloat(Runtime.Instance->GetNextWake())));
		Runtime.Instance->Start();
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Runtime.Instance->GetNextWake(), 2.f), FString::SanitizeFloat(Runtime.Instance->GetNextWake())));
		Runtime.Instance->Tick(0.5f);
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Runtime.Instance->GetNextWake(), 1.5f), FString::SanitizeFloat(Runtime.Instance->GetNextWake())));
		// The tree ends with the wait; its completion is handled on the next tick, then nothing is left to time.
		Runtime.Instance->Tick(1.5f);
		ASSERT_THAT(IsTrue(Runtime.Instance->GetNextWake() == 0.f, FString::SanitizeFloat(Runtime.Instance->GetNextWake())));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsTrue(Runtime.Instance->IsHolding(), TEXT("the brain is not holding")));
		ASSERT_THAT(IsTrue(Runtime.Instance->GetNextWake() == ADSK::NeverWake, FString::SanitizeFloat(Runtime.Instance->GetNextWake())));
	}

	TEST_METHOD(QueuedWorkIsDueAtOnce)
	{
		FADSKTestRuntime Runtime(Xml);
		Runtime.Instance->Start();
		// Each tick that writes Step makes the next one due; once the walk waits, nothing is.
		for (int32 Tick = 0; Tick < 3; ++Tick) Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsTrue(Runtime.Instance->GetNextWake() > 0.f, TEXT("a waiting patrol is due at once")));
		Runtime.Instance->SetInput(TEXT("Alert"), FADSKValue::MakeBool(true));
		ASSERT_THAT(IsTrue(Runtime.Instance->GetNextWake() == 0.f, FString::SanitizeFloat(Runtime.Instance->GetNextWake())));
	}

	TEST_METHOD(ACooldownOptionIsDueWhenItIsReady)
	{
		FADSKTestRuntime Runtime(Xml);
		Runtime.Instance->Start();
		// The shout ran at once and started its cooldown; the walk runs now and the cooldown is checked at 1.5 s.
		for (int32 Tick = 0; Tick < 3; ++Tick) Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Runtime.Instance->GetNextWake(), 0.7f), FString::SanitizeFloat(Runtime.Instance->GetNextWake())));
		Runtime.Instance->Tick(0.7f);
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Runtime.Instance->GetNextWake(), 0.4f), FString::SanitizeFloat(Runtime.Instance->GetNextWake())));
	}

	TEST_METHOD(TickingOnlyWhenDueEndsWhereTickingEveryFrameEnds)
	{
		for (const TPair<float, float>& Alert : {TPair<float, float>(1.3f, 2.2f), TPair<float, float>(0.25f, 5.f), TPair<float, float>(3.f, 3.1f)})
		{
			FRun Every, Sleeping;
			Every.Drive(6.f, Alert.Key, Alert.Value, false);
			Sleeping.Drive(6.f, Alert.Key, Alert.Value, true);
			ASSERT_THAT(AreEqual(Every.Summary(), Sleeping.Summary()));
			ASSERT_THAT(IsTrue(Sleeping.Ticks * 4 < Every.Ticks, FString::Printf(TEXT("%d ticks asleep against %d"), Sleeping.Ticks, Every.Ticks)));
		}
	}
};

#endif
