// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Nodes/ADSKBuiltinNodes.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Support/TestADSKNodes.h"
#include "Support/TestChecks.h"
#include "CQTest.h"

namespace FRegistrySpecPrivate
{
	FADSKNodeDescriptor Leaf(const TCHAR* Name)
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
		return Descriptor;
	}

	FADSKNodeDescriptor WithConfig(const TCHAR* Name, const UScriptStruct* Config, const TCHAR* Xml, const TCHAR* Property)
	{
		FADSKNodeDescriptor Descriptor = Leaf(Name);
		Descriptor.ConfigStruct = Config;
		Descriptor.Fields.Add({Xml, Property});
		return Descriptor;
	}

	TSharedRef<FADSKNodeRegistry> Fresh()
	{
		return FADSKNodeRegistry::Create();
	}

	FADSKCheck Is(EADSKRegisterResult Actual, EADSKRegisterResult Expected)
	{
		if (Actual == Expected)
		{
			return FADSKCheck::Pass();
		}
		return FADSKCheck::Fail(FString::Printf(TEXT("registration returned %s, expected %s"), LexToString(Actual), LexToString(Expected)));
	}
}

using namespace FRegistrySpecPrivate;

TEST_CLASS_WITH_FLAGS(FRegistryLiteralSpec, "AdamskAI.Registry.Literals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(BoolIsExactLowercase)
	{
		bool bValue = false;
		ASSERT_THAT(IsTrue(ADSK::TryParseBool(TEXT("true"), bValue), TEXT("rejected true")));
		ASSERT_THAT(IsTrue(bValue, TEXT("true parsed as false")));
		ASSERT_THAT(IsTrue(ADSK::TryParseBool(TEXT("false"), bValue), TEXT("rejected false")));
		ASSERT_THAT(IsFalse(bValue, TEXT("false parsed as true")));
		for (const TCHAR* Bad : {TEXT("True"), TEXT("1"), TEXT(""), TEXT(" true"), TEXT("yes")})
		{
			ASSERT_THAT(IsFalse(ADSK::TryParseBool(Bad, bValue), FString::Printf(TEXT("accepted [%s]"), Bad)));
		}
	}

	TEST_METHOD(FloatGrammarIsStrict)
	{
		float Value = 0.f;
		ASSERT_THAT(IsTrue(ADSK::TryParseFloat(TEXT("-12.5"), Value), TEXT("rejected -12.5")));
		ASSERT_THAT(IsTrue(Value == -12.5f, FString::Printf(TEXT("-12.5 parsed as %f"), Value)));
		ASSERT_THAT(IsTrue(ADSK::TryParseFloat(TEXT("0"), Value), TEXT("rejected 0")));
		ASSERT_THAT(IsTrue(Value == 0.f, FString::Printf(TEXT("0 parsed as %f"), Value)));
		for (const TCHAR* Bad : {TEXT(""), TEXT("+1"), TEXT("1e3"), TEXT("1."), TEXT(".5"), TEXT(" 1"), TEXT("1 "), TEXT("nan"),
				 TEXT("inf"), TEXT("--1"), TEXT("1.2.3"), TEXT("1e400"), TEXT("9999999999999999999999999999999999999999999")})
		{
			ASSERT_THAT(IsFalse(ADSK::TryParseFloat(Bad, Value), FString::Printf(TEXT("accepted [%s]"), Bad)));
		}
	}

	TEST_METHOD(VectorNeedsThreeStrictComponents)
	{
		FVector Value;
		ASSERT_THAT(IsTrue(ADSK::TryParseVector(TEXT("0,90,-1.5"), Value), TEXT("rejected 0,90,-1.5")));
		ASSERT_THAT(AreEqual(FVector(0, 90, -1.5), Value));
		for (const TCHAR* Bad : {TEXT("1,2"), TEXT("1,2,3,4"), TEXT("1, 2,3"), TEXT("1,2,"), TEXT(",1,2"), TEXT("1,2,x"), TEXT("")})
		{
			ASSERT_THAT(IsFalse(ADSK::TryParseVector(Bad, Value), FString::Printf(TEXT("accepted [%s]"), Bad)));
		}
	}

	TEST_METHOD(EqualityComparesTypeAndActiveMember)
	{
		FADSKValue A = FADSKValue::MakeFloat(1.f);
		FADSKValue B = FADSKValue::MakeBool(true);
		B.Float = 1.f;
		ASSERT_THAT(IsTrue(A != B, TEXT("a Float equals a Bool with the same Float member")));
		ASSERT_THAT(IsTrue(FADSKValue::MakeBool(true) == FADSKValue::MakeBool(true), TEXT("true differs from true")));
		FADSKValue Noisy = FADSKValue::MakeBool(true);
		Noisy.Float = 99.f;
		ASSERT_THAT(IsTrue(Noisy == FADSKValue::MakeBool(true), TEXT("an inactive member changed equality")));
		ASSERT_THAT(AreEqual(FString(TEXT("1.0,2.0,3.0")), LexToString(FADSKValue::MakeVector(FVector(1, 2, 3)))));
	}
};

TEST_CLASS_WITH_FLAGS(FRegistryNamesSpec, "AdamskAI.Registry.Names",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(NodeNamesAreExactCase)
	{
		const TSharedRef<FADSKTestLeafLog> Log = MakeShared<FADSKTestLeafLog>();
		const TSharedRef<const FADSKNodeRegistry> Registry = FADSKTestNodes::MakeFrozenRegistry(Log);
		ASSERT_THAT(IsNotNull(Registry->Find(TEXT("MoveTo"))));
		ASSERT_THAT(IsNull(Registry->Find(TEXT("moveto"))));
		ASSERT_THAT(IsNull(Registry->Find(TEXT("MOVETO"))));
		ASSERT_THAT(IsNotNull(Registry->Find(TEXT("Wait"))));
	}

	TEST_METHOD(FieldAndPortNamesAreExactCase)
	{
		const TSharedRef<FADSKTestLeafLog> Log = MakeShared<FADSKTestLeafLog>();
		const TSharedRef<const FADSKNodeRegistry> Registry = FADSKTestNodes::MakeFrozenRegistry(Log);
		const FADSKNodeDescriptor* MoveTo = Registry->Find(TEXT("MoveTo"));
		ASSERT_THAT(IsNotNull(MoveTo));
		ASSERT_THAT(IsNotNull(MoveTo->FindField(TEXT("acceptanceRadius"))));
		ASSERT_THAT(IsNull(MoveTo->FindField(TEXT("acceptanceradius"))));
		ASSERT_THAT(AreEqual(0, MoveTo->FindPort(TEXT("goal"))));
		ASSERT_THAT(AreEqual(static_cast<int32>(INDEX_NONE), MoveTo->FindPort(TEXT("Goal"))));
	}

	TEST_METHOD(CaseDifferingNamesRegisterSeparately)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = Fresh();
		ADSK_ASSERT(Is(Registry->Register(Leaf(TEXT("MoveTo"))), EADSKRegisterResult::Accepted));
		ADSK_ASSERT(Is(Registry->Register(Leaf(TEXT("moveto"))), EADSKRegisterResult::Accepted));
		ADSK_ASSERT(Is(Registry->Register(Leaf(TEXT("MoveTo"))), EADSKRegisterResult::DuplicateName));
		ASSERT_THAT(IsFalse(Registry->Find(TEXT("MoveTo")) == Registry->Find(TEXT("moveto")), TEXT("MoveTo and moveto are one descriptor")));
	}
};

TEST_CLASS_WITH_FLAGS(FRegistryResultSpec, "AdamskAI.Registry.Results",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(NameRulesReportInOrder)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = Fresh();
		ADSK_ASSERT(Is(Registry->Register(Leaf(TEXT("9Lives"))), EADSKRegisterResult::InvalidName));
		ADSK_ASSERT(Is(Registry->Register(Leaf(TEXT("Has-Dash"))), EADSKRegisterResult::InvalidName));
		ADSK_ASSERT(Is(Registry->Register(Leaf(TEXT(""))), EADSKRegisterResult::InvalidName));
		ADSK_ASSERT(Is(Registry->Register(Leaf(TEXT("State"))), EADSKRegisterResult::ReservedName));
		ADSK_ASSERT(Is(Registry->Register(Leaf(TEXT("Agent"))), EADSKRegisterResult::ReservedName));
		ADSK_ASSERT(Is(Registry->Register(Leaf(TEXT("Ok"))), EADSKRegisterResult::Accepted));
		ADSK_ASSERT(Is(Registry->Register(Leaf(TEXT("Ok"))), EADSKRegisterResult::DuplicateName));
	}

	TEST_METHOD(ArityShapesAreEnforced)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = Fresh();
		FADSKNodeDescriptor NoStart = Leaf(TEXT("NoStart"));
		NoStart.OnStart = nullptr;
		ADSK_ASSERT(Is(Registry->Register(NoStart), EADSKRegisterResult::InvalidArity));
		FADSKNodeDescriptor EmptySequence;
		EmptySequence.Name = TEXT("EmptySeq");
		EmptySequence.Kind = EADSKNodeKind::Sequence;
		ADSK_ASSERT(Is(Registry->Register(EmptySequence), EADSKRegisterResult::InvalidArity));
		FADSKNodeDescriptor Guard;
		Guard.Name = TEXT("BadGuard");
		Guard.Kind = EADSKNodeKind::Guard;
		Guard.MinChildren = Guard.MaxChildren = 1;
		ADSK_ASSERT(Is(Registry->Register(Guard), EADSKRegisterResult::InvalidArity));
		Guard.Ports.Add({TEXT("condition"), EADSKValueType::Float, EADSKPortDirection::In});
		ADSK_ASSERT(Is(Registry->Register(Guard), EADSKRegisterResult::InvalidArity));
		Guard.Ports[0].Type = EADSKValueType::Bool;
		ADSK_ASSERT(Is(Registry->Register(Guard), EADSKRegisterResult::Accepted));
	}

	TEST_METHOD(FieldRulesReportInOrder)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = Fresh();
		const UScriptStruct* Config = FADSKTestFieldConfig::StaticStruct();
		ADSK_ASSERT(Is(Registry->Register(WithConfig(TEXT("A1"), Config, TEXT("id"), TEXT("Scale"))), EADSKRegisterResult::InvalidFieldName));
		ADSK_ASSERT(Is(Registry->Register(WithConfig(TEXT("A2"), Config, TEXT("in-x"), TEXT("Scale"))), EADSKRegisterResult::InvalidFieldName));
		ADSK_ASSERT(Is(Registry->Register(WithConfig(TEXT("A3"), Config, TEXT("out-x"), TEXT("Scale"))), EADSKRegisterResult::InvalidFieldName));
		ADSK_ASSERT(Is(Registry->Register(WithConfig(TEXT("A4"), Config, TEXT("-x"), TEXT("Scale"))), EADSKRegisterResult::InvalidFieldName));
		ADSK_ASSERT(Is(Registry->Register(WithConfig(TEXT("A5"), Config, TEXT("scale"), TEXT("Missing"))), EADSKRegisterResult::MissingProperty));
		ADSK_ASSERT(Is(Registry->Register(WithConfig(TEXT("A6"), Config, TEXT("wide"), TEXT("Wide"))), EADSKRegisterResult::UnsupportedFieldType));
		ADSK_ASSERT(Is(Registry->Register(WithConfig(TEXT("A6b"), Config, TEXT("count"), TEXT("Count"))), EADSKRegisterResult::Accepted));
		ADSK_ASSERT(Is(Registry->Register(WithConfig(TEXT("A7"), Config, TEXT("spin"), TEXT("Spin"))), EADSKRegisterResult::UnsupportedFieldType));
		FADSKNodeDescriptor Duplicate = WithConfig(TEXT("A8"), Config, TEXT("scale"), TEXT("Scale"));
		Duplicate.Fields.Add({TEXT("scale"), TEXT("bFlag")});
		ADSK_ASSERT(Is(Registry->Register(Duplicate), EADSKRegisterResult::DuplicateField));
		Duplicate.Fields[1].XmlName = TEXT("Scale");
		Duplicate.Fields.Add({TEXT("offset-a"), TEXT("Offset")});
		ADSK_ASSERT(Is(Registry->Register(Duplicate), EADSKRegisterResult::Accepted));
	}

	TEST_METHOD(PortRulesReportInOrder)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = Fresh();
		FADSKNodeDescriptor Node = WithConfig(TEXT("P1"), FADSKTestFieldConfig::StaticStruct(), TEXT("scale"), TEXT("Scale"));
		Node.Ports.Add({TEXT("bad name"), EADSKValueType::Bool, EADSKPortDirection::In});
		ADSK_ASSERT(Is(Registry->Register(Node), EADSKRegisterResult::InvalidPortName));
		Node.Ports[0].Name = TEXT("scale");
		ADSK_ASSERT(Is(Registry->Register(Node), EADSKRegisterResult::PortFieldCollision));
		Node.Ports[0].Name = TEXT("goal");
		Node.Ports.Add({TEXT("goal"), EADSKValueType::Vector, EADSKPortDirection::Out});
		ADSK_ASSERT(Is(Registry->Register(Node), EADSKRegisterResult::DuplicatePort));
		Node.Ports[1].Name = TEXT("Goal");
		ADSK_ASSERT(Is(Registry->Register(Node), EADSKRegisterResult::Accepted));
	}
};

TEST_CLASS_WITH_FLAGS(FRegistrySafetySpec, "AdamskAI.Registry.Safety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ObjectReferencesInConfigAreRejected)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = Fresh();
		FADSKNodeDescriptor Direct = Leaf(TEXT("Direct"));
		Direct.ConfigStruct = FADSKTestObjectConfig::StaticStruct();
		ADSK_ASSERT(Is(Registry->Register(Direct), EADSKRegisterResult::UnsafeStruct));
		FADSKNodeDescriptor Nested = WithConfig(TEXT("Nested"), FADSKTestNestedObjectConfig::StaticStruct(), TEXT("radius"), TEXT("Radius"));
		ADSK_ASSERT(Is(Registry->Register(Nested), EADSKRegisterResult::UnsafeStruct));
	}

	TEST_METHOD(ObjectReferencesInMemoryAreRejected)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = Fresh();
		FADSKNodeDescriptor Direct = Leaf(TEXT("MemDirect"));
		Direct.MemoryStruct = FADSKTestObjectConfig::StaticStruct();
		ADSK_ASSERT(Is(Registry->Register(Direct), EADSKRegisterResult::UnsafeStruct));
		FADSKNodeDescriptor Nested = Leaf(TEXT("MemNested"));
		Nested.MemoryStruct = FADSKTestNestedObjectConfig::StaticStruct();
		ADSK_ASSERT(Is(Registry->Register(Nested), EADSKRegisterResult::UnsafeStruct));
		FADSKNodeDescriptor Safe = Leaf(TEXT("MemSafe"));
		Safe.MemoryStruct = FADSKTestValueMemory::StaticStruct();
		ADSK_ASSERT(Is(Registry->Register(Safe), EADSKRegisterResult::Accepted));
	}
};

TEST_CLASS_WITH_FLAGS(FRegistryFrozenSpec, "AdamskAI.Registry.Frozen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(FreezeIsIdempotentAndBlocksRegistration)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = Fresh();
		ADSK_ASSERT(Is(Registry->Register(Leaf(TEXT("Before"))), EADSKRegisterResult::Accepted));
		Registry->Freeze();
		Registry->Freeze();
		ASSERT_THAT(IsTrue(Registry->IsFrozen(), TEXT("the registry is not frozen")));
		ADSK_ASSERT(Is(Registry->Register(Leaf(TEXT("After"))), EADSKRegisterResult::Frozen));
		ASSERT_THAT(IsNull(Registry->Find(TEXT("After"))));
		ASSERT_THAT(AreEqual(1, Registry->Num()));
	}

	TEST_METHOD(DefaultRegistryIsFrozenWithBuiltins)
	{
		const TSharedRef<const FADSKNodeRegistry> Registry = FADSKNodeRegistry::GetDefault();
		ASSERT_THAT(IsTrue(Registry->IsFrozen(), TEXT("the default registry is not frozen")));
		ASSERT_THAT(AreEqual(19, Registry->Num()));
		ASSERT_THAT(AreEqual(EADSKSelectorPolicy::Reactive, Registry->Find(TEXT("ReactiveSelector"))->SelectorPolicy));
		ASSERT_THAT(AreEqual(EADSKNodeKind::Selector, Registry->Find(TEXT("ReactiveSelector"))->Kind));
		ASSERT_THAT(AreEqual(EADSKNodeKind::Parallel, Registry->Find(TEXT("SimpleParallel"))->Kind));
		ASSERT_THAT(AreEqual(EADSKPortDirection::Out, Registry->Find(TEXT("Roll"))->Ports[0].Direction));
		ASSERT_THAT(AreEqual(EADSKNodeKind::Leaf, Registry->Find(TEXT("Chance"))->Kind));
		const FADSKNodeDescriptor* Wait = Registry->Find(TEXT("Wait"));
		ASSERT_THAT(IsNotNull(Wait));
		ASSERT_THAT(IsTrue(Wait->ConfigStruct == FADSKWaitConfig::StaticStruct(), TEXT("Wait's config is ") + GetNameSafe(Wait->ConfigStruct)));
		ASSERT_THAT(AreEqual(EADSKNodeKind::Guard, Registry->Find(TEXT("Guard"))->Kind));
		ASSERT_THAT(AreEqual(EADSKNodeKind::Emit, Registry->Find(TEXT("Emit"))->Kind));
		ASSERT_THAT(AreEqual(EADSKNodeKind::Set, Registry->Find(TEXT("Set"))->Kind));
		ASSERT_THAT(AreEqual(EADSKNodeKind::Concurrent, Registry->Find(TEXT("Parallel"))->Kind));
		ASSERT_THAT(IsTrue(static_cast<bool>(Registry->Find(TEXT("Cooldown"))->OnCheck)));
		ASSERT_THAT(AreEqual(EADSKNodeKind::Selector, Registry->Find(TEXT("Selector"))->Kind));
		ASSERT_THAT(AreEqual(ADSK::UnboundedChildren, Registry->Find(TEXT("Sequence"))->MaxChildren));
	}

	TEST_METHOD(UnfrozenSchemaExportIsEmptyAfterEnsure)
	{
		// An ensure logs a "Handled ensure" banner and the failed condition as separate errors.
		TestRunner->AddExpectedMessagePlain(TEXT("Handled ensure"), ELogVerbosity::Error,
			EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/0);
		for (const TCHAR* Line : {TEXT("Ensure condition failed: bFrozen"), TEXT("requires a frozen registry"), TEXT("Stack:"),
				 TEXT("[Callstack]")})
		{
			TestRunner->AddExpectedMessagePlain(Line, ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/0);
		}
		TestRunner->AddExpectedMessagePlain(TEXT(""), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Exact, /*Occurrences=*/0);
		TestRunner->AddExpectedMessagePlain(TEXT("Script Stack"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/0);
		const TSharedRef<FADSKNodeRegistry> Registry = Fresh();
		ASSERT_THAT(IsTrue(Registry->ExportSchemaJson().IsEmpty(), TEXT("an unfrozen registry exported a schema")));
	}
};

TEST_CLASS_WITH_FLAGS(FRegistrySchemaSpec, "AdamskAI.Registry.Schema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(SchemaIsByteIdenticalUnderReverseRegistration)
	{
		const TSharedRef<FADSKTestLeafLog> Log = MakeShared<FADSKTestLeafLog>();
		const FString Forward = FADSKTestNodes::MakeFrozenRegistry(Log, false)->ExportSchemaJson();
		const FString Reverse = FADSKTestNodes::MakeFrozenRegistry(Log, true)->ExportSchemaJson();
		ASSERT_THAT(IsFalse(Forward.IsEmpty(), TEXT("the schema is empty")));
		ASSERT_THAT(AreEqual(Forward, Reverse));
		ASSERT_THAT(IsFalse(Forward.Contains(TEXT("\n")), TEXT("the schema is not one line")));
	}

	TEST_METHOD(SchemaSortsCaseSensitivelyAndNullsUnboundedMax)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = Fresh();
		Registry->Register(Leaf(TEXT("beta")));
		Registry->Register(Leaf(TEXT("Zed")));
		Registry->Register(Leaf(TEXT("alpha")));
		FADSKNodeDescriptor Seq;
		Seq.Name = TEXT("Seq");
		Seq.Kind = EADSKNodeKind::Sequence;
		Seq.MinChildren = 1;
		Seq.MaxChildren = ADSK::UnboundedChildren;
		Registry->Register(Seq);
		Registry->Freeze();
		const FString Json = Registry->ExportSchemaJson();
		ASSERT_THAT(IsTrue(Json.Contains(TEXT("\"maxChildren\":null")), Json));
		const int32 Zed = Json.Find(TEXT("\"Zed\""), ESearchCase::CaseSensitive);
		const int32 Alpha = Json.Find(TEXT("\"alpha\""), ESearchCase::CaseSensitive);
		ASSERT_THAT(IsTrue(Zed != INDEX_NONE && Alpha != INDEX_NONE && Zed < Alpha, FString::Printf(TEXT("Zed at %d, alpha at %d"), Zed, Alpha)));
	}
};

#endif
