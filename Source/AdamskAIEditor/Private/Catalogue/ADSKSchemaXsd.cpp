// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Catalogue/ADSKSchemaXsd.h"

#include "Runtime/ADSKNode.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ADSKSchemaXsdPrivate
{
	FString Escape(const FString& Text)
	{
		return Text.Replace(TEXT("&"), TEXT("&amp;")).Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;")).Replace(TEXT("\""), TEXT("&quot;"));
	}

	FString Doc(const FString& Text, const TCHAR* Indent)
	{
		return Text.IsEmpty() ? FString() : FString::Printf(TEXT("%s<xs:annotation><xs:documentation>%s</xs:documentation></xs:annotation>\n"), Indent, *Escape(Text));
	}

	/** Literal types the compiler parses strictly; the rest (paths, names, tags, slot names) are any text. */
	FString LiteralType(const FString& Type)
	{
		for (const TCHAR* Known : {TEXT("Bool"), TEXT("Float"), TEXT("Int"), TEXT("Vector")})
		{
			if (Type.Equals(Known, ESearchCase::CaseSensitive)) return Type;
		}
		return TEXT("xs:string");
	}

	/** Types, sections, states and the subtree call: everything that does not come from the registry. */
	const TCHAR* Language = TEXT(R"(  <xs:simpleType name="Bool">
    <xs:restriction base="xs:string"><xs:enumeration value="true"/><xs:enumeration value="false"/></xs:restriction>
  </xs:simpleType>
  <xs:simpleType name="Int">
    <xs:restriction base="xs:string"><xs:pattern value="-?[0-9]+"/></xs:restriction>
  </xs:simpleType>
  <xs:simpleType name="Float">
    <xs:restriction base="xs:string"><xs:pattern value="-?[0-9]+(\.[0-9]+)?"/></xs:restriction>
  </xs:simpleType>
  <xs:simpleType name="Vector">
    <xs:restriction base="xs:string"><xs:pattern value="-?[0-9]+(\.[0-9]+)?,-?[0-9]+(\.[0-9]+)?,-?[0-9]+(\.[0-9]+)?"/></xs:restriction>
  </xs:simpleType>
  <xs:simpleType name="Identifier">
    <xs:restriction base="xs:string"><xs:pattern value="[A-Za-z][A-Za-z0-9\-]*"/></xs:restriction>
  </xs:simpleType>
  <xs:simpleType name="BuiltinType">
    <xs:restriction base="xs:string">
      <xs:enumeration value="Bool"/><xs:enumeration value="Float"/><xs:enumeration value="Int"/><xs:enumeration value="Vector"/><xs:enumeration value="Object"/>
    </xs:restriction>
  </xs:simpleType>
  <xs:simpleType name="SlotType">
    <xs:annotation><xs:documentation>Bool, Float, Int, Vector, Object or an enum declared in Enums.</xs:documentation></xs:annotation>
    <xs:union memberTypes="BuiltinType Identifier"/>
  </xs:simpleType>
  <xs:simpleType name="ParamType">
    <xs:restriction base="xs:string"><xs:enumeration value="Float"/><xs:enumeration value="Int"/><xs:enumeration value="Bool"/></xs:restriction>
  </xs:simpleType>
  <xs:simpleType name="Select">
    <xs:restriction base="xs:string"><xs:enumeration value="best"/><xs:enumeration value="weighted"/></xs:restriction>
  </xs:simpleType>
  <xs:simpleType name="History">
    <xs:restriction base="xs:string"><xs:enumeration value="shallow"/><xs:enumeration value="deep"/></xs:restriction>
  </xs:simpleType>

)") TEXT(R"(  <xs:element name="Agent">
    <xs:annotation><xs:documentation>One behavior file: optional Enums, Inputs, Locals, Sensors and Events, one StateMachine, then its Trees.</xs:documentation></xs:annotation>
    <xs:complexType>
      <xs:sequence>
        <xs:element name="Enums" minOccurs="0">
          <xs:complexType>
            <xs:sequence>
              <xs:element name="Enum" maxOccurs="16">
                <xs:complexType>
                  <xs:sequence>
                    <xs:element name="Value" maxOccurs="32">
                      <xs:complexType><xs:attribute name="name" type="Identifier" use="required"/></xs:complexType>
                    </xs:element>
                  </xs:sequence>
                  <xs:attribute name="name" type="Identifier" use="required"/>
                </xs:complexType>
              </xs:element>
            </xs:sequence>
          </xs:complexType>
        </xs:element>
        <xs:element name="Inputs" minOccurs="0">
          <xs:complexType>
            <xs:sequence>
              <xs:element name="Input" minOccurs="0" maxOccurs="unbounded">
                <xs:annotation><xs:documentation>Written by the host. Needs required="true" or a default, never both; an enum Input may have neither.</xs:documentation></xs:annotation>
                <xs:complexType>
                  <xs:attribute name="name" type="xs:string" use="required"/>
                  <xs:attribute name="type" type="SlotType" use="required"/>
                  <xs:attribute name="required" type="Bool"/>
                  <xs:attribute name="default" type="xs:string"/>
                </xs:complexType>
              </xs:element>
            </xs:sequence>
          </xs:complexType>
        </xs:element>
        <xs:element name="Locals" minOccurs="0">
          <xs:complexType>
            <xs:sequence>
              <xs:element name="Local" minOccurs="0" maxOccurs="unbounded">
                <xs:annotation><xs:documentation>Written by out- ports and Bind; zero at every start.</xs:documentation></xs:annotation>
                <xs:complexType>
                  <xs:attribute name="name" type="xs:string" use="required"/>
                  <xs:attribute name="type" type="SlotType" use="required"/>
                </xs:complexType>
              </xs:element>
            </xs:sequence>
          </xs:complexType>
        </xs:element>
@SENSORS@        <xs:element name="Events" minOccurs="0">
          <xs:complexType>
            <xs:sequence>
              <xs:element name="Event" minOccurs="0" maxOccurs="unbounded">
                <xs:annotation><xs:documentation>priority: higher dispatches first (default 0). change="Slot": raised by the core when that Input or Local changes; it takes no Param.</xs:documentation></xs:annotation>
                <xs:complexType>
                  <xs:sequence>
                    <xs:element name="Param" minOccurs="0" maxOccurs="unbounded">
                      <xs:complexType>
                        <xs:attribute name="name" type="xs:string" use="required"/>
                        <xs:attribute name="type" type="SlotType" use="required"/>
                      </xs:complexType>
                    </xs:element>
                  </xs:sequence>
                  <xs:attribute name="name" type="xs:string" use="required"/>
                  <xs:attribute name="priority" type="Int"/>
                  <xs:attribute name="change" type="xs:string"/>
                </xs:complexType>
              </xs:element>
            </xs:sequence>
          </xs:complexType>
        </xs:element>
        <xs:element name="StateMachine">
          <xs:annotation><xs:documentation>The root state; initial names a direct child.</xs:documentation></xs:annotation>
          <xs:complexType>
            <xs:choice minOccurs="0" maxOccurs="unbounded">
              <xs:element name="State" type="State"/>
              <xs:element name="On" type="On"/>
            </xs:choice>
            <xs:attribute name="id" type="xs:string" use="required"/>
            <xs:attribute name="initial" type="xs:string" use="required"/>
            <xs:attribute name="select" type="Select"/>
          </xs:complexType>
        </xs:element>
        <xs:element name="Tree" minOccurs="0" maxOccurs="unbounded">
          <xs:annotation><xs:documentation>Its parameters (reachable only through Run), then exactly one root node.</xs:documentation></xs:annotation>
          <xs:complexType>
            <xs:sequence>
              <xs:element name="Param" minOccurs="0" maxOccurs="unbounded">
                <xs:complexType>
                  <xs:attribute name="name" type="xs:string" use="required"/>
                  <xs:attribute name="type" type="SlotType" use="required"/>
                </xs:complexType>
              </xs:element>
              <xs:group ref="Node"/>
            </xs:sequence>
            <xs:attribute name="id" type="xs:string" use="required"/>
          </xs:complexType>
        </xs:element>
      </xs:sequence>
      <xs:attribute name="version" use="required">
        <xs:simpleType><xs:restriction base="xs:string"><xs:enumeration value="1"/></xs:restriction></xs:simpleType>
      </xs:attribute>
      <xs:attribute name="id" type="xs:string" use="required"/>
    </xs:complexType>
  </xs:element>

)") TEXT(R"(  <xs:complexType name="State">
    <xs:annotation><xs:documentation>A leaf may name a tree; a state with child states names its initial child instead. tags: comma-separated dotted names. select (states with child states): enter the child scoring highest (best) or one drawn by score (weighted), initial when none scores above 0. score, weight, bias (children of a select state): the score weight * score + bias, score a Float or Int slot that reads 1 when left out. history (states with child states): entered again after being left, resume the child it left (shallow) or the whole path below it (deep).</xs:documentation></xs:annotation>
    <xs:choice minOccurs="0" maxOccurs="unbounded">
      <xs:element name="State" type="State"/>
      <xs:element name="On" type="On"/>
    </xs:choice>
    <xs:attribute name="id" type="xs:string" use="required"/>
    <xs:attribute name="initial" type="xs:string"/>
    <xs:attribute name="tree" type="xs:string"/>
    <xs:attribute name="tags" type="xs:string"/>
    <xs:attribute name="select" type="Select"/>
    <xs:attribute name="history" type="History"/>
    <xs:attribute name="score" type="xs:string"/>
    <xs:attribute name="weight" type="Float"/>
    <xs:attribute name="bias" type="Float"/>
  </xs:complexType>

  <xs:complexType name="When">
    <xs:annotation><xs:documentation>One more condition that must hold: if/unless a Bool slot, or a slot compared with one of is, below, above.</xs:documentation></xs:annotation>
    <xs:attribute name="if" type="xs:string"/>
    <xs:attribute name="unless" type="xs:string"/>
    <xs:attribute name="is" type="xs:string"/>
    <xs:attribute name="below" type="xs:string"/>
    <xs:attribute name="above" type="xs:string"/>
  </xs:complexType>
  <xs:complexType name="Any">
    <xs:annotation><xs:documentation>Holds when at least one of its When children holds.</xs:documentation></xs:annotation>
    <xs:sequence>
      <xs:element name="When" type="When" minOccurs="2" maxOccurs="unbounded"/>
    </xs:sequence>
  </xs:complexType>

  <xs:complexType name="On">
    <xs:annotation><xs:documentation>Handles a declared event, tree.success or tree.failure. No goto: internal (binds only). if/unless a Bool slot, or a slot compared with one of is, below, above; every When and Any child must hold too. chance: taken with this probability (a number, or a Float slot read when the handler is tried) once the condition passes, otherwise the next handler is tried.</xs:documentation></xs:annotation>
    <xs:choice minOccurs="0" maxOccurs="unbounded">
      <xs:element name="Bind">
        <xs:annotation><xs:documentation>Copies the event parameter into a Local of the same type.</xs:documentation></xs:annotation>
        <xs:complexType>
          <xs:attribute name="param" type="xs:string" use="required"/>
          <xs:attribute name="to" type="xs:string" use="required"/>
        </xs:complexType>
      </xs:element>
      <xs:element name="When" type="When"/>
      <xs:element name="Any" type="Any"/>
    </xs:choice>
    <xs:attribute name="event" type="xs:string" use="required"/>
    <xs:attribute name="goto" type="xs:string"/>
    <xs:attribute name="if" type="xs:string"/>
    <xs:attribute name="unless" type="xs:string"/>
    <xs:attribute name="is" type="xs:string"/>
    <xs:attribute name="below" type="xs:string"/>
    <xs:attribute name="above" type="xs:string"/>
    <xs:attribute name="chance" type="xs:string"/>
  </xs:complexType>

  <xs:complexType name="ValueParam">
    <xs:attribute name="name" type="xs:string" use="required"/>
    <xs:attribute name="value" type="xs:string" use="required"/>
    <xs:attribute name="type" type="ParamType"/>
  </xs:complexType>
  <xs:complexType name="EmitParam">
    <xs:annotation><xs:documentation>One event parameter: a literal value, or from a slot read when the Emit runs.</xs:documentation></xs:annotation>
    <xs:attribute name="name" type="xs:string" use="required"/>
    <xs:attribute name="value" type="xs:string"/>
    <xs:attribute name="from" type="xs:string"/>
  </xs:complexType>
  <xs:complexType name="RunParam">
    <xs:attribute name="name" type="xs:string" use="required"/>
    <xs:attribute name="from" type="xs:string" use="required"/>
  </xs:complexType>

  <xs:element name="Run">
    <xs:annotation><xs:documentation>Replaced at load time by a copy of the tree's body, ids prefixed with this id and /; Param binds each tree parameter to a slot.</xs:documentation></xs:annotation>
    <xs:complexType>
      <xs:sequence>
        <xs:element name="Param" type="RunParam" minOccurs="0" maxOccurs="unbounded"/>
      </xs:sequence>
      <xs:attribute name="id" type="xs:string" use="required"/>
      <xs:attribute name="tree" type="xs:string" use="required"/>
    </xs:complexType>
  </xs:element>
)");

	void AppendNode(const FJsonObject& Node, const FADSKNodeDescriptor* Descriptor, FString& Out)
	{
		const FString Name = Node.GetStringField(TEXT("name"));
		const FString Kind = Node.GetStringField(TEXT("kind"));
		Out += FString::Printf(TEXT("  <xs:element name=\"%s\">\n"), *Name);
		Out += Doc(Descriptor ? Descriptor->Description : FString(), TEXT("    "));
		Out += TEXT("    <xs:complexType>\n      <xs:sequence>\n");
		bool bParams = false;
		bool bArgs = false;
		if (Kind == TEXT("emit"))
		{
			Out += TEXT("        <xs:element name=\"Param\" type=\"EmitParam\" minOccurs=\"0\" maxOccurs=\"unbounded\"/>\n");
		}
		else if (Node.TryGetBoolField(TEXT("params"), bParams) && bParams)
		{
			Out += TEXT("        <xs:element name=\"Param\" type=\"ValueParam\" minOccurs=\"0\" maxOccurs=\"unbounded\"/>\n");
		}
		else if (Node.TryGetBoolField(TEXT("args"), bArgs) && bArgs)
		{
			// `<Param name from/>`, the same shape as Run's.
			Out += TEXT("        <xs:element name=\"Param\" type=\"RunParam\" minOccurs=\"0\" maxOccurs=\"unbounded\"/>\n");
		}
		if (Kind == TEXT("guard"))
		{
			Out += TEXT("        <xs:choice minOccurs=\"0\" maxOccurs=\"unbounded\"><xs:element name=\"When\" type=\"When\"/><xs:element name=\"Any\" type=\"Any\"/></xs:choice>\n");
		}
		const int32 Min = static_cast<int32>(Node.GetNumberField(TEXT("minChildren")));
		const TSharedPtr<FJsonValue> Max = Node.TryGetField(TEXT("maxChildren"));
		const bool bUnbounded = !Max.IsValid() || Max->IsNull();
		const int32 MaxCount = bUnbounded ? 0 : static_cast<int32>(Max->AsNumber());
		if (bUnbounded || MaxCount > 0)
		{
			Out += FString::Printf(TEXT("        <xs:group ref=\"Node\" minOccurs=\"%d\" maxOccurs=\"%s\"/>\n"), Min,
				bUnbounded ? TEXT("unbounded") : *FString::FromInt(MaxCount));
		}
		Out += TEXT("      </xs:sequence>\n      <xs:attribute name=\"id\" type=\"xs:string\" use=\"required\"/>\n");

		const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
		if (Node.TryGetArrayField(TEXT("fields"), Fields))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Fields)
			{
				const TSharedPtr<FJsonObject> Field = Value->AsObject();
				const FString FieldName = Field->GetStringField(TEXT("name"));
				const FString Type = Field->GetStringField(TEXT("type"));
				const FADSKFieldMap* Map = Descriptor ? Descriptor->FindField(FieldName) : nullptr;
				const FString Default = Field->GetStringField(TEXT("default"));
				FString Help = FString::Printf(TEXT("%s%s."), *Type, Default.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", default %s"), *Default));
				if (Map && Map->bLegacy) Help += TEXT(" Legacy: kept for older files, do not use.");
				const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
				if (Field->TryGetArrayField(TEXT("values"), Choices))
				{
					Out += FString::Printf(TEXT("      <xs:attribute name=\"%s\">\n"), *FieldName);
					Out += Doc(Help, TEXT("        "));
					Out += TEXT("        <xs:simpleType><xs:restriction base=\"xs:string\">");
					for (const TSharedPtr<FJsonValue>& Choice : *Choices) Out += FString::Printf(TEXT("<xs:enumeration value=\"%s\"/>"), *Escape(Choice->AsString()));
					Out += TEXT("</xs:restriction></xs:simpleType>\n      </xs:attribute>\n");
					continue;
				}
				Out += FString::Printf(TEXT("      <xs:attribute name=\"%s\" type=\"%s\">\n"), *FieldName, *LiteralType(Type));
				Out += Doc(Help, TEXT("        "));
				Out += TEXT("      </xs:attribute>\n");
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Ports = nullptr;
		if (Node.TryGetArrayField(TEXT("ports"), Ports))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Ports)
			{
				const TSharedPtr<FJsonObject> Port = Value->AsObject();
				const FString Direction = Port->GetStringField(TEXT("direction"));
				Out += FString::Printf(TEXT("      <xs:attribute name=\"%s-%s\" type=\"xs:string\" use=\"required\">\n"), *Direction, *Port->GetStringField(TEXT("name")));
				const TCHAR* Written = Kind == TEXT("sensor") ? TEXT("an Input") : TEXT("a Local");
				Out += Doc(FString::Printf(TEXT("%s slot: %s."), *Port->GetStringField(TEXT("type")), Direction == TEXT("out") ? Written : TEXT("an Input or Local")), TEXT("        "));
				Out += TEXT("      </xs:attribute>\n");
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Attributes = nullptr;
		if (Node.TryGetArrayField(TEXT("attributes"), Attributes))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Attributes)
			{
				const TSharedPtr<FJsonObject> Attribute = Value->AsObject();
				Out += FString::Printf(TEXT("      <xs:attribute name=\"%s\" type=\"xs:string\"%s/>\n"), *Attribute->GetStringField(TEXT("name")),
					Attribute->GetBoolField(TEXT("required")) ? TEXT(" use=\"required\"") : TEXT(""));
			}
		}
		Out += TEXT("    </xs:complexType>\n  </xs:element>\n");
	}
}

bool FADSKSchemaXsd::Render(const FADSKNodeRegistry& Registry, const FString& RegistryLabel, FString& OutXsd, FString& OutError)
{
	OutXsd.Reset();
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
	FString Out = TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	Out += FString::Printf(TEXT("<!--\n  AdamskAI behavior files, XML v1. Generated by AdamskAIValidate -xsd; do not edit by hand.\n  Registry: %s. Fingerprint: %s.\n")
		TEXT("  For editor completion: it checks shape only, the compiler stays the authority (references, slot types, ids).\n-->\n"),
		*RegistryLabel, *Registry.GetCatalogueFingerprint().ToString());
	Out += TEXT("<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">\n\n");
	FString TreeNodes, Sensors;
	for (const TSharedPtr<FJsonValue>& Value : *Nodes)
	{
		const TSharedPtr<FJsonObject> Node = Value->AsObject();
		(Node->GetStringField(TEXT("kind")) == TEXT("sensor") ? Sensors : TreeNodes) += FString::Printf(TEXT("      <xs:element ref=\"%s\"/>\n"), *Node->GetStringField(TEXT("name")));
	}
	// Written only when the catalogue has sensors, so a schema without them keeps its text.
	FString SensorSection;
	if (!Sensors.IsEmpty())
	{
		SensorSection = TEXT("        <xs:element name=\"Sensors\" minOccurs=\"0\">\n")
			TEXT("          <xs:annotation><xs:documentation>Read by the host before every tick: each sensor writes the Inputs bound to its out- ports.</xs:documentation></xs:annotation>\n")
			TEXT("          <xs:complexType>\n            <xs:group ref=\"Sensor\" minOccurs=\"0\" maxOccurs=\"unbounded\"/>\n          </xs:complexType>\n        </xs:element>\n");
	}
	Out += FString(ADSKSchemaXsdPrivate::Language).Replace(TEXT("@SENSORS@"), *SensorSection);
	Out += TEXT("\n  <xs:group name=\"Node\">\n    <xs:choice>\n      <xs:element ref=\"Run\"/>\n") + TreeNodes + TEXT("    </xs:choice>\n  </xs:group>\n\n");
	if (!Sensors.IsEmpty())
	{
		Out += TEXT("  <xs:group name=\"Sensor\">\n    <xs:choice>\n") + Sensors + TEXT("    </xs:choice>\n  </xs:group>\n\n");
	}
	for (const TSharedPtr<FJsonValue>& Value : *Nodes)
	{
		const TSharedPtr<FJsonObject> Node = Value->AsObject();
		ADSKSchemaXsdPrivate::AppendNode(*Node, Registry.Find(Node->GetStringField(TEXT("name"))), Out);
	}
	Out += TEXT("\n</xs:schema>\n");
	OutXsd = MoveTemp(Out);
	return true;
}
