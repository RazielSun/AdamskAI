// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "Nodes/ADSKBuiltinNodes.h"
#include "Catalogue/ADSKCatalogue.h"
#include "Catalogue/ADSKSchemaXsd.h"
#include "Nodes/ADSKHostNodes.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "CQTest.h"
#include "Support/TestChecks.h"
#include "Support/TempTree.h"

namespace FCatalogueSpecPrivate
{
	TSharedRef<const FADSKNodeRegistry> PluginRegistry()
	{
		return ADSKTest::PluginCatalogue();
	}

	FString Render(const FADSKNodeRegistry& Registry, const TCHAR* Label = TEXT("plugin"))
	{
		FString Markdown, Error;
		ADSKTest::Expect(FADSKCatalogue::Render(Registry, Label, Markdown, Error), TEXT("catalogue render failed: ") + Error);
		return Markdown;
	}

	FString RenderXsd(const FADSKNodeRegistry& Registry)
	{
		FString Xsd, Error;
		ADSKTest::Expect(FADSKSchemaXsd::Render(Registry, TEXT("plugin"), Xsd, Error), TEXT("schema render failed: ") + Error);
		return Xsd;
	}
}

using namespace FCatalogueSpecPrivate;

TEST_CLASS_WITH_FLAGS(FCatalogueSpec, "AdamskAI.Catalogue.Render",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(RenderingIgnoresRegistrationOrder)
	{
		const TSharedRef<FADSKNodeRegistry> Forward = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Forward);
		FADSKHostNodes::RegisterAll(*Forward);
		Forward->Freeze();
		const TSharedRef<FADSKNodeRegistry> Reverse = FADSKNodeRegistry::Create();
		FADSKHostNodes::RegisterAll(*Reverse);
		FADSKBuiltinNodes::RegisterAll(*Reverse);
		Reverse->Freeze();
		ASSERT_THAT(AreEqual(Render(*Forward), Render(*Reverse)));
		TArray<FString> Errors;
		const TSharedPtr<const FADSKNodeRegistry> Assembled = FADSKRegistryAssembly::AssembleIsolated({}, Errors);
		ASSERT_THAT(IsTrue(Assembled.IsValid(), FString::Join(Errors, TEXT("; "))));
		ASSERT_THAT(AreEqual(Render(*Forward), Render(*Assembled)));
	}

	TEST_METHOD(ThePluginCatalogueHoldsTheActionAndGroupNodes)
	{
		const TSharedRef<const FADSKNodeRegistry> Registry = PluginRegistry();
		for (const TCHAR* Node : {TEXT("Act"), TEXT("HoldToken"), TEXT("Radio"), TEXT("Distance"), TEXT("GroupTarget")})
		{
			ASSERT_THAT(IsNotNull(Registry->Find(Node), FString::Printf(TEXT("%s is not in the plugin catalogue"), Node)));
		}
	}

	TEST_METHOD(OutputIsSortedLfWithOneTrailingNewlineAndTheFingerprint)
	{
		const TSharedRef<const FADSKNodeRegistry> Registry = PluginRegistry();
		const FString Markdown = Render(*Registry);
		ASSERT_THAT(IsFalse(Markdown.Contains(TEXT("\r")), TEXT("the catalogue has CR line endings")));
		ASSERT_THAT(IsTrue(Markdown.EndsWith(TEXT("\n")) && !Markdown.EndsWith(TEXT("\n\n")), TEXT("the catalogue must end with exactly one LF")));
		ASSERT_THAT(IsTrue(Markdown.Contains(TEXT("- Registry: plugin\n")), Markdown));
		ASSERT_THAT(IsTrue(Markdown.Contains(Registry->GetCatalogueFingerprint().ToString()), TEXT("no fingerprint ") + Registry->GetCatalogueFingerprint().ToString()));
		int32 Previous = INDEX_NONE;
		for (const TCHAR* Node : {TEXT("## Emit\n"), TEXT("## Guard\n"), TEXT("## LookAt\n"), TEXT("## MoveTo\n"), TEXT("## Selector\n"), TEXT("## Sequence\n"), TEXT("## Wait\n")})
		{
			const int32 At = Markdown.Find(Node, ESearchCase::CaseSensitive);
			ASSERT_THAT(IsTrue(At > Previous, FString::Printf(TEXT("[%s] at %d is not after the previous node at %d"), *FString(Node).TrimEnd(), At, Previous)));
			Previous = At;
		}
		ASSERT_THAT(IsTrue(Render(*Registry, TEXT("full")).Contains(TEXT("- Registry: full\n")), TEXT("the registry label is not printed")));
	}

	TEST_METHOD(CanonicalDefaultsArePrinted)
	{
		const FString Markdown = Render(*PluginRegistry());
		ASSERT_THAT(IsTrue(Markdown.Contains(TEXT("| `duration` | Float | `1.0` |")), Markdown));
		ASSERT_THAT(IsTrue(Markdown.Contains(TEXT("| `acceptanceRadius` | Float | `50.0` |")), Markdown));
		ASSERT_THAT(IsTrue(Markdown.Contains(TEXT("| `condition` | Bool | in | `in-condition` |")), Markdown));
		ASSERT_THAT(IsTrue(Markdown.Contains(TEXT("## Guard\n")), Markdown));
		ASSERT_THAT(IsTrue(Markdown.Contains(TEXT("| `is` | no |")), Markdown));
		ASSERT_THAT(IsTrue(Markdown.Contains(TEXT("## Emit\n")), Markdown));
		ASSERT_THAT(IsTrue(Markdown.Contains(TEXT("| `event` | yes |")), Markdown));
	}

	TEST_METHOD(GuidanceIsAListUnderTheDescriptionAndOutsideTheFingerprint)
	{
		const TSharedRef<const FADSKNodeRegistry> Registry = PluginRegistry();
		const FString Markdown = Render(*Registry);
		ASSERT_THAT(IsTrue(Markdown.Contains(TEXT("re-checked every tick. Fails, cancelling the child, when the condition stops passing; otherwise ends with the child.\n\nUse it well:\n\n- Keep a child running")), Markdown));
		for (const FADSKNodeDescriptor* Descriptor : Registry->GetDescriptors())
		{
			ASSERT_THAT(IsFalse(Descriptor->Guidance.IsEmpty(), FString::Printf(TEXT("%s has no guidance"), *Descriptor->Name)));
		}
		ASSERT_THAT(IsFalse(Registry->ExportSchemaJson().Contains(TEXT("Use it well")) || Registry->ExportSchemaJson().Contains(TEXT("Keep a child running")), TEXT("guidance leaked into the schema")));
	}

	TEST_METHOD(MatchNormalizesCrLfOnly)
	{
		const FString Markdown = Render(*PluginRegistry());
		ASSERT_THAT(IsTrue(FADSKCatalogue::Matches(Markdown, Markdown.Replace(TEXT("\n"), TEXT("\r\n"))), TEXT("CRLF line endings must still match")));
		ASSERT_THAT(IsFalse(FADSKCatalogue::Matches(Markdown, Markdown + TEXT("\n")), TEXT("an extra trailing newline matched")));
		ASSERT_THAT(IsFalse(FADSKCatalogue::Matches(Markdown, Markdown.Replace(TEXT("`1.0`"), TEXT("`2.0`"))), TEXT("a changed default matched")));
		ASSERT_THAT(IsFalse(FADSKCatalogue::Matches(Markdown, Markdown.TrimEnd()), TEXT("a missing trailing newline matched")));
	}

	TEST_METHOD(UnfrozenRegistryIsRefused)
	{
		const TSharedRef<FADSKNodeRegistry> Open = FADSKNodeRegistry::Create();
		FString Markdown, Error;
		ASSERT_THAT(IsFalse(FADSKCatalogue::Render(*Open, TEXT("plugin"), Markdown, Error), TEXT("an unfrozen registry rendered")));
		ASSERT_THAT(IsTrue(Markdown.IsEmpty(), Markdown));
		ASSERT_THAT(IsFalse(Error.IsEmpty(), TEXT("the refusal has no error")));
	}

	TEST_METHOD(CommittedPluginCatalogueIsCurrent)
	{
		FString Committed;
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToString(Committed, *(FADSKTestPaths::PluginBase() / TEXT("docs/node-catalog.md"))), TEXT("cannot read docs/node-catalog.md")));
		ASSERT_THAT(IsTrue(FADSKCatalogue::Matches(Render(*PluginRegistry()), Committed), TEXT("docs/node-catalog.md does not match the rendered plugin catalogue")));
	}

	TEST_METHOD(SchemaDeclaresEveryNodeWithItsFieldsPortsAndChildren)
	{
		const TSharedRef<const FADSKNodeRegistry> Registry = PluginRegistry();
		const FString Xsd = RenderXsd(*Registry);
		ASSERT_THAT(IsFalse(Xsd.Contains(TEXT("\r")), TEXT("the schema has CR line endings")));
		ASSERT_THAT(IsTrue(Xsd.EndsWith(TEXT("</xs:schema>\n")), TEXT("the schema must end with </xs:schema> and one LF")));
		ASSERT_THAT(IsTrue(Xsd.Contains(Registry->GetCatalogueFingerprint().ToString()), TEXT("no fingerprint")));
		for (const FADSKNodeDescriptor* Node : Registry->GetDescriptors())
		{
			ASSERT_THAT(IsTrue(Xsd.Contains(FString::Printf(TEXT("<xs:element name=\"%s\">"), *Node->Name)) && Xsd.Contains(FString::Printf(TEXT("<xs:element ref=\"%s\"/>"), *Node->Name)),
				Node->Name + TEXT(" is not declared or not in the Node group")));
		}
		for (const TCHAR* Expected : {
				 TEXT("<xs:attribute name=\"acceptanceRadius\" type=\"Float\">"),
				 TEXT("<xs:attribute name=\"in-goal\" type=\"xs:string\" use=\"required\">"),
				 TEXT("<xs:attribute name=\"out-point\" type=\"xs:string\" use=\"required\">"),
				 TEXT("<xs:enumeration value=\"RandomBest25\"/>"),
				 TEXT("<xs:element name=\"Param\" type=\"ValueParam\" minOccurs=\"0\" maxOccurs=\"unbounded\"/>"),
				 TEXT("<xs:element name=\"Param\" type=\"EmitParam\" minOccurs=\"0\" maxOccurs=\"unbounded\"/>"),
				 TEXT("<xs:group ref=\"Node\" minOccurs=\"1\" maxOccurs=\"unbounded\"/>"),
				 TEXT("<xs:group ref=\"Node\" minOccurs=\"2\" maxOccurs=\"2\"/>"),
				 TEXT("<xs:attribute name=\"event\" type=\"xs:string\" use=\"required\"/>"),
				 TEXT("<xs:attribute name=\"is\" type=\"xs:string\"/>"),
				 TEXT("<xs:element ref=\"Run\"/>")})
		{
			ASSERT_THAT(IsTrue(Xsd.Contains(Expected), FString(TEXT("the schema lacks ")) + Expected));
		}
		ASSERT_THAT(AreEqual(RenderXsd(*Registry), Xsd));
	}

	TEST_METHOD(SchemaRefusesAnUnfrozenRegistry)
	{
		const TSharedRef<FADSKNodeRegistry> Open = FADSKNodeRegistry::Create();
		FString Xsd, Error;
		ASSERT_THAT(IsFalse(FADSKSchemaXsd::Render(*Open, TEXT("plugin"), Xsd, Error), TEXT("an unfrozen registry rendered")));
		ASSERT_THAT(IsTrue(Xsd.IsEmpty() && !Error.IsEmpty(), Xsd));
	}

	TEST_METHOD(CommittedPluginSchemaIsCurrent)
	{
		FString Committed;
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToString(Committed, *(FADSKTestPaths::PluginBase() / TEXT("docs/adamskai.xsd"))), TEXT("cannot read docs/adamskai.xsd")));
		ASSERT_THAT(IsTrue(FADSKCatalogue::Matches(RenderXsd(*PluginRegistry()), Committed), TEXT("docs/adamskai.xsd does not match the rendered plugin schema; regenerate with -xsd")));
	}
};

#endif
