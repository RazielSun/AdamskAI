// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Catalogue/ADSKCatalogue.h"

#include "Runtime/ADSKNode.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ADSKCataloguePrivate
{
	FString Code(const FString& Value)
	{
		return Value.IsEmpty() ? FString(TEXT("(none)")) : FString::Printf(TEXT("`%s`"), *Value);
	}

	FString Children(const FJsonObject& Node)
	{
		const int32 Min = static_cast<int32>(Node.GetNumberField(TEXT("minChildren")));
		const TSharedPtr<FJsonValue> Max = Node.TryGetField(TEXT("maxChildren"));
		const FString MaxText = (!Max.IsValid() || Max->IsNull()) ? FString(TEXT("unbounded")) : FString::FromInt(static_cast<int32>(Max->AsNumber()));
		return FString::Printf(TEXT("%d..%s"), Min, *MaxText);
	}

	/** The first sentence of a description, for the index; the whole text when it has one sentence. */
	FString FirstSentence(const FString& Description)
	{
		const int32 End = Description.Find(TEXT(". "), ESearchCase::CaseSensitive);
		return End == INDEX_NONE ? Description : Description.Left(End + 1);
	}

	bool IsLegacy(const FADSKNodeDescriptor* Descriptor, const FString& FieldName)
	{
		const FADSKFieldMap* Field = Descriptor ? Descriptor->Fields.FindByPredicate([&FieldName](const FADSKFieldMap& Candidate) { return Candidate.XmlName == FieldName; }) : nullptr;
		return Field && Field->bLegacy;
	}

	void AppendFields(const FJsonObject& Node, const FADSKNodeDescriptor* Descriptor, FString& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
		if (!Node.TryGetArrayField(TEXT("fields"), Fields) || Fields->IsEmpty())
		{
			Out += TEXT("Fields: none\n\n");
			return;
		}
		Out += TEXT("| Field | Type | Default |\n|---|---|---|\n");
		for (const TSharedPtr<FJsonValue>& Value : *Fields)
		{
			const TSharedPtr<FJsonObject> Field = Value->AsObject();
			FString Type = Field->GetStringField(TEXT("type"));
			const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
			if (Field->TryGetArrayField(TEXT("values"), Choices))
			{
				Type += TEXT(": ") + FString::JoinBy(*Choices, TEXT(", "), [](const TSharedPtr<FJsonValue>& Choice) { return FString::Printf(TEXT("`%s`"), *Choice->AsString()); });
			}
			const FString Name = Field->GetStringField(TEXT("name"));
			if (IsLegacy(Descriptor, Name)) Type += TEXT(" (legacy: kept for older files, do not use)");
			Out += FString::Printf(TEXT("| `%s` | %s | %s |\n"), *Name, *Type, *Code(Field->GetStringField(TEXT("default"))));
		}
		Out += TEXT("\n");
	}

	void AppendParams(const FJsonObject& Node, FString& Out)
	{
		bool bParams = false;
		if (Node.TryGetBoolField(TEXT("params"), bParams) && bParams)
		{
			Out += TEXT("Params: any number of `<Param name=\"...\" value=\"...\"/>` children (distinct names; `type=\"Int\"` or `\"Bool\"` for such params, Float otherwise).\n\n");
		}
		bool bArgs = false;
		if (Node.TryGetBoolField(TEXT("args"), bArgs) && bArgs)
		{
			Out += TEXT("Params: any number of `<Param name=\"...\" from=\"Slot\"/>` children (distinct names; any Input, Local or tree parameter but an enum), read each time the node runs.\n\n");
		}
	}

	void AppendPorts(const FJsonObject& Node, FString& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Ports = nullptr;
		if (!Node.TryGetArrayField(TEXT("ports"), Ports) || Ports->IsEmpty())
		{
			Out += TEXT("Ports: none\n\n");
			return;
		}
		Out += TEXT("| Port | Type | Direction | XML |\n|---|---|---|---|\n");
		for (const TSharedPtr<FJsonValue>& Value : *Ports)
		{
			const TSharedPtr<FJsonObject> Port = Value->AsObject();
			const FString Name = Port->GetStringField(TEXT("name"));
			const FString Direction = Port->GetStringField(TEXT("direction"));
			Out += FString::Printf(TEXT("| `%s` | %s | %s | `%s-%s` |\n"), *Name, *Port->GetStringField(TEXT("type")), *Direction, *Direction, *Name);
		}
		Out += TEXT("\n");
	}

	void AppendAttributes(const FJsonObject& Node, FString& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Attributes = nullptr;
		if (!Node.TryGetArrayField(TEXT("attributes"), Attributes) || Attributes->IsEmpty()) return;
		Out += TEXT("| Intrinsic attribute | Required |\n|---|---|\n");
		for (const TSharedPtr<FJsonValue>& Value : *Attributes)
		{
			const TSharedPtr<FJsonObject> Attribute = Value->AsObject();
			Out += FString::Printf(TEXT("| `%s` | %s |\n"), *Attribute->GetStringField(TEXT("name")), Attribute->GetBoolField(TEXT("required")) ? TEXT("yes") : TEXT("no"));
		}
		Out += TEXT("\n");
	}

	FString Normalize(const FString& Text)
	{
		return Text.Replace(TEXT("\r\n"), TEXT("\n"), ESearchCase::CaseSensitive);
	}
}

bool FADSKCatalogue::Render(const FADSKNodeRegistry& Registry, const FString& RegistryLabel, FString& OutMarkdown, FString& OutError)
{
	OutMarkdown.Reset();
	if (!Registry.IsFrozen())
	{
		OutError = TEXT("registry is not frozen");
		return false;
	}
	TSharedPtr<FJsonObject> Schema;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Registry.ExportSchemaJson());
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (!FJsonSerializer::Deserialize(Reader, Schema) || !Schema.IsValid() || !Schema->TryGetArrayField(TEXT("nodes"), Nodes))
	{
		OutError = TEXT("catalogue schema does not parse");
		return false;
	}
	FString Out = TEXT("# AdamskAI node catalogue\n\nGenerated by `AdamskAIValidate -catalog`; do not edit by hand.\n\n");
	Out += FString::Printf(TEXT("- Registry: %s\n- Fingerprint: `%s`\n\n"), *RegistryLabel, *Registry.GetCatalogueFingerprint().ToString());
	Out += TEXT("| Node | Kind | What it does |\n|---|---|---|\n");
	for (const TSharedPtr<FJsonValue>& Value : *Nodes)
	{
		const TSharedPtr<FJsonObject> Node = Value->AsObject();
		const FString Name = Node->GetStringField(TEXT("name"));
		const FADSKNodeDescriptor* Descriptor = Registry.Find(Name);
		const FString Summary = Descriptor && !Descriptor->Description.IsEmpty() ? ADSKCataloguePrivate::FirstSentence(Descriptor->Description) : FString(TEXT("(no description)"));
		Out += FString::Printf(TEXT("| [`%s`](#%s) | %s | %s |\n"), *Name, *Name.ToLower(), *Node->GetStringField(TEXT("kind")), *Summary);
	}
	Out += TEXT("\n");
	for (const TSharedPtr<FJsonValue>& Value : *Nodes)
	{
		const TSharedPtr<FJsonObject> Node = Value->AsObject();
		const FString Name = Node->GetStringField(TEXT("name"));
		const FADSKNodeDescriptor* Descriptor = Registry.Find(Name);
		Out += FString::Printf(TEXT("## %s\n\n"), *Name);
		if (Descriptor && !Descriptor->Description.IsEmpty()) Out += Descriptor->Description + TEXT("\n\n");
		if (Descriptor && !Descriptor->Guidance.IsEmpty())
		{
			TArray<FString> Points;
			Descriptor->Guidance.ParseIntoArrayLines(Points);
			Out += TEXT("Use it well:\n\n") + FString::JoinBy(Points, TEXT(""), [](const FString& Point) { return TEXT("- ") + Point + TEXT("\n"); }) + TEXT("\n");
		}
		Out += FString::Printf(TEXT("Kind: %s. Children: %s. Schema version: %d."), *Node->GetStringField(TEXT("kind")),
			*ADSKCataloguePrivate::Children(*Node), static_cast<int32>(Node->GetNumberField(TEXT("version"))));
		if (Descriptor && Descriptor->bNeverFails) Out += TEXT(" Never fails.");
		if (Descriptor && Descriptor->bThreadSafe) Out += TEXT(" Thread-safe (may tick on a worker).");
		Out += TEXT("\n\n");
		ADSKCataloguePrivate::AppendFields(*Node, Descriptor, Out);
		ADSKCataloguePrivate::AppendParams(*Node, Out);
		ADSKCataloguePrivate::AppendPorts(*Node, Out);
		ADSKCataloguePrivate::AppendAttributes(*Node, Out);
	}
	OutMarkdown = Out.TrimEnd() + TEXT("\n");
	return true;
}

bool FADSKCatalogue::Matches(const FString& Generated, const FString& Existing)
{
	return ADSKCataloguePrivate::Normalize(Generated).Equals(ADSKCataloguePrivate::Normalize(Existing), ESearchCase::CaseSensitive);
}
