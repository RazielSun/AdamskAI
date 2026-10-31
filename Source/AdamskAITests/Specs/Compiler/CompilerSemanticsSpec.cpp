// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

TEST_CLASS_WITH_FLAGS(FCompilerNodeSpec, "AdamskAI.Compiler.Nodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(NodeNamesAreExactCase)
	{
		ADSK_ASSERT(Rejects(15, "<Bogus id=\"w\"/>", Code::UnknownNode, 15));
		ADSK_ASSERT(Rejects(15, "<wait id=\"w\" duration=\"1\"/>", Code::UnknownNode, 15));
		ADSK_ASSERT(Rejects(15, "<MOVETO id=\"w\" in-goal=\"In\"/>", Code::UnknownNode, 15));
		ADSK_ASSERT(Rejects(15, "<State id=\"w\"/>", Code::UnknownNode, 15));
		ADSK_ASSERT(Accepts(15, "<MoveTo id=\"w\" in-goal=\"In\"/>"));
	}

	TEST_METHOD(FieldAndPortNamesAreExactCase)
	{
		ADSK_ASSERT(Rejects(15, "<MoveTo id=\"w\" in-goal=\"In\" acceptanceradius=\"5\"/>", Code::UnknownAttribute, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<MoveTo id=\"w\" in-Goal=\"In\"/>", Code::UnknownPort, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<MoveTo id=\"w\" in-goal=\"In\" out-goal=\"L\"/>", Code::TypeMismatch, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Wait id=\"w\" Duration=\"1\"/>", Code::UnknownAttribute, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Wait ID=\"w\" duration=\"1\"/>", Code::MissingRequired, 15));
		ADSK_ASSERT(Rejects(15, "<MoveTo id=\"w\" in-goal=\"in\"/>", Code::UnresolvedReference, 15, "w"));
	}

	TEST_METHOD(IdsAreRequiredAndUniqueIgnoringCase)
	{
		ADSK_ASSERT(Rejects(15, "<Wait duration=\"1\"/>", Code::MissingRequired, 15));
		ADSK_ASSERT(Rejects(15, "<Wait id=\"w\" duration=\"1\" extra=\"2\"/>", Code::UnknownAttribute, 15, "w"));
		ADSK_ASSERT(Rejects(16, "</Tree><Tree id=\"t2\"><Wait id=\"w\" duration=\"1\"/></Tree>", Code::DuplicateId, 16, "w"));
		ADSK_ASSERT(Rejects(15, "<Sequence id=\"w\"><Wait id=\"W\" duration=\"1\"/></Sequence>", Code::DuplicateId, 15, "W"));
	}

	TEST_METHOD(ChildCountFollowsTheDescriptor)
	{
		ADSK_ASSERT(Rejects(15, "<Wait id=\"w\" duration=\"1\"><Wait id=\"x\" duration=\"1\"/></Wait>", Code::BadArity, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Sequence id=\"w\"/>", Code::BadArity, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Guard id=\"w\" in-condition=\"Flag\"><Wait id=\"a\" duration=\"1\"/><Wait id=\"b\" duration=\"1\"/></Guard>", Code::BadArity, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Guard id=\"w\" in-condition=\"Flag\"/>", Code::BadArity, 15, "w"));
		ADSK_ASSERT(Accepts(15, "<Guard id=\"w\" in-condition=\"Flag\"><Wait id=\"a\" duration=\"1\"/></Guard>"));
	}
};

TEST_CLASS_WITH_FLAGS(FCompilerPortSpec, "AdamskAI.Compiler.Ports",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(InAndOutBindingRules)
	{
		ADSK_ASSERT(Rejects(15, "<MoveTo id=\"w\" in-goal=\"Nope\"/>", Code::UnresolvedReference, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<MoveTo id=\"w\" in-goal=\"Flag\"/>", Code::TypeMismatch, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Sense id=\"w\" in-point=\"L\"/>", Code::TypeMismatch, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Sense id=\"w\" out-point=\"In\"/>", Code::TypeMismatch, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Sense id=\"w\" out-point=\"LB\"/>", Code::TypeMismatch, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Sense id=\"w\" out-nothing=\"L\"/>", Code::UnknownPort, 15, "w"));
		ADSK_ASSERT(Accepts(15, "<Sense id=\"w\" out-point=\"L\"/>"));
	}

	TEST_METHOD(EveryDeclaredPortMustBeBound)
	{
		ADSK_ASSERT(Rejects(15, "<MoveTo id=\"w\"/>", Code::MissingRequired, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Sense id=\"w\"/>", Code::MissingRequired, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Guard id=\"w\"><Wait id=\"a\" duration=\"1\"/></Guard>", Code::MissingRequired, 15, "w"));
		ADSK_ASSERT(Accepts(15, "<LookAtPoint id=\"w\" in-target=\"L\"/>"));
	}
};

TEST_CLASS_WITH_FLAGS(FCompilerConfigSpec, "AdamskAI.Compiler.Config",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(FloatFieldsAreLexicallyGated)
	{
		ADSK_ASSERT(Rejects(15, "<Wait id=\"w\" duration=\"abc\"/>", Code::TypeMismatch, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Wait id=\"w\" duration=\"1e3\"/>", Code::TypeMismatch, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Wait id=\"w\" duration=\" 1\"/>", Code::TypeMismatch, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Wait id=\"w\" duration=\"+1\"/>", Code::TypeMismatch, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Wait id=\"w\" duration=\"nan\"/>", Code::TypeMismatch, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<Wait id=\"w\" duration=\"\"/>", Code::TypeMismatch, 15, "w"));
	}

	TEST_METHOD(ValidatorRunsAfterConversion)
	{
		ADSK_ASSERT(Rejects(15, "<Wait id=\"w\" duration=\"-1\"/>", Code::OutOfRange, 15, "w"));
		ADSK_ASSERT(Rejects(15, "<MoveTo id=\"w\" in-goal=\"In\" acceptanceRadius=\"-5\"/>", Code::OutOfRange, 15, "w"));
		const FADSKCompileResult Both = Load(Mutate(15, "<Wait id=\"w\" duration=\"x\"/>"));
		ASSERT_THAT(IsFalse(Both.Count(Code::OutOfRange) > 0, ADSKTest::FormatDiagnostics(Both)));
	}

	TEST_METHOD(BoolAndVectorFieldsConvertStrictly)
	{
		const FString Ok = Mutate(15, "<Probe id=\"w\" flag=\"true\" scale=\"-2.5\" offset=\"1,2,3\"/>");
		const FADSKCompileResult Converted = LoadProbe(Ok);
		ASSERT_THAT(IsTrue(Converted.IsSuccess() && Converted.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Converted)));
		for (const char* Bad : {"<Probe id=\"w\" flag=\"True\"/>", "<Probe id=\"w\" flag=\"1\"/>", "<Probe id=\"w\" scale=\"+1\"/>",
				 "<Probe id=\"w\" offset=\"1,2\"/>", "<Probe id=\"w\" offset=\"1, 2,3\"/>", "<Probe id=\"w\" offset=\"1,2,3,4\"/>"})
		{
			const FADSKCompileResult Result = LoadProbe(Mutate(15, Bad));
			ADSK_ASSERT(Has(Result, Code::TypeMismatch, EADSKSeverity::Error, 15, "w"));
		ASSERT_THAT(IsFalse(Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
		}
	}
};

#endif
