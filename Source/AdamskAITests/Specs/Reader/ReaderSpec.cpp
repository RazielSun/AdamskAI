// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Support/TestADSKNodes.h"
#include "Support/TestChecks.h"
#include "CQTest.h"

namespace FReaderSpecPrivate
{
	const TCHAR* Machine = TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\"/></StateMachine>");
	const TCHAR* MinimalAgent = TEXT("<Agent version=\"1\" id=\"a\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\"/></StateMachine></Agent>");

	FADSKCompileResult Load(const FString& Text)
	{
		const TSharedRef<FADSKTestLeafLog> Log = MakeShared<FADSKTestLeafLog>();
		return FADSKCompiler::Compile(TEXT("reader.xml"), Text, FADSKTestNodes::MakeFrozenRegistry(Log));
	}

	FADSKCheck IsMalformedAt(const FString& Text, int32 Line)
	{
		const FADSKCompileResult Result = Load(Text);
		if (Result.Diagnostics.Num() == 1 && Result.Diagnostics[0].Code == EADSKDiagnosticCode::XmlMalformed
			&& Result.Diagnostics[0].Line == Line && !Result.IsSuccess())
		{
			return FADSKCheck::Pass();
		}
		return FADSKCheck::Fail(FString::Printf(TEXT("expected exactly one xml-malformed at line %d, got:%s"), Line, *ADSKTest::FormatDiagnostics(Result)));
	}

	FADSKCheck IsClean(const FString& Text)
	{
		const FADSKCompileResult Result = Load(Text);
		return Result.Diagnostics.IsEmpty() ? FADSKCheck::Pass() : FADSKCheck::Fail(TEXT("expected no diagnostics, got:") + ADSKTest::FormatDiagnostics(Result));
	}

	FADSKCheck PassesSyntax(const FString& Text)
	{
		const FADSKCompileResult Result = Load(Text);
		if (Result.Count(EADSKDiagnosticCode::XmlMalformed) == 0 && Result.Count(EADSKDiagnosticCode::LimitExceeded) == 0)
		{
			return FADSKCheck::Pass();
		}
		return FADSKCheck::Fail(TEXT("expected no syntax or limit diagnostics, got:") + ADSKTest::FormatDiagnostics(Result));
	}

	FString PaddedToBytes(int32 TargetBytes)
	{
		FString Text = MinimalAgent;
		Text += TEXT("<!--");
		const int32 Overhead = FTCHARToUTF8(*Text).Length() + 3;
		const int32 Free = TargetBytes - Overhead;
		for (int32 Index = 0; Index < Free / 2; ++Index)
		{
			Text.AppendChar(TCHAR(0x00E9));
		}
		for (int32 Index = 0; Index < Free % 2; ++Index)
		{
			Text.AppendChar(TEXT('x'));
		}
		Text += TEXT("-->");
		return Text;
	}
}

using namespace FReaderSpecPrivate;

TEST_CLASS_WITH_FLAGS(FReaderAcceptedSpec, "AdamskAI.Reader.Accepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(DeclarationCommentsAndWhitespaceAreAccepted)
	{
		ADSK_ASSERT(IsClean(TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!-- hi -->\n<Agent version=\"1\" id=\"a\">\n\t<!-- x -->\r\n<StateMachine id=\"m\" initial=\"s\"><State id=\"s\"/></StateMachine></Agent>\n<!-- end -->\n")));
		ADSK_ASSERT(IsClean(MinimalAgent));
	}

	TEST_METHOD(SelfClosingNestedAndByteOrderMarkAreAccepted)
	{
		ADSK_ASSERT(IsClean(TEXT("<Agent version=\"1\" id=\"a\"><Inputs/><Locals></Locals><StateMachine id=\"m\" initial=\"s\"><State id=\"s\"/></StateMachine></Agent>")));
		ADSK_ASSERT(IsClean(FString(TEXT("\xFEFF")) + MinimalAgent));
	}

	TEST_METHOD(EntitiesInAttributesAreDecoded)
	{
		// &#49; decodes to "1", so the version is accepted only if decoding happened.
		ADSK_ASSERT(IsClean(FString(TEXT("<Agent version=\"&#49;\" id=\"&amp;&lt;&gt;&quot;&apos;&#x41;\">")) + Machine + TEXT("</Agent>")));
		const FADSKCompileResult Result = Load(TEXT("<Agent version=\"&amp;\" id=\"a\"/>"));
		ASSERT_THAT(AreEqual(1, Result.Count(EADSKDiagnosticCode::VersionUnsupported), ADSKTest::FormatDiagnostics(Result)));
	}

	TEST_METHOD(RootRulesUseTheirStableCodes)
	{
		ASSERT_THAT(AreEqual(1, Load(TEXT("<Other version=\"1\"/>")).Count(EADSKDiagnosticCode::UnknownElement)));
		ASSERT_THAT(AreEqual(1, Load(TEXT("<Agent id=\"a\"/>")).Count(EADSKDiagnosticCode::MissingRequired)));
		ASSERT_THAT(AreEqual(1, Load(TEXT("<Agent version=\"2\" id=\"a\"/>")).Count(EADSKDiagnosticCode::VersionUnsupported)));
		ASSERT_THAT(AreEqual(1, Load(TEXT("<Agent version=\"01\" id=\"a\"/>")).Count(EADSKDiagnosticCode::VersionUnsupported)));
	}
};

TEST_CLASS_WITH_FLAGS(FReaderRejectedSpec, "AdamskAI.Reader.Rejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ExpatRejectsMalformedStructure)
	{
		ADSK_ASSERT(IsMalformedAt(TEXT(""), 1));
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\"/>\n<Agent version=\"1\"/>"), 2));
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\"/>\nhello"), 2));
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\"\n version=\"1\"/>"), 2));
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=1/>"), 1));
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\" id=\"&foo;\"/>"), 1));
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\">\n</Agnt>"), 2));
	}

	TEST_METHOD(ExpatRejectsUnterminatedInput)
	{
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\">\n"), 2));
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\">\n<!-- x\n</Agent>"), 2));
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\n/>"), 1));
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\" id='a\"/>"), 1));
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\"><!-- a -- b --></Agent>"), 1));
	}

	TEST_METHOD(AdapterRejectsUnsupportedSyntax)
	{
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\">\n hello\n</Agent>"), 2));
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\">\n<![CDATA[x]]>\n</Agent>"), 2));
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\">\n<?pi x?>\n</Agent>"), 2));
		ADSK_ASSERT(IsMalformedAt(TEXT("<!DOCTYPE Agent>\n<Agent version=\"1\"/>"), 1));
		ADSK_ASSERT(IsMalformedAt(TEXT("<!DOCTYPE Agent [<!ENTITY x \"y\">]>\n<Agent version=\"1\" id=\"&x;\"/>"), 1));
		ADSK_ASSERT(IsMalformedAt(TEXT("<Agent version=\"1\">&amp;</Agent>"), 1));
	}
};

TEST_CLASS_WITH_FLAGS(FReaderLimitSpec, "AdamskAI.Reader.Limits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(SourceBytesAreCountedAsUtf8)
	{
		const FString AtLimit = PaddedToBytes(FADSKCompiler::MaxSourceBytes);
		ASSERT_THAT(AreEqual(FADSKCompiler::MaxSourceBytes, FTCHARToUTF8(*AtLimit).Length()));
		ASSERT_THAT(IsTrue(AtLimit.Len() < FADSKCompiler::MaxSourceBytes, FString::Printf(TEXT("%d characters"), AtLimit.Len())));
		ADSK_ASSERT(PassesSyntax(AtLimit));
		const FADSKCompileResult Over = Load(PaddedToBytes(FADSKCompiler::MaxSourceBytes + 1));
		ASSERT_THAT(IsTrue(Over.Diagnostics.Num() == 1 && Over.Diagnostics[0].Code == EADSKDiagnosticCode::LimitExceeded
			&& Over.Diagnostics[0].Line == 0, ADSKTest::FormatDiagnostics(Over)));
	}

	TEST_METHOD(ElementCountIsBoundedAtTheThousandAndTwentyFifth)
	{
		auto Build = [](int32 Children)
		{
			FString Text = TEXT("<Agent version=\"1\" id=\"a\">\n");
			for (int32 Index = 0; Index < Children; ++Index)
			{
				Text += TEXT("<A/>\n");
			}
			return Text + TEXT("</Agent>");
		};
		ADSK_ASSERT(PassesSyntax(Build(FADSKCompiler::MaxElements - 1)));
		const FADSKCompileResult Over = Load(Build(FADSKCompiler::MaxElements));
		ASSERT_THAT(IsTrue(Over.Diagnostics.Num() == 1 && Over.Diagnostics[0].Code == EADSKDiagnosticCode::LimitExceeded, ADSKTest::FormatDiagnostics(Over)));
		ASSERT_THAT(AreEqual(FADSKCompiler::MaxElements + 1, Over.Diagnostics[0].Line));
	}

	TEST_METHOD(UnfrozenRegistryIsACallerError)
	{
		TestRunner->AddExpectedMessagePlain(TEXT("Handled ensure"), ELogVerbosity::Error,
			EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/0);
		for (const TCHAR* Line : {TEXT("Ensure condition failed"), TEXT("requires a frozen registry"), TEXT("Stack:"), TEXT("[Callstack]")})
		{
			TestRunner->AddExpectedMessagePlain(Line, ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/0);
		}
		TestRunner->AddExpectedMessagePlain(TEXT(""), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Exact, /*Occurrences=*/0);
		TestRunner->AddExpectedMessagePlain(TEXT("Script Stack"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/0);
		const FADSKCompileResult Result = FADSKCompiler::Compile(TEXT("x"), TEXT("<Agent/>"), FADSKNodeRegistry::Create());
		ASSERT_THAT(IsTrue(Result.Diagnostics.Num() == 1 && Result.Diagnostics[0].Code == EADSKDiagnosticCode::MissingRequired, ADSKTest::FormatDiagnostics(Result)));
		ASSERT_THAT(AreEqual(0, Result.Diagnostics[0].Line));
		ASSERT_THAT(IsFalse(Result.IsSuccess(), TEXT("compiled against an unfrozen registry")));
	}
};

#endif
