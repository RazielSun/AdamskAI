// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"
#include "Misc/SecureHash.h"

using namespace FCompilerSpecSupport;

namespace FCompilerDefinitionSpecPrivate
{
	TSharedPtr<const FADSKDefinition> LoadGuard(FAutomationTestBase& Test)
	{
		return Load(FADSKGuardFixture::GuardXml(Test)).Definition;
	}

	FString NodeName(const FADSKDefinition& Definition, int32 Index)
	{
		return Definition.GetNodes()[Index].Id.ToString();
	}
}

using namespace FCompilerDefinitionSpecPrivate;

TEST_CLASS_WITH_FLAGS(FDefinitionTablesSpec, "AdamskAI.Compiler.Definition.Tables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(SlotsAndEventsKeepDocumentOrder)
	{
		const TSharedPtr<const FADSKDefinition> Definition = LoadGuard(*TestRunner);
		ASSERT_THAT(IsTrue(Definition.IsValid(), TEXT("the guard fixture does not compile")));
		const TConstArrayView<FADSKSlotDefinition> Slots = Definition->GetSlots();
		ASSERT_THAT(AreEqual(4, Slots.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("PatrolA")), Slots[0].Name));
		ASSERT_THAT(IsTrue(Slots[0].bIsInput && Slots[0].bRequired, TEXT("PatrolA is not a required input")));
		ASSERT_THAT(AreEqual(FString(TEXT("MayPatrol")), Slots[2].Name));
		ASSERT_THAT(IsFalse(Slots[2].bRequired, TEXT("MayPatrol is required")));
		ASSERT_THAT(IsTrue(Slots[2].Default == FADSKValue::MakeBool(true), LexToString(Slots[2].Default)));
		ASSERT_THAT(AreEqual(FString(TEXT("Interest")), Slots[3].Name));
		ASSERT_THAT(IsFalse(Slots[3].bIsInput, TEXT("Interest is an input")));
		ASSERT_THAT(AreEqual(EADSKValueType::Vector, Slots[3].Type, FString::Printf(TEXT("Interest is %s"), LexToString(Slots[3].Type))));
		const TConstArrayView<FADSKEventDefinition> Events = Definition->GetEvents();
		ASSERT_THAT(AreEqual(5, Events.Num()));
		ASSERT_THAT(IsTrue(Events[ADSK::TreeSuccessEvent].bReserved && Events[ADSK::TreeFailureEvent].bReserved, TEXT("the tree outcome events are not reserved")));
		ASSERT_THAT(AreEqual(FString(TEXT("Suspicious")), Events[2].Name));
		ASSERT_THAT(AreEqual(10, Events[2].Priority));
		ASSERT_THAT(AreEqual(1, Events[2].ParamCount));
		ASSERT_THAT(AreEqual(FString(TEXT("ContactLost")), Events[4].Name));
		ASSERT_THAT(AreEqual(20, Events[4].Priority));
		ASSERT_THAT(AreEqual(0, Events[4].ParamCount));
		ASSERT_THAT(AreEqual(3, Definition->FindSlot(TEXT("Interest"))));
		ASSERT_THAT(AreEqual(3, Definition->FindEvent(TEXT("ThreatConfirmed"))));
	}

	TEST_METHOD(NodesKeepTheAuthoredIdText)
	{
		// The FName may already exist with other casing (outside the editor it then keeps that one); the text does not.
		const FName Earlier(TEXT("AIMHIGH"));
		const FString Xml = TEXT("<Agent version=\"1\" id=\"a\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>"
			"<Tree id=\"t\"><Sequence id=\"AimHigh\"><Run id=\"Call\" tree=\"u\"/></Sequence></Tree><Tree id=\"u\"><Wait id=\"Hold\"/></Tree></Agent>");
		const FADSKCompileResult Result = Load(Xml);
		ASSERT_THAT(IsTrue(Result.IsSuccess() && !Earlier.IsNone(), ADSKTest::FormatDiagnostics(Result)));
		ASSERT_THAT(AreEqual(FString(TEXT("AimHigh")), Result.Definition->GetNodes()[0].AuthoredId));
		ASSERT_THAT(AreEqual(FString(TEXT("Call/Hold")), Result.Definition->GetNodes()[1].AuthoredId));
	}

	TEST_METHOD(StatesAndTransitionsAreIndexed)
	{
		const TSharedPtr<const FADSKDefinition> Definition = LoadGuard(*TestRunner);
		ASSERT_THAT(IsTrue(Definition.IsValid(), TEXT("the guard fixture does not compile")));
		const TConstArrayView<FADSKStateDefinition> States = Definition->GetStates();
		ASSERT_THAT(AreEqual(6, States.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("mode")), States[0].Id));
		ASSERT_THAT(AreEqual(INDEX_NONE, States[0].Parent));
		ASSERT_THAT(AreEqual(1, States[0].Depth));
		ASSERT_THAT(AreEqual(1, States[0].Initial));
		ASSERT_THAT(AreEqual(FString(TEXT("active")), States[1].Id));
		ASSERT_THAT(AreEqual(2, States[1].Initial));
		ASSERT_THAT(AreEqual(4, States[1].ChildCount));
		ASSERT_THAT(AreEqual(2, States[1].Depth));
		ASSERT_THAT(AreEqual(5, Definition->FindState(TEXT("alert"))));
		const int32 FirstChild = States[1].FirstChild;
		ASSERT_THAT(AreEqual(2, Definition->GetStateChildren()[FirstChild]));
		ASSERT_THAT(AreEqual(5, Definition->GetStateChildren()[FirstChild + 3]));
		ASSERT_THAT(AreEqual(INDEX_NONE, States[2].Initial));
		ASSERT_THAT(AreEqual(0, States[2].RootNode));
		ASSERT_THAT(AreEqual(6, States[3].RootNode));
		ASSERT_THAT(AreEqual(9, States[4].RootNode));
		ASSERT_THAT(AreEqual(14, States[5].RootNode));
		const FADSKTransitionDefinition& Refresh = Definition->GetTransitions()[States[5].FirstTransition];
		ASSERT_THAT(AreEqual(3, Refresh.Event));
		ASSERT_THAT(AreEqual(INDEX_NONE, Refresh.Target));
		ASSERT_THAT(AreEqual(1, Refresh.BindCount));
		const FADSKBindDefinition& Bind = Definition->GetBinds()[Refresh.FirstBind];
		ASSERT_THAT(AreEqual(0, Bind.Param));
		ASSERT_THAT(AreEqual(3, Bind.Slot));
		const FADSKTransitionDefinition& Loop = Definition->GetTransitions()[States[2].FirstTransition + 1];
		ASSERT_THAT(AreEqual(ADSK::TreeSuccessEvent, Loop.Event));
		ASSERT_THAT(AreEqual(2, Loop.Target));
		ASSERT_THAT(AreEqual(2, Loop.Source));
	}

	TEST_METHOD(NodesPortsAndConfigAreLinked)
	{
		const TSharedPtr<const FADSKDefinition> Definition = LoadGuard(*TestRunner);
		ASSERT_THAT(IsTrue(Definition.IsValid(), TEXT("the guard fixture does not compile")));
		ASSERT_THAT(AreEqual(15, Definition->GetNodes().Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("may")), NodeName(*Definition, 0)));
		ASSERT_THAT(AreEqual(FString(TEXT("toA")), NodeName(*Definition, 2)));
		ASSERT_THAT(AreEqual(FString(TEXT("watchInterest")), NodeName(*Definition, 14)));
		const FADSKNodeDefinition& Legs = Definition->GetNodes()[1];
		ASSERT_THAT(AreEqual(0, Legs.Parent));
		ASSERT_THAT(AreEqual(4, Legs.ChildCount));
		ASSERT_THAT(AreEqual(2, Definition->GetNodeChildren()[Legs.FirstChild]));
		const FADSKNodeDefinition& MoveToA = Definition->GetNodes()[2];
		ASSERT_THAT(IsTrue(MoveToA.Descriptor == Definition->GetRegistry()->Find(TEXT("MoveTo")), TEXT("node 2 is not the registry's MoveTo")));
		ASSERT_THAT(AreEqual(1, MoveToA.PortCount));
		ASSERT_THAT(AreEqual(0, Definition->GetPortBindings()[MoveToA.FirstPortBinding]));
		ASSERT_THAT(AreEqual(2, Definition->GetPortBindings()[Definition->GetNodes()[0].FirstPortBinding]));
		ASSERT_THAT(IsNear(50.f, MoveToA.Config.Get<FADSKTestMoveConfig>().AcceptanceRadius, UE_SMALL_NUMBER));
		ASSERT_THAT(IsNear(2.f, Definition->GetNodes()[3].Config.Get<FADSKWaitConfig>().Duration, UE_SMALL_NUMBER));
		ASSERT_THAT(IsFalse(Definition->GetNodes()[0].Config.Get<FADSKGuardConfig>().bInvert, TEXT("the patrol guard is inverted")));
		ASSERT_THAT(AreEqual(2, Definition->FindNode(TEXT("toA"))));
	}

	TEST_METHOD(ConfigValuesOfEveryFieldTypeArePreserved)
	{
		const TSharedPtr<const FADSKDefinition> Definition =
			LoadProbe(Mutate(15, "<Probe id=\"w\" flag=\"true\" scale=\"-2.5\" offset=\"1,2,3\"/>")).Definition;
		ASSERT_THAT(IsTrue(Definition.IsValid(), TEXT("the probe does not compile")));
		const FADSKTestFieldConfig& Config = Definition->GetNodes()[0].Config.Get<FADSKTestFieldConfig>();
		ASSERT_THAT(IsTrue(Config.bFlag, TEXT("flag is false")));
		ASSERT_THAT(IsTrue(Config.Scale == -2.5f, FString::Printf(TEXT("scale is %f"), Config.Scale)));
		ASSERT_THAT(AreEqual(FVector(1, 2, 3), Config.Offset));
	}
};

TEST_CLASS_WITH_FLAGS(FDefinitionResultSpec, "AdamskAI.Compiler.Definition.Results",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(WarningTravelsWithAValidDefinition)
	{
		const FADSKCompileResult Result = Load(FADSKGuardFixture::GuardXmlWithUnusedTree(*TestRunner));
		ASSERT_THAT(IsTrue(Result.Definition.IsValid() && Result.Count(EADSKDiagnosticCode::UnreferencedTree) == 1, ADSKTest::FormatDiagnostics(Result)));
		ASSERT_THAT(AreEqual(5, Result.Definition->GetTrees().Num()));
	}

	TEST_METHOD(AnyErrorYieldsNoDefinition)
	{
		const FADSKCompileResult Result = Load(Mutate(15, "<Bogus id=\"w\"/>"));
		ASSERT_THAT(IsFalse(Result.Definition.IsValid(), TEXT("a definition despite an error")));
		ASSERT_THAT(IsTrue(Result.HasError() && !Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
	}

	TEST_METHOD(SourceHashIsTheUtf8Sha1AndStable)
	{
		const FString Xml = FADSKGuardFixture::GuardXml(*TestRunner);
		const FTCHARToUTF8 Utf8(*Xml);
		FSHAHash Expected;
		FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Expected.Hash);
		const TSharedPtr<const FADSKDefinition> First = Load(Xml).Definition;
		const TSharedPtr<const FADSKDefinition> Second = Load(Xml).Definition;
		const TSharedPtr<const FADSKDefinition> Other = Load(Xml + TEXT("<!-- changed -->")).Definition;
		ASSERT_THAT(IsTrue(First.IsValid() && Second.IsValid() && Other.IsValid(), TEXT("the guard fixture does not compile")));
		ASSERT_THAT(AreEqual(Expected, First->GetSourceHash()));
		ASSERT_THAT(AreEqual(Expected, Second->GetSourceHash()));
		ASSERT_THAT(AreNotEqual(Expected, Other->GetSourceHash()));
		ASSERT_THAT(AreEqual(FString(TEXT("spec.xml")), First->GetSourceName()));
		ASSERT_THAT(AreEqual(1, First->GetLanguageVersion()));
		ASSERT_THAT(AreEqual(FString(TEXT("guard")), First->GetAgentId()));
	}

	TEST_METHOD(DefinitionRetainsTheRegistryAfterTheCallerReleasesIt)
	{
		TSharedPtr<const FADSKDefinition> Definition;
		{
			const TSharedRef<FADSKTestLeafLog> Log = MakeShared<FADSKTestLeafLog>();
			Definition = FADSKCompiler::Compile(TEXT("spec.xml"), FADSKGuardFixture::GuardXml(*TestRunner), FADSKTestNodes::MakeFrozenRegistry(Log)).Definition;
		}
		ASSERT_THAT(IsTrue(Definition.IsValid(), TEXT("the guard fixture does not compile")));
		const FADSKNodeDescriptor* MoveTo = Definition->GetRegistry()->Find(TEXT("MoveTo"));
		ASSERT_THAT(IsNotNull(MoveTo));
		ASSERT_THAT(IsTrue(Definition->GetNodes()[2].Descriptor == MoveTo, TEXT("node 2 is not the registry's MoveTo")));
		ASSERT_THAT(AreEqual(FString(TEXT("MoveTo")), MoveTo->Name));
		ASSERT_THAT(AreEqual(1, MoveTo->Ports.Num()));
	}
};

#endif
