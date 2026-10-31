// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;

namespace FSuggestionsSpecPrivate
{
	/** The message of the first diagnostic with Code, or the formatted diagnostics when there is none. */
	FString MessageOf(const FADSKCompileResult& Result, EADSKDiagnosticCode Code)
	{
		const FADSKDiagnostic* Found = Result.Diagnostics.FindByPredicate([Code](const FADSKDiagnostic& D) { return D.Code == Code; });
		return Found ? Found->Message : TEXT("no such diagnostic:") + ADSKTest::FormatDiagnostics(Result);
	}

	FADSKCheck Suggests(int32 Line, const char* Replacement, EADSKDiagnosticCode Code, const TCHAR* Expected)
	{
		const FString Message = MessageOf(Load(Mutate(Line, Replacement)), Code);
		return Message.Contains(Expected, ESearchCase::CaseSensitive) ? FADSKCheck::Pass()
			: FADSKCheck::Fail(FString::Printf(TEXT("[%hs]: expected \"%s\" in \"%s\""), Replacement, Expected, *Message));
	}
}

using namespace FSuggestionsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FSuggestionsSpec, "AdamskAI.Compiler.Suggestions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AnUnknownNameNamesItselfAndTheClosestMatch)
	{
		ADSK_ASSERT(Suggests(15, "<Wait id=\"w\" durration=\"1\"/>", EADSKDiagnosticCode::UnknownAttribute, TEXT("'durration'; did you mean 'duration'?")));
		ADSK_ASSERT(Suggests(15, "<wait id=\"w\" duration=\"1\"/>", EADSKDiagnosticCode::UnknownNode, TEXT("did you mean 'Wait'?")));
		ADSK_ASSERT(Suggests(15, "<Guard id=\"w\" in-condition=\"Flg\"><Wait id=\"x\" duration=\"1\"/></Guard>", EADSKDiagnosticCode::UnresolvedReference, TEXT("did you mean 'Flag'?")));
		ADSK_ASSERT(Suggests(15, "<Guard id=\"w\" in-conditon=\"Flag\"><Wait id=\"x\" duration=\"1\"/></Guard>", EADSKDiagnosticCode::UnknownPort, TEXT("did you mean 'condition'?")));
		ADSK_ASSERT(Suggests(12, "<State id=\"s\" tree=\"T\"/>", EADSKDiagnosticCode::UnresolvedReference, TEXT("did you mean 't'?")));
		ADSK_ASSERT(Suggests(12, "<State id=\"s\" tree=\"t\"><On event=\"Pin\" goto=\"s\"/></State>", EADSKDiagnosticCode::UnresolvedReference, TEXT("'Pin'; did you mean 'Ping'?")));
		ADSK_ASSERT(Suggests(12, "<State id=\"s\" tree=\"t\"><On event=\"Ping\" goto=\"S\"/></State>", EADSKDiagnosticCode::UnresolvedReference, TEXT("did you mean 's'?")));
		ADSK_ASSERT(Suggests(12, "<State id=\"s\" tree=\"t\" tag=\"x\"/>", EADSKDiagnosticCode::UnknownAttribute, TEXT("did you mean 'tags'?")));
	}

	TEST_METHOD(NoSuggestionWhenNothingIsClose)
	{
		const FString Message = MessageOf(Load(Mutate(15, "<Wait id=\"w\" color=\"1\"/>")), EADSKDiagnosticCode::UnknownAttribute);
		ASSERT_THAT(IsTrue(Message.Contains(TEXT("'color'")) && !Message.Contains(TEXT("did you mean")), Message));
	}
};

#endif
