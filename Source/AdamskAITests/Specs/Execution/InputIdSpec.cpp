// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/GuardFixture.h"
#include "CQTest.h"

TEST_CLASS_WITH_FLAGS(FInputIdSpec, "AdamskAI.Execution.Input.Ids",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ResolvedIdsWriteAndQueueLikeNames)
	{
		FADSKTestRuntime Runtime(FADSKGuardFixture::LeafXml());
		const FADSKInputId Goal = Runtime.Instance->FindInput(TEXT("Goal"));
		ASSERT_THAT(IsTrue(Goal.IsValid(), TEXT("Goal did not resolve")));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->SetInput(Goal, FADSKValue::MakeVector(FVector(4, 5, 6)))));
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Runtime.Instance->SetInput(Goal, FADSKValue::MakeBool(true))));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		FADSKValue Value;
		ASSERT_THAT(IsTrue(Runtime.Instance->GetValue(TEXT("Goal"), Value), TEXT("no slot Goal")));
		ASSERT_THAT(AreEqual(FVector(4, 5, 6), Value.Vector));
		const FADSKEventId Go = Runtime.Instance->FindEvent(TEXT("Go"));
		ASSERT_THAT(IsTrue(Go.IsValid(), TEXT("Go did not resolve")));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->EnqueueEvent(Go)));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("fail")}));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(UnknownReservedAndLocalNamesDoNotResolve)
	{
		FADSKTestRuntime Runtime(FADSKGuardFixture::LeafXml());
		ASSERT_THAT(IsFalse(Runtime.Instance->FindInput(TEXT("Nope")).IsValid(), TEXT("Nope resolved")));
		ASSERT_THAT(IsFalse(Runtime.Instance->FindInput(TEXT("goal")).IsValid(), TEXT("goal resolved ignoring case")));
		ASSERT_THAT(IsFalse(Runtime.Instance->FindEvent(TEXT("tree.success")).IsValid(), TEXT("tree.success resolved as a host event")));
		ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Runtime.Instance->SetInput(FADSKInputId(), FADSKValue::MakeVector(FVector::ZeroVector))));
	}

	TEST_METHOD(IdsFromAnotherDefinitionAreRefusedEvenWithTheSameLayout)
	{
		FADSKTestRuntime First(FADSKGuardFixture::LeafXml());
		FADSKTestRuntime Second(FADSKGuardFixture::LeafXml());
		const FADSKInputId Foreign = First.Instance->FindInput(TEXT("Goal"));
		ASSERT_THAT(AreEqual(Second.Instance->FindInput(TEXT("Goal")).Slot, Foreign.Slot));
		ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Second.Instance->SetInput(Foreign, FADSKValue::MakeVector(FVector::OneVector))));
		Second.Instance->SetInput(TEXT("Goal"), FADSKValue::MakeVector(FVector::ZeroVector));
		ADSK_ASSERT(ADSKTest::Start(*Second.Instance));
		ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Second.Instance->EnqueueEvent(First.Instance->FindEvent(TEXT("Go")))));
		Second.Instance->Stop();
	}
};

#endif
