// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;

TEST_CLASS_WITH_FLAGS(FIdentitySpec, "AdamskAI.Identity.Definition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(FrozenCatalogueAndDefinitionIdentityAreStable)
	{
		const TSharedRef<const FADSKNodeRegistry> First = MakeProbeRegistry();
		const TSharedRef<const FADSKNodeRegistry> Second = MakeProbeRegistry();
		ASSERT_THAT(AreEqual(First->GetCatalogueFingerprint(), Second->GetCatalogueFingerprint()));
		const FADSKCompileResult A = FADSKCompiler::Compile(TEXT("identity.xml"), FString::Join(BaseLines(), TEXT("\n")), First);
		const FADSKCompileResult B = FADSKCompiler::Compile(TEXT("identity.xml"), FString::Join(BaseLines(), TEXT("\n")), Second);
		ASSERT_THAT(IsTrue(A.Definition.IsValid(), ADSKTest::FormatDiagnostics(A)));
		ASSERT_THAT(IsTrue(B.Definition.IsValid(), ADSKTest::FormatDiagnostics(B)));
		ASSERT_THAT(AreEqual(First->GetCatalogueFingerprint(), A.Definition->GetCatalogueFingerprint()));
		ASSERT_THAT(AreEqual(A.Definition->GetCompositeHash(), B.Definition->GetCompositeHash()));
		ASSERT_THAT(AreEqual(A.Definition->GetCompositeHashHex().ToLower(), A.Definition->GetCompositeHashHex()));
	}
};

#endif
