// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Compiler/ADSKCompilerInternal.h"

#include "Nodes/ADSKBuiltinNodes.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "Misc/StringOutputDevice.h"

using namespace ADSKCompilerInternal;

namespace ADSKCompilerValidateNodesPrivate
{
	bool ConvertObjectPath(FCompileContext& Context, FNode& Node, void* Value, const FString& Text, int32 Line)
	{
		if (Text.IsEmpty() || !FPackageName::IsValidObjectPath(Text))
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Line, TEXT("attribute value does not match the field type"), Node.Id);
			return false;
		}
		*static_cast<FSoftObjectPath*>(Value) = FSoftObjectPath(Text);
		return true;
	}

	/** The class itself is checked only when it can load. */
	bool ConvertClassPath(FCompileContext& Context, FNode& Node, void* Value, const FString& Text, int32 Line)
	{
		if (Text.IsEmpty() || !FPackageName::IsValidObjectPath(Text))
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Line, TEXT("attribute value does not match the field type"), Node.Id);
			return false;
		}
		*static_cast<FSoftClassPath*>(Value) = FSoftClassPath(Text);
		return true;
	}

	/** Dot-separated segments of anything but whitespace, commas and quotes, as gameplay tags are written. */
	bool IsTagText(const FString& Text)
	{
		if (Text.IsEmpty() || Text.Len() >= NAME_SIZE || Text.StartsWith(TEXT(".")) || Text.EndsWith(TEXT(".")) || Text.Contains(TEXT("..")))
		{
			return false;
		}
		for (const TCHAR C : Text)
		{
			if (FChar::IsWhitespace(C) || C == TEXT(',') || C == TEXT('"'))
			{
				return false;
			}
		}
		return true;
	}

	bool ConvertTag(FCompileContext& Context, FNode& Node, const FADSKFieldMap& Field, void* Value, const FString& Text, int32 Line)
	{
		if (!IsTagText(Text))
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Line, TEXT("attribute value is not a gameplay tag name"), Node.Id);
			return false;
		}
		ADSK::SetTagName(*CastField<FStructProperty>(Field.Property)->Struct, Value, FName(*Text));
		return true;
	}

	/** Assigns the enumerator named exactly Text; any other text is `unknown-enum-value`. */
	bool ConvertChoice(FCompileContext& Context, FNode& Node, const FADSKFieldMap& Field, void* Value, const FString& Text, int32 Line)
	{
		const int32 Index = Field.ChoiceNames.IndexOfByPredicate([&Text](const FString& Name) { return Name.Equals(Text, ESearchCase::CaseSensitive); });
		if (Index == INDEX_NONE)
		{
			Context.Error(EADSKDiagnosticCode::UnknownEnumValue, Line, TEXT("attribute value names no choice of the field"), Node.Id);
			return false;
		}
		const int64 Enumerator = Field.Enum->GetValueByIndex(Index);
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Field.Property))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(Value, Enumerator);
		}
		else
		{
			CastField<FByteProperty>(Field.Property)->SetIntPropertyValue(Value, Enumerator);
		}
		return true;
	}

	bool ConvertField(FCompileContext& Context, FNode& Node, const FADSKFieldMap& Field, const FString& Text, int32 Line)
	{
		uint8* Container = Node.Config.GetMutableMemory();
		void* Value = Field.Property->ContainerPtrToValuePtr<void>(Container);
		if (Field.Kind == EADSKFieldKind::ObjectPath) { return ConvertObjectPath(Context, Node, Value, Text, Line); }
		if (Field.Kind == EADSKFieldKind::ClassPath) { return ConvertClassPath(Context, Node, Value, Text, Line); }
		if (Field.Kind == EADSKFieldKind::Tag) { return ConvertTag(Context, Node, Field, Value, Text, Line); }
		if (Field.Kind == EADSKFieldKind::Choice) { return ConvertChoice(Context, Node, Field, Value, Text, Line); }
		if (Field.Kind == EADSKFieldKind::String) { *static_cast<FString*>(Value) = Text; return true; }
		if (Field.Kind == EADSKFieldKind::Name)
		{
			if (Text.Len() >= NAME_SIZE)
			{
				Context.Error(EADSKDiagnosticCode::OutOfRange, Line, TEXT("name is too long"), Node.Id);
				return false;
			}
			*static_cast<FName*>(Value) = FName(*Text);
			return true;
		}
		bool bLexicalOk = false;
		FVector Vector = FVector::ZeroVector;
		switch (Field.ValueType)
		{
		case EADSKValueType::Bool:
		{
			bool bParsed = false;
			bLexicalOk = ADSK::TryParseBool(Text, bParsed);
			break;
		}
		case EADSKValueType::Float:
		{
			float Parsed = 0.f;
			bLexicalOk = ADSK::TryParseFloat(Text, Parsed);
			break;
		}
		case EADSKValueType::Vector:
			bLexicalOk = ADSK::TryParseVector(Text, Vector);
			break;
		case EADSKValueType::Int:
		{
			int32 Parsed = 0;
			bLexicalOk = ADSK::TryParseInt(Text, Parsed);
			break;
		}
		}
		if (bLexicalOk && Field.ValueType == EADSKValueType::Vector)
		{
			*static_cast<FVector*>(Value) = Vector;
			return true;
		}
		bool bConverted = false;
		if (bLexicalOk)
		{
			FStringOutputDevice Errors;
			const TCHAR* Remaining = Field.Property->ImportText_Direct(*Text, Value, nullptr, PPF_None, &Errors);
			bConverted = Remaining != nullptr && *Remaining == TEXT('\0') && Errors.IsEmpty();
		}
		if (!bConverted)
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Line, TEXT("attribute value does not match the field type"), Node.Id);
		}
		return bConverted;
	}

	void BindPort(FCompileContext& Context, FNode& Node, const FADSKXmlAttribute& Attribute, bool bIn, int32 Line)
	{
		const FString PortName = Attribute.Name.RightChop(bIn ? 3 : 4);
		const int32 Port = Node.Descriptor->FindPort(PortName);
		if (Port == INDEX_NONE)
		{
			TArray<FString> Ports;
			for (const FADSKPortDescriptor& Candidate : Node.Descriptor->Ports) { Ports.Add(Candidate.Name); }
			Context.Error(EADSKDiagnosticCode::UnknownPort, Line,
				*FString::Printf(TEXT("node has no port '%s'%s"), *PortName, *DidYouMean(PortName, Ports)), Node.Id);
			return;
		}
		const FADSKPortDescriptor& Descriptor = Node.Descriptor->Ports[Port];
		const bool bDirectionOk = (Descriptor.Direction == EADSKPortDirection::In) == bIn;
		const int32 Slot = Context.FindSlot(Attribute.Value);
		if (!bDirectionOk)
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Line, TEXT("port direction does not match the attribute prefix"), Node.Id);
		}
		else if (Slot == INDEX_NONE)
		{
			Context.Error(EADSKDiagnosticCode::UnresolvedReference, Line,
				*FString::Printf(TEXT("port names no Input or Local: '%s'%s"), *Attribute.Value, *DidYouMean(Attribute.Value, Context.SlotNames())), Node.Id);
		}
		else if ((Context.Slots[Slot].Type != Descriptor.Type && !(Node.Descriptor->Kind == EADSKNodeKind::Guard && PortName == TEXT("condition") && bIn)) || (!bIn && Context.Slots[Slot].bInput != Node.bSensor))
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Line, Node.bSensor && !bIn && Context.Slots[Slot].Type == Descriptor.Type
				? TEXT("a sensor writes an Input, not a Local") : TEXT("slot type or kind does not fit the port"), Node.Id);
		}
		else
		{
			Node.PortSlots[Port] = Slot;
		}
	}

	void ApplyId(FCompileContext& Context, FNode& Node, const FString& Id, int32 Line)
	{
		// Runtime node ids are FNames, which fold case, so `aim` and `Aim` would be one node there.
		const FNode* Other = Context.Nodes.FindByPredicate(
			[&Id](const FNode& Candidate) { return Candidate.Id.Equals(Id, ESearchCase::IgnoreCase); });
		if (Id.IsEmpty())
		{
			Context.Error(EADSKDiagnosticCode::MissingRequired, Line, TEXT("node id is empty"));
		}
		else if (Other && Other->Id.Equals(Id, ESearchCase::CaseSensitive))
		{
			Context.Error(EADSKDiagnosticCode::DuplicateId, Line, TEXT("node id is not unique"), Id);
		}
		else if (Other)
		{
			Context.Error(EADSKDiagnosticCode::DuplicateId, Line, *FString::Printf(TEXT("node id differs from '%s' only by case"), *Other->Id), Id);
		}
		Node.Id = Id;
	}

	/** `is`, `below` or `above` on a Guard: compares the condition slot instead of reading a Bool. */
	void ApplyGuardComparison(FCompileContext& Context, FNode& Node, const FADSKXmlElement& Element)
	{
		EADSKCompareOp Op = EADSKCompareOp::Equal;
		const FADSKXmlAttribute* Attribute = FindCompareAttribute(Context, Element, Op, Node.Id);
		if (!Attribute)
		{
			return;
		}
		Node.Condition.bCompares = true;
		Node.Condition.Op = Op;
		const int32 Port = Node.Descriptor->FindPort(TEXT("condition"));
		const int32 Slot = Port == INDEX_NONE ? INDEX_NONE : Node.PortSlots[Port];
		if (Slot != INDEX_NONE)
		{
			ParseCompareOperand(Context, Context.Slots[Slot], Op, Attribute->Value, Element.Line, Node.Id, Node.Condition.Operand, Node.Condition.OperandSlot);
		}
	}

	bool IsCompareAttribute(const FString& Name)
	{
		return Name.Equals(TEXT("is"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("below"), ESearchCase::CaseSensitive)
			|| Name.Equals(TEXT("above"), ESearchCase::CaseSensitive);
	}

	void ApplyParam(FCompileContext& Context, FNode& Node, const FADSKXmlElement& Element)
	{
		RejectChildren(Context, Element);
		RejectUnknownAttributes(Context, Element, {TEXT("name"), TEXT("value"), TEXT("type")});
		const FString* Name = RequireAttribute(Context, Element, TEXT("name"));
		const FString* Text = RequireAttribute(Context, Element, TEXT("value"));
		if (!Name || !Text)
		{
			return;
		}
		if (Name->IsEmpty() || Name->Len() >= NAME_SIZE)
		{
			Context.Error(EADSKDiagnosticCode::MissingRequired, Element.Line, TEXT("Param name is empty or too long"), Node.Id);
			return;
		}
		FADSKNamedParam Parsed;
		Parsed.Name = FName(**Name);
		const FString* Type = Element.FindAttribute(TEXT("type"));
		if (Type && !Type->Equals(TEXT("Float"), ESearchCase::CaseSensitive) && !Type->Equals(TEXT("Int"), ESearchCase::CaseSensitive)
			&& !Type->Equals(TEXT("Bool"), ESearchCase::CaseSensitive))
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("Param type must be Float, Int or Bool"), Node.Id);
			return;
		}
		Parsed.Type = !Type || Type->Equals(TEXT("Float")) ? EADSKParamType::Float : Type->Equals(TEXT("Int")) ? EADSKParamType::Int : EADSKParamType::Bool;
		const bool bParsed = Parsed.Type == EADSKParamType::Float ? ADSK::TryParseFloat(*Text, Parsed.Value)
			: Parsed.Type == EADSKParamType::Int ? ADSK::TryParseInt(*Text, Parsed.IntValue)
			: ADSK::TryParseBool(*Text, Parsed.bBoolValue);
		if (!bParsed)
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("Param value does not match its type"), Node.Id);
			return;
		}
		const FArrayProperty* Array = CastField<FArrayProperty>(Node.Descriptor->ConfigStruct->FindPropertyByName(Node.Descriptor->ParamsProperty));
		TArray<FADSKNamedParam>& Params = *Array->ContainerPtrToValuePtr<TArray<FADSKNamedParam>>(Node.Config.GetMutableMemory());
		// Case-insensitive: names become FNames, so a case-only twin would silently overwrite the first.
		if (Params.ContainsByPredicate([Name](const FADSKNamedParam& Param) { return Param.Name.ToString().Equals(*Name, ESearchCase::IgnoreCase); }))
		{
			Context.Error(EADSKDiagnosticCode::DuplicateId, Element.Line, TEXT("Param name repeats on this node"), Node.Id);
			return;
		}
		Params.Add(MoveTemp(Parsed));
	}

	/** A `<Param>` child naming its target and, unless bValueAllowed is false, either `value` or `from`; false after a report. */
	bool ReadArg(FCompileContext& Context, const FADSKXmlElement& Element, const FString& NodeId, bool bValueAllowed,
		FString& OutName, int32& OutSlot, FString& OutValue)
	{
		RejectChildren(Context, Element);
		const TCHAR* Allowed[] = {TEXT("name"), TEXT("from"), TEXT("value")};
		RejectUnknownAttributes(Context, Element, TConstArrayView<const TCHAR*>(Allowed, bValueAllowed ? 3 : 2));
		const FString* Name = RequireAttribute(Context, Element, TEXT("name"));
		const FString* From = Element.FindAttribute(TEXT("from"));
		const FString* Value = bValueAllowed ? Element.FindAttribute(TEXT("value")) : nullptr;
		if (!Name)
		{
			return false;
		}
		if ((From != nullptr) == (Value != nullptr) && (bValueAllowed || !From))
		{
			Context.Error(From ? EADSKDiagnosticCode::BadArity : EADSKDiagnosticCode::MissingRequired, Element.Line,
				bValueAllowed ? TEXT("Param needs exactly one of value and from") : TEXT("Param needs from"), NodeId);
			return false;
		}
		OutName = *Name;
		if (Value)
		{
			OutValue = *Value;
			return true;
		}
		OutSlot = Context.FindSlot(*From);
		if (OutSlot == INDEX_NONE)
		{
			Context.Error(EADSKDiagnosticCode::UnresolvedReference, Element.Line, TEXT("from names no Input, Local or tree parameter"), NodeId);
			return false;
		}
		return true;
	}

	/** One `<Param name value|from/>` of an Emit; checked against the event's parameters once events resolve. */
	void ApplyEmitArg(FCompileContext& Context, FNode& Node, const FADSKXmlElement& Element)
	{
		FEmitArg Arg;
		Arg.Line = Element.Line;
		if (!ReadArg(Context, Element, Node.Id, true, Arg.Name, Arg.Slot, Arg.Value))
		{
			return;
		}
		if (Node.EmitArgs.ContainsByPredicate([&Arg](const FEmitArg& Other) { return Other.Name.Equals(Arg.Name, ESearchCase::CaseSensitive); }))
		{
			Context.Error(EADSKDiagnosticCode::DuplicateId, Element.Line, TEXT("Param name repeats on this Emit"), Node.Id);
			return;
		}
		Node.EmitArgs.Add(MoveTemp(Arg));
	}

	/** One `<Param name from/>` of a node whose descriptor takes args. */
	void ApplyNodeArg(FCompileContext& Context, FNode& Node, const FADSKXmlElement& Element)
	{
		FADSKNodeArg Arg;
		FString Unused;
		if (!ReadArg(Context, Element, Node.Id, false, Arg.Name, Arg.Slot, Unused))
		{
			return;
		}
		if (Node.Args.ContainsByPredicate([&Arg](const FADSKNodeArg& Other) { return Other.Name.Equals(Arg.Name, ESearchCase::CaseSensitive); }))
		{
			Context.Error(EADSKDiagnosticCode::DuplicateId, Element.Line, TEXT("Param name repeats on this node"), Node.Id);
			return;
		}
		// An enum value is an index into this file's enum, meaningless to whoever receives it.
		if (Context.Slots[Arg.Slot].Type == EADSKValueType::Enum)
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("from names an enum slot; a node's Param passes Bool, Float, Int, Vector or Object"), Node.Id);
			return;
		}
		Node.Args.Add(MoveTemp(Arg));
	}

	/** A Set's `to` and its `from` or `value`, typed by the target slot; reports and leaves Target unset on a mistake. */
	void ApplySet(FCompileContext& Context, FNode& Node, const FADSKXmlElement& Element)
	{
		const FString* To = Element.FindAttribute(TEXT("to"));
		const FString* From = Element.FindAttribute(TEXT("from"));
		const FString* Value = Element.FindAttribute(TEXT("value"));
		if (!To)
		{
			Context.Error(EADSKDiagnosticCode::MissingRequired, Element.Line, TEXT("Set needs to"), Node.Id);
			return;
		}
		if ((From != nullptr) == (Value != nullptr))
		{
			Context.Error(From ? EADSKDiagnosticCode::BadArity : EADSKDiagnosticCode::MissingRequired, Element.Line, TEXT("Set needs exactly one of value and from"), Node.Id);
			return;
		}
		const int32 Target = Context.FindSlot(*To);
		if (Target == INDEX_NONE)
		{
			Context.Error(EADSKDiagnosticCode::UnresolvedReference, Element.Line,
				*FString::Printf(TEXT("to names no Local or tree parameter: '%s'%s"), **To, *DidYouMean(*To, Context.SlotNames())), Node.Id);
			return;
		}
		const FSlot& Slot = Context.Slots[Target];
		if (Slot.bInput)
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("Set writes Locals only; the host writes Inputs"), Node.Id);
			return;
		}
		if (From)
		{
			const int32 Source = Context.FindSlot(*From);
			if (Source == INDEX_NONE)
			{
				Context.Error(EADSKDiagnosticCode::UnresolvedReference, Element.Line,
					*FString::Printf(TEXT("from names no Input, Local or tree parameter: '%s'%s"), **From, *DidYouMean(*From, Context.SlotNames())), Node.Id);
				return;
			}
			if (Context.Slots[Source].Type != Slot.Type || Context.Slots[Source].Enum != Slot.Enum)
			{
				Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("from and to slot types differ"), Node.Id);
				return;
			}
			Node.Assign.Source = Source;
		}
		else if (Slot.Type == EADSKValueType::Enum)
		{
			const int32 Index = Context.Enums[Slot.Enum].Values.IndexOfByPredicate([Value](const FString& Name) { return Name.Equals(*Value, ESearchCase::CaseSensitive); });
			if (Index == INDEX_NONE)
			{
				Context.Error(EADSKDiagnosticCode::UnknownEnumValue, Element.Line, TEXT("value names no value of the slot's enum"), Node.Id);
				return;
			}
			Node.Assign.Value = FADSKValue::MakeEnum(Slot.Enum, Index);
		}
		else if (!ADSK::TryParseValue(Slot.Type, *Value, Node.Assign.Value))
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("value does not match the slot's type"), Node.Id);
			return;
		}
		Node.Assign.Target = Target;
	}

	void BindPorts(FCompileContext& Context, FNode& Node, const FADSKXmlElement& Element)
	{
		for (const FADSKXmlAttribute& Attribute : Element.Attributes)
		{
			if (Attribute.Name.StartsWith(TEXT("in-"), ESearchCase::CaseSensitive))
			{
				BindPort(Context, Node, Attribute, true, Element.Line);
			}
			else if (Attribute.Name.StartsWith(TEXT("out-"), ESearchCase::CaseSensitive))
			{
				BindPort(Context, Node, Attribute, false, Element.Line);
			}
		}
	}

	/** False only when a config field failed to convert. */
	bool ApplyAttributes(FCompileContext& Context, int32 NodeIndex, const FADSKXmlElement& Element)
	{
		FNode& Node = Context.Nodes[NodeIndex];
		bool bConfigOk = true;
		if (const FString* Id = Element.FindAttribute(TEXT("id")))
		{
			ApplyId(Context, Node, *Id, Element.Line);
		}
		else
		{
			Context.Error(EADSKDiagnosticCode::MissingRequired, Element.Line, TEXT("node requires an id"));
		}
		BindPorts(Context, Node, Element);
		if (Node.Descriptor->Kind == EADSKNodeKind::Guard)
		{
			ApplyGuardComparison(Context, Node, Element);
		}
		for (const FADSKXmlAttribute& Attribute : Element.Attributes)
		{
			if (Attribute.Name.Equals(TEXT("id"), ESearchCase::CaseSensitive)
				|| Attribute.Name.StartsWith(TEXT("in-"), ESearchCase::CaseSensitive)
				|| Attribute.Name.StartsWith(TEXT("out-"), ESearchCase::CaseSensitive))
			{
				continue;
			}
			else if (Node.Descriptor->Kind == EADSKNodeKind::Emit && Attribute.Name.Equals(TEXT("event"), ESearchCase::CaseSensitive))
			{
				Node.EmitEventName = Attribute.Value;
				Node.bHasEmitEvent = true;
			}
			else if (Node.Descriptor->Kind == EADSKNodeKind::Guard && IsCompareAttribute(Attribute.Name))
			{
				continue;
			}
			else if (Node.Descriptor->Kind == EADSKNodeKind::Set
				&& (Attribute.Name.Equals(TEXT("to"), ESearchCase::CaseSensitive) || Attribute.Name.Equals(TEXT("from"), ESearchCase::CaseSensitive)
					|| Attribute.Name.Equals(TEXT("value"), ESearchCase::CaseSensitive)))
			{
				continue;
			}
			else if (const FADSKFieldMap* Field = Node.Descriptor->FindField(Attribute.Name))
			{
				bConfigOk = ConvertField(Context, Node, *Field, Attribute.Value, Element.Line) && bConfigOk;
			}
			else
			{
				TArray<FString> Names{TEXT("id")};
				for (const FADSKFieldMap& Known : Node.Descriptor->Fields) { Names.Add(Known.XmlName); }
				for (const FADSKPortDescriptor& Port : Node.Descriptor->Ports)
				{
					Names.Add((Port.Direction == EADSKPortDirection::In ? TEXT("in-") : TEXT("out-")) + Port.Name);
				}
				Context.Error(EADSKDiagnosticCode::UnknownAttribute, Element.Line,
					*FString::Printf(TEXT("unknown attribute '%s'%s"), *Attribute.Name, *DidYouMean(Attribute.Name, Names)), Node.Id);
			}
		}
		if (Node.Descriptor->Kind == EADSKNodeKind::Emit && !Element.FindAttribute(TEXT("event"))) Context.Error(EADSKDiagnosticCode::MissingRequired, Element.Line, TEXT("Emit needs event"), Node.Id);
		if (Node.Descriptor->Kind == EADSKNodeKind::Set) ApplySet(Context, Node, Element);
		return bConfigOk;
	}

	void CheckPortsAndConfig(FCompileContext& Context, int32 NodeIndex, const FADSKXmlElement& Element, bool bConfigOk)
	{
		const FNode& Node = Context.Nodes[NodeIndex];
		for (int32 Port = 0; Port < Node.PortSlots.Num(); ++Port)
		{
			if (Node.PortSlots[Port] == INDEX_NONE && !Element.FindAttribute(FString::Printf(TEXT("%s%s"),
					Node.Descriptor->Ports[Port].Direction == EADSKPortDirection::In ? TEXT("in-") : TEXT("out-"), *Node.Descriptor->Ports[Port].Name)))
			{
				Context.Error(EADSKDiagnosticCode::MissingRequired, Element.Line, TEXT("declared port is not bound"), Node.Id);
			}
		}
		if (Node.Descriptor->Kind == EADSKNodeKind::Guard && !Node.Condition.bCompares && Node.PortSlots[0] != INDEX_NONE && Context.Slots[Node.PortSlots[0]].Type != EADSKValueType::Bool)
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("Guard condition needs Bool without is"), Node.Id);
		}
		if (bConfigOk && Node.Descriptor->ValidateConfig && !Node.Descriptor->ValidateConfig(Node.Config.GetMemory()))
		{
			Context.Error(EADSKDiagnosticCode::OutOfRange, Element.Line, TEXT("configuration failed validation"), Node.Id);
		}
	}

	/** `Run` is compile-time syntax: a placeholder node with no descriptor, replaced before linking. */
	int32 ValidateRun(FCompileContext& Context, const FADSKXmlElement& Element, int32 Parent)
	{
		RejectUnknownAttributes(Context, Element, {TEXT("id"), TEXT("tree")});
		FNode Node;
		Node.bRun = true;
		Node.Line = Element.Line;
		Node.Parent = Parent;
		const int32 Index = Context.Nodes.Add(MoveTemp(Node));
		if (Parent != INDEX_NONE)
		{
			Context.Nodes[Parent].Children.Add(Index);
		}
		if (const FString* Id = RequireAttribute(Context, Element, TEXT("id")))
		{
			ApplyId(Context, Context.Nodes[Index], *Id, Element.Line);
		}
		if (const FString* Tree = RequireAttribute(Context, Element, TEXT("tree")))
		{
			Context.Nodes[Index].RunTree = *Tree;
		}
		for (const int32 Child : Element.Children)
		{
			const FADSKXmlElement& ChildElement = Context.Element(Child);
			FNode& Run = Context.Nodes[Index];
			if (!ChildElement.Name.Equals(TEXT("Param"), ESearchCase::CaseSensitive))
			{
				Context.Error(EADSKDiagnosticCode::BadArity, ChildElement.Line, TEXT("Run takes only Param children"), Run.Id);
				continue;
			}
			FRunArg Arg;
			Arg.Line = ChildElement.Line;
			FString Unused;
			if (!ReadArg(Context, ChildElement, Run.Id, false, Arg.Name, Arg.Slot, Unused))
			{
				continue;
			}
			if (Run.RunArgs.ContainsByPredicate([&Arg](const FRunArg& Other) { return Other.Name.Equals(Arg.Name, ESearchCase::CaseSensitive); }))
			{
				Context.Error(EADSKDiagnosticCode::DuplicateId, ChildElement.Line, TEXT("Param name repeats on this Run"), Run.Id);
				continue;
			}
			Run.RunArgs.Add(MoveTemp(Arg));
		}
		return Index;
	}

	bool SensorWrites(const FNode& Sensor, int32 Slot)
	{
		for (int32 Port = 0; Port < Sensor.PortSlots.Num(); ++Port)
		{
			if (Sensor.PortSlots[Port] == Slot && Sensor.Descriptor->Ports[Port].Direction == EADSKPortDirection::Out)
			{
				return true;
			}
		}
		return false;
	}

	int32 ValidateNode(FCompileContext& Context, int32 ElementIndex, int32 Parent, int32 Depth)
	{
		const FADSKXmlElement& Element = Context.Element(ElementIndex);
		if (Depth > FADSKCompiler::MaxTreeDepth)
		{
			Context.Error(EADSKDiagnosticCode::LimitExceeded, Element.Line, TEXT("tree depth exceeds 16"));
			return INDEX_NONE;
		}
		if (Element.Name.Equals(TEXT("Run"), ESearchCase::CaseSensitive))
		{
			return ValidateRun(Context, Element, Parent);
		}
		const FADSKNodeDescriptor* Descriptor = Context.Registry->Find(Element.Name);
		if (!Descriptor)
		{
			TArray<FString> Names{TEXT("Run")};
			for (const FADSKNodeDescriptor* Candidate : Context.Registry->GetDescriptors()) { Names.Add(Candidate->Name); }
			Context.Error(EADSKDiagnosticCode::UnknownNode, Element.Line,
				*FString::Printf(TEXT("no node type '%s' in the catalogue%s"), *Element.Name, *DidYouMean(Element.Name, Names)));
			return INDEX_NONE;
		}
		if ((Descriptor->Kind == EADSKNodeKind::Sensor) != Context.bInSensors)
		{
			Context.Error(EADSKDiagnosticCode::UnknownNode, Element.Line, Context.bInSensors
				? *FString::Printf(TEXT("'%s' is not a sensor; Sensors takes only sensors"), *Element.Name)
				: *FString::Printf(TEXT("'%s' is a sensor; it belongs in Sensors, not in a tree"), *Element.Name));
			return INDEX_NONE;
		}
		FNode Node;
		Node.Descriptor = Descriptor;
		Node.Line = Element.Line;
		Node.Parent = Parent;
		Node.bSensor = Context.bInSensors;
		Node.PortSlots.Init(INDEX_NONE, Descriptor->Ports.Num());
		if (Descriptor->ConfigStruct)
		{
			Node.Config.InitializeAs(Descriptor->ConfigStruct);
		}
		const int32 Index = Context.Nodes.Add(MoveTemp(Node));
		if (Parent != INDEX_NONE)
		{
			Context.Nodes[Parent].Children.Add(Index);
		}
		const bool bConfigOk = ApplyAttributes(Context, Index, Element);
		// `Param` children are configuration, not tree children; ValidateConfig sees them applied.
		TArray<int32, TInlineAllocator<8>> TreeChildren;
		bool bUnusedSyntaxOk = true;
		for (const int32 Child : Element.Children)
		{
			const FADSKXmlElement& ChildElement = Context.Element(Child);
			if (IsConditionElement(ChildElement))
			{
				if (Descriptor->Kind != EADSKNodeKind::Guard)
				{
					Context.Error(EADSKDiagnosticCode::UnknownElement, ChildElement.Line, TEXT("When and Any belong to On and Guard"), Context.Nodes[Index].Id);
				}
				else if (!TreeChildren.IsEmpty())
				{
					Context.Error(EADSKDiagnosticCode::BadArity, ChildElement.Line, TEXT("When and Any come before the Guard's child"), Context.Nodes[Index].Id);
				}
				else
				{
					ValidateConditionElement(Context, ChildElement, Context.Nodes[Index].Id, Context.Nodes[Index].Terms, bUnusedSyntaxOk);
				}
			}
			else if (!ChildElement.Name.Equals(TEXT("Param"), ESearchCase::CaseSensitive))
			{
				TreeChildren.Add(Child);
			}
			else if (Descriptor->Kind == EADSKNodeKind::Emit)
			{
				ApplyEmitArg(Context, Context.Nodes[Index], ChildElement);
			}
			else if (Descriptor->bTakesArgs)
			{
				ApplyNodeArg(Context, Context.Nodes[Index], ChildElement);
			}
			else if (Descriptor->ParamsProperty.IsNone())
			{
				Context.Error(EADSKDiagnosticCode::UnknownElement, ChildElement.Line, TEXT("node takes no Param"), Context.Nodes[Index].Id);
			}
			else
			{
				ApplyParam(Context, Context.Nodes[Index], ChildElement);
			}
		}
		CheckPortsAndConfig(Context, Index, Element, bConfigOk);
		const int32 ChildCount = TreeChildren.Num();
		if (ChildCount < Descriptor->MinChildren || ChildCount > Descriptor->MaxChildren)
		{
			Context.Error(EADSKDiagnosticCode::BadArity, Element.Line, TEXT("child count does not fit the node"), Context.Nodes[Index].Id);
		}
		for (const int32 Child : TreeChildren)
		{
			ValidateNode(Context, Child, Index, Depth + 1);
		}
		return Index;
	}
}

using namespace ADSKCompilerValidateNodesPrivate;

int32 ADSKCompilerInternal::ValidateTreeParam(FCompileContext& Context, const FADSKXmlElement& Element)
{
	RejectChildren(Context, Element);
	RejectUnknownAttributes(Context, Element, {TEXT("name"), TEXT("type")});
	const FString* Name = RequireAttribute(Context, Element, TEXT("name"));
	const FString* Type = RequireAttribute(Context, Element, TEXT("type"));
	if (!Name || !Type)
	{
		return INDEX_NONE;
	}
	FSlot Slot;
	Slot.Name = *Name;
	Slot.Line = Element.Line;
	Slot.FormalTree = Context.CurrentTree;
	if (!ParseValueType(*Type, Slot.Type))
	{
		Slot.Enum = Context.FindEnum(*Type);
		if (Slot.Enum == INDEX_NONE)
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("unknown or unsupported parameter type"));
			return INDEX_NONE;
		}
		Slot.Type = EADSKValueType::Enum;
	}
	if (Context.FindSlot(Slot.Name) != INDEX_NONE)
	{
		Context.Error(EADSKDiagnosticCode::DuplicateId, Element.Line, TEXT("tree parameter repeats a slot or parameter name"));
		return INDEX_NONE;
	}
	Slot.Default = Slot.Type == EADSKValueType::Enum ? FADSKValue::MakeEnum(Slot.Enum, 0) : FADSKValue::MakeZero(Slot.Type);
	return Context.Slots.Add(MoveTemp(Slot));
}

void ADSKCompilerInternal::ValidateTree(FCompileContext& Context, int32 ElementIndex)
{
	const FADSKXmlElement& Element = Context.Element(ElementIndex);
	RejectUnknownAttributes(Context, Element, {TEXT("id")});
	FTree Tree;
	Tree.Line = Element.Line;
	if (const FString* Id = RequireAttribute(Context, Element, TEXT("id")))
	{
		Tree.Id = *Id;
		if (Context.FindTree(*Id) != INDEX_NONE)
		{
			Context.Error(EADSKDiagnosticCode::DuplicateId, Element.Line, TEXT("duplicate tree id"));
		}
	}
	Context.CurrentTree = Context.Trees.Num();
	TArray<int32, TInlineAllocator<4>> Roots;
	for (const int32 Child : Element.Children)
	{
		const FADSKXmlElement& ChildElement = Context.Element(Child);
		if (!ChildElement.Name.Equals(TEXT("Param"), ESearchCase::CaseSensitive))
		{
			Roots.Add(Child);
		}
		else if (const int32 Formal = ValidateTreeParam(Context, ChildElement); Formal != INDEX_NONE)
		{
			Tree.Formals.Add(Formal);
		}
	}
	if (Roots.Num() != 1)
	{
		Context.Error(EADSKDiagnosticCode::BadArity, Element.Line, TEXT("a Tree has exactly one root node"));
	}
	for (const int32 Child : Roots)
	{
		const int32 Root = ValidateNode(Context, Child, INDEX_NONE, 1);
		Tree.Root = Tree.Root == INDEX_NONE ? Root : Tree.Root;
	}
	Context.CurrentTree = INDEX_NONE;
	Context.Trees.Add(MoveTemp(Tree));
}

void ADSKCompilerInternal::ValidateSensors(FCompileContext& Context, int32 ElementIndex)
{
	const FADSKXmlElement& Section = Context.Element(ElementIndex);
	RejectUnknownAttributes(Context, Section, TConstArrayView<const TCHAR*>());
	Context.bInSensors = true;
	for (const int32 Child : Section.Children)
	{
		const FADSKXmlElement& Element = Context.Element(Child);
		if (Element.Name.Equals(TEXT("Run"), ESearchCase::CaseSensitive))
		{
			Context.Error(EADSKDiagnosticCode::UnknownNode, Element.Line, TEXT("Sensors takes only sensors"));
			continue;
		}
		const int32 Index = ValidateNode(Context, Child, INDEX_NONE, 1);
		if (Index == INDEX_NONE)
		{
			continue;
		}
		// One writer per Input: two sensors on one slot would overwrite each other every tick.
		const FNode& Sensor = Context.Nodes[Index];
		for (int32 Port = 0; Port < Sensor.PortSlots.Num(); ++Port)
		{
			const int32 Slot = Sensor.PortSlots[Port];
			if (Slot == INDEX_NONE || Sensor.Descriptor->Ports[Port].Direction != EADSKPortDirection::Out)
			{
				continue;
			}
			const bool bTaken = Context.Nodes.ContainsByPredicate([&Sensor, Slot](const FNode& Other) { return &Other != &Sensor && Other.bSensor && SensorWrites(Other, Slot); });
			if (bTaken)
			{
				Context.Error(EADSKDiagnosticCode::DuplicateId, Element.Line,
					*FString::Printf(TEXT("Input '%s' is already written by another sensor"), *Context.Slots[Slot].Name), Sensor.Id);
			}
		}
	}
	Context.bInSensors = false;
}
