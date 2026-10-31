// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Runtime/ADSKInstance.h"
#include "Support/GuardFixture.h"
#include "Support/TestADSKNodes.h"
#include "Support/TestChecks.h"

class FADSKGuardScenarioDriver
{
public:
	FADSKGuardScenarioDriver(FAutomationTestBase& Test, int32 RecorderCapacity)
		: Log(MakeShared<FADSKTestLeafLog>())
		, Registry(FADSKTestNodes::MakeFrozenRegistry(Log))
		, Definition(ADSKTest::LoadOrStandIn(TEXT("guard-scenario.xml"), FADSKGuardFixture::GuardXml(Test), Registry))
		, Recorder(RecorderCapacity >= 2 ? FADSKRecorder::Create(RecorderCapacity) : nullptr)
	{
		Instance = MakeUnique<FADSKInstance>(Definition.ToSharedRef(), Recorder);
		Instance->SetInput(TEXT("PatrolA"), FADSKValue::MakeVector(FVector(100, 0, 0)));
		Instance->SetInput(TEXT("PatrolB"), FADSKValue::MakeVector(FVector(200, 0, 0)));
		ADSKTest::Expect(ADSKTest::Start(*Instance));
		CaptureDecision();
	}

	void Advance(float Delta = 0.1f)
	{
		Instance->Tick(Delta);
		CaptureDecision();
	}

	void AdvanceTicks(int32 Count, float Delta = 0.1f)
	{
		for (int32 Index = 0; Index < Count; ++Index) Advance(Delta);
	}

	void Event(const TCHAR* Name, const FVector* Location = nullptr)
	{
		if (Location)
		{
			Instance->EnqueueEvent(Name, {{TEXT("Location"), FADSKValue::MakeVector(*Location)}});
		}
		else
		{
			Instance->EnqueueEvent(Name);
		}
		Advance(0.f);
	}

	FADSKActivationHandle ActiveHandle() const { return Instance->GetActiveLeafHandle(); }

	void Complete(EADSKNodeStatus Status = EADSKNodeStatus::Succeeded)
	{
		Instance->PostActionResult(ActiveHandle(), Status);
		Advance(0.f);
	}

	void FinishInvestigate(EADSKNodeStatus MoveStatus = EADSKNodeStatus::Succeeded)
	{
		Complete(MoveStatus);
		if (MoveStatus == EADSKNodeStatus::Succeeded) Complete();
		Advance(0.f);
	}

	void FinishSearch()
	{
		Complete();
		AdvanceTicks(10);
		Complete();
		AdvanceTicks(10);
		Advance(0.f);
	}

	void FinishPatrolCycle()
	{
		Complete();
		AdvanceTicks(20);
		Complete();
		AdvanceTicks(20);
		Advance(0.f);
	}

	FVector Interest() const
	{
		FADSKValue Value;
		ADSKTest::Expect(Instance->GetValue(TEXT("Interest"), Value), TEXT("the guard fixture has no Interest slot"));
		return Value.Vector;
	}

	FADSKCheck PathIs(std::initializer_list<const TCHAR*> Expected) const
	{
		return ADSKTest::PathIs(*Instance, Expected, Recorder.Get());
	}

	FADSKCheck StopAndBalanced()
	{
		Instance->Stop();
		if (!Log->IsBalanced())
		{
			return FADSKCheck::Fail(FString::Printf(TEXT("%d leaf starts but %d ends"), Log->Starts, Log->Ends) + Trace());
		}
		if (!Recorder) return FADSKCheck::Pass();
		TMap<uint64, int32> Balance;
		for (const FADSKRecord& Record : Recorder->Snapshot())
		{
			const uint64 Key = (static_cast<uint64>(Record.Handle.Generation) << 32) | Record.Handle.ActivationId;
			if (Record.Kind == EADSKRecordKind::NodeStarted) ++Balance.FindOrAdd(Key);
			if (Record.Kind == EADSKRecordKind::NodeEnded) --Balance.FindOrAdd(Key);
		}
		for (const TPair<uint64, int32>& Pair : Balance)
		{
			if (Pair.Value != 0)
			{
				return FADSKCheck::Fail(FString::Printf(TEXT("handle %u.%u has %d unmatched node-started records"),
					static_cast<uint32>(Pair.Key >> 32), static_cast<uint32>(Pair.Key), Pair.Value) + Trace());
			}
		}
		return FADSKCheck::Pass();
	}

	FString Trace() const
	{
		return ADSKTest::FormatTrace(Recorder.Get(), *Definition);
	}

	FString Decisions;
	TSharedRef<FADSKTestLeafLog> Log;
	TSharedRef<const FADSKNodeRegistry> Registry;
	TSharedPtr<const FADSKDefinition> Definition;
	TSharedPtr<FADSKRecorder> Recorder;
	TUniquePtr<FADSKInstance> Instance;

private:
	void CaptureDecision()
	{
		for (; CapturedLines < Log->Lines.Num(); ++CapturedLines)
		{
			Decisions += Log->Lines[CapturedLines] + TEXT(";");
		}
		// Read without Expect: a stand-in definition has no Interest, and its failure is reported once already.
		FADSKValue Interest;
		Instance->GetValue(TEXT("Interest"), Interest);
		Decisions += FString::Join(Instance->GetActiveStatePath(), TEXT("/"));
		Decisions += TEXT(":") + Interest.Vector.ToString() + TEXT("|");
	}

	int32 CapturedLines = 0;
};
