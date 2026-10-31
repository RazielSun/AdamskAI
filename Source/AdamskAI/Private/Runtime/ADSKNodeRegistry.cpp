// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Runtime/ADSKNodeRegistry.h"

#include "ADSKModule.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Modules/ModuleManager.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/PropertyOptional.h"
#include "UObject/UnrealType.h"

namespace ADSKNodeRegistryPrivate
{
	constexpr const TCHAR* ReservedNames[] = {TEXT("Agent"), TEXT("Inputs"), TEXT("Input"), TEXT("Locals"), TEXT("Local"),
		TEXT("Events"), TEXT("Event"), TEXT("Param"), TEXT("StateMachine"), TEXT("State"), TEXT("On"), TEXT("Bind"),
		TEXT("Tree"), TEXT("Enums"), TEXT("Enum"), TEXT("Value"), TEXT("Run"), TEXT("When"), TEXT("Any"), TEXT("Sensors")};

	bool IsAlpha(TCHAR C)
	{
		return (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z'));
	}

	bool IsAlnum(TCHAR C)
	{
		return IsAlpha(C) || (C >= TEXT('0') && C <= TEXT('9'));
	}

	bool IsNodeName(const FString& Name)
	{
		if (Name.IsEmpty() || !IsAlpha(Name[0]))
		{
			return false;
		}
		for (int32 Index = 1; Index < Name.Len(); ++Index)
		{
			if (!IsAlnum(Name[Index]))
			{
				return false;
			}
		}
		return true;
	}

	/** Identifier for fields and ports: a letter, then letters, digits or `-`. */

	bool IsReservedName(const FString& Name)
	{
		for (const TCHAR* Reserved : ReservedNames)
		{
			if (Name.Equals(Reserved, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	bool IsReservedFieldName(const FString& Name)
	{
		return Name.Equals(TEXT("id"), ESearchCase::CaseSensitive) || Name.StartsWith(TEXT("in-"), ESearchCase::CaseSensitive)
			|| Name.StartsWith(TEXT("out-"), ESearchCase::CaseSensitive);
	}

	bool HasArityShape(const FADSKNodeDescriptor& Descriptor)
	{
		const EADSKSelectorPolicy Policy = Descriptor.SelectorPolicy;
		if (Policy != EADSKSelectorPolicy::Ordered && (Descriptor.Kind != EADSKNodeKind::Selector || Descriptor.MaxChildren > ADSK::MaxReactiveChildren
			|| (Policy == EADSKSelectorPolicy::Reactive && Descriptor.MemoryStruct != FADSKReactiveSelectorMemory::StaticStruct())
			|| (Policy == EADSKSelectorPolicy::Utility && (Descriptor.ConfigStruct != FADSKUtilityConfig::StaticStruct()
				|| Descriptor.MemoryStruct != FADSKUtilitySelectorMemory::StaticStruct()))))
		{
			return false;
		}
		switch (Descriptor.Kind)
		{
		case EADSKNodeKind::Leaf:
			return Descriptor.OnStart && Descriptor.MinChildren == 0 && Descriptor.MaxChildren == 0;
		case EADSKNodeKind::Sequence:
			return Descriptor.MinChildren >= 1 && Descriptor.MaxChildren >= Descriptor.MinChildren;
		case EADSKNodeKind::Selector:
			return Descriptor.MinChildren >= 1 && Descriptor.MaxChildren >= Descriptor.MinChildren;
		case EADSKNodeKind::Guard:
		{
			if (Descriptor.MinChildren != 1 || Descriptor.MaxChildren != 1)
			{
				return false;
			}
			const int32 Port = Descriptor.FindPort(TEXT("condition"));
			return Port != INDEX_NONE && Descriptor.Ports[Port].Type == EADSKValueType::Bool
				&& Descriptor.Ports[Port].Direction == EADSKPortDirection::In;
		}
		case EADSKNodeKind::Emit:
		case EADSKNodeKind::Set:
			return Descriptor.MinChildren == 0 && Descriptor.MaxChildren == 0 && Descriptor.Ports.IsEmpty() && Descriptor.Fields.IsEmpty();
		case EADSKNodeKind::Scope:
			return Descriptor.OnStart && Descriptor.MinChildren == 1 && Descriptor.MaxChildren == 1;
		case EADSKNodeKind::Parallel:
			return Descriptor.MinChildren == 2 && Descriptor.MaxChildren == 2 && Descriptor.Ports.IsEmpty() && Descriptor.Fields.IsEmpty();
		case EADSKNodeKind::Concurrent:
			return Descriptor.MinChildren >= 2 && Descriptor.MaxChildren <= ADSK::MaxParallelChildren && Descriptor.MaxChildren >= Descriptor.MinChildren
				&& Descriptor.Ports.IsEmpty() && Descriptor.MemoryStruct == FADSKParallelMemory::StaticStruct() && Descriptor.ConfigStruct == FADSKParallelConfig::StaticStruct();
		case EADSKNodeKind::Composite:
			return Descriptor.OnCompositeStart && Descriptor.OnChildFinished && Descriptor.MinChildren >= 1 && Descriptor.MaxChildren >= Descriptor.MinChildren;
		case EADSKNodeKind::Sensor:
			return Descriptor.OnSample && Descriptor.MinChildren == 0 && Descriptor.MaxChildren == 0 && !Descriptor.OnStart && !Descriptor.MemoryStruct
				&& !Descriptor.StateStruct && Descriptor.Ports.ContainsByPredicate([](const FADSKPortDescriptor& Port) { return Port.Direction == EADSKPortDirection::Out; });
		}
		return false;
	}

	/** Enumerators an author may write: every declared name except the generated `_MAX`. */
	TArray<FString> ChoiceNames(const UEnum& Enum)
	{
		TArray<FString> Names;
		const int32 Count = Enum.NumEnums() - (Enum.ContainsExistingMax() ? 1 : 0);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Names.Add(Enum.GetNameStringByIndex(Index));
		}
		return Names;
	}

	/** The enum of a UENUM property (an enum class or a TEnumAsByte), or null. */
	const UEnum* FindChoiceEnum(const FProperty* Property)
	{
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			return EnumProperty->GetEnum();
		}
		const FByteProperty* ByteProperty = CastField<FByteProperty>(Property);
		return ByteProperty ? ByteProperty->Enum.Get() : nullptr;
	}

	int64 GetChoiceValue(const FProperty* Property, const void* Value)
	{
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			return EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
		}
		return CastField<FByteProperty>(Property)->GetSignedIntPropertyValue(Value);
	}

	EADSKRegisterResult CheckParamsProperty(const FADSKNodeDescriptor& Descriptor)
	{
		if (Descriptor.bTakesArgs)
		{
			const bool bHasCallbacks = Descriptor.Kind == EADSKNodeKind::Leaf || Descriptor.Kind == EADSKNodeKind::Scope || Descriptor.Kind == EADSKNodeKind::Composite;
			return bHasCallbacks && Descriptor.ParamsProperty.IsNone() ? EADSKRegisterResult::Accepted : EADSKRegisterResult::InvalidParamsProperty;
		}
		if (Descriptor.ParamsProperty.IsNone())
		{
			return EADSKRegisterResult::Accepted;
		}
		const FArrayProperty* Array = Descriptor.ConfigStruct ? CastField<FArrayProperty>(Descriptor.ConfigStruct->FindPropertyByName(Descriptor.ParamsProperty)) : nullptr;
		const FStructProperty* Inner = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
		return Inner && Inner->Struct == FADSKNamedParam::StaticStruct() ? EADSKRegisterResult::Accepted : EADSKRegisterResult::InvalidParamsProperty;
	}

	/** Maps a reflected property onto a value type; false when it is not bool, float or exact FVector. */
	bool MapPropertyType(const FProperty* Property, EADSKValueType& OutType)
	{
		if (CastField<FBoolProperty>(Property))
		{
			OutType = EADSKValueType::Bool;
			return true;
		}
		if (CastField<FFloatProperty>(Property))
		{
			OutType = EADSKValueType::Float;
			return true;
		}
		if (CastField<FIntProperty>(Property))
		{
			OutType = EADSKValueType::Int;
			return true;
		}
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (StructProperty && StructProperty->Struct == TBaseStructure<FVector>::Get())
		{
			OutType = EADSKValueType::Vector;
			return true;
		}
		return false;
	}

	FString FormatDefault(const FADSKFieldMap& Field, const void* Value)
	{
		if (Field.Kind == EADSKFieldKind::ObjectPath || Field.Kind == EADSKFieldKind::ClassPath) { return static_cast<const FSoftObjectPath*>(Value)->ToString(); }
		if (Field.Kind == EADSKFieldKind::Tag)
		{
			const FName Tag = ADSK::GetTagName(*CastField<FStructProperty>(Field.Property)->Struct, Value);
			return Tag.IsNone() ? FString() : Tag.ToString();
		}
		if (Field.Kind == EADSKFieldKind::Choice) { return Field.Enum->GetNameStringByValue(GetChoiceValue(Field.Property, Value)); }
		if (Field.Kind == EADSKFieldKind::Name) { return static_cast<const FName*>(Value)->ToString(); }
		if (Field.Kind == EADSKFieldKind::String) { return *static_cast<const FString*>(Value); }
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Field.Property)) { return Bool->GetPropertyValue(Value) ? TEXT("true") : TEXT("false"); }
		if (const FFloatProperty* Float = CastField<FFloatProperty>(Field.Property)) { return FString::SanitizeFloat(Float->GetPropertyValue(Value)); }
		if (const FIntProperty* Int = CastField<FIntProperty>(Field.Property)) { return FString::FromInt(Int->GetPropertyValue(Value)); }
		const FVector& Vector = *static_cast<const FVector*>(Value);
		return FString::Printf(TEXT("[%s,%s,%s]"), *FString::SanitizeFloat(Vector.X), *FString::SanitizeFloat(Vector.Y), *FString::SanitizeFloat(Vector.Z));
	}

	EADSKRegisterResult ResolveFields(FADSKNodeDescriptor& Descriptor)
	{
		FStructOnScope Defaults(Descriptor.ConfigStruct);
		for (int32 Index = 0; Index < Descriptor.Fields.Num(); ++Index)
		{
			FADSKFieldMap& Field = Descriptor.Fields[Index];
			if (!ADSK::IsIdentifier(Field.XmlName) || IsReservedFieldName(Field.XmlName))
			{
				return EADSKRegisterResult::InvalidFieldName;
			}
			for (int32 Earlier = 0; Earlier < Index; ++Earlier)
			{
				if (Descriptor.Fields[Earlier].XmlName.Equals(Field.XmlName, ESearchCase::CaseSensitive))
				{
					return EADSKRegisterResult::DuplicateField;
				}
			}
			Field.Property = Descriptor.ConfigStruct ? Descriptor.ConfigStruct->FindPropertyByName(Field.PropertyName) : nullptr;
			if (!Field.Property)
			{
				return EADSKRegisterResult::MissingProperty;
			}
			const FStructProperty* PathProperty = CastField<FStructProperty>(Field.Property);
			Field.Enum = FindChoiceEnum(Field.Property);
			Field.Kind = PathProperty && PathProperty->Struct == TBaseStructure<FSoftObjectPath>::Get() ? EADSKFieldKind::ObjectPath
				: PathProperty && PathProperty->Struct == TBaseStructure<FSoftClassPath>::Get() ? EADSKFieldKind::ClassPath
				: PathProperty && ADSK::IsGameplayTagStruct(PathProperty->Struct) ? EADSKFieldKind::Tag
				: Field.Enum ? EADSKFieldKind::Choice
				: CastField<FNameProperty>(Field.Property) ? EADSKFieldKind::Name
				: CastField<FStrProperty>(Field.Property) ? EADSKFieldKind::String
				: EADSKFieldKind::Value;
			if (Field.Kind == EADSKFieldKind::Choice)
			{
				Field.ChoiceNames = ChoiceNames(*Field.Enum);
				if (Field.ChoiceNames.IsEmpty())
				{
					return EADSKRegisterResult::UnsupportedFieldType;
				}
			}
			if ((Field.Kind == EADSKFieldKind::Value && !MapPropertyType(Field.Property, Field.ValueType))
				|| (Field.MetaClass && Field.Kind != EADSKFieldKind::ClassPath))
			{
				return EADSKRegisterResult::UnsupportedFieldType;
			}
			Field.DefaultValue = FormatDefault(Field, Field.Property->ContainerPtrToValuePtr<void>(Defaults.GetStructMemory()));
		}
		return EADSKRegisterResult::Accepted;
	}

	EADSKRegisterResult CheckPorts(const FADSKNodeDescriptor& Descriptor)
	{
		for (int32 Index = 0; Index < Descriptor.Ports.Num(); ++Index)
		{
			const FString& Name = Descriptor.Ports[Index].Name;
			if (!ADSK::IsIdentifier(Name))
			{
				return EADSKRegisterResult::InvalidPortName;
			}
			for (int32 Earlier = 0; Earlier < Index; ++Earlier)
			{
				if (Descriptor.Ports[Earlier].Name.Equals(Name, ESearchCase::CaseSensitive))
				{
					return EADSKRegisterResult::DuplicatePort;
				}
			}
			if (Descriptor.FindField(Name))
			{
				return EADSKRegisterResult::PortFieldCollision;
			}
			if (Descriptor.Ports[Index].Type == EADSKValueType::Enum) return EADSKRegisterResult::UnsupportedPortType;
		}
		return EADSKRegisterResult::Accepted;
	}

	bool IsValueOnlyStruct(const UStruct* Struct, TSet<const UStruct*>& Visited);

	/** False for any property that can hold a UObject or delegate, directly or through a container. */
	bool IsValueOnlyProperty(const FProperty* Property, TSet<const UStruct*>& Visited)
	{
		if (CastField<FObjectPropertyBase>(Property) || CastField<FInterfaceProperty>(Property)
			|| CastField<FDelegateProperty>(Property) || CastField<FMulticastDelegateProperty>(Property))
		{
			return false;
		}
		if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
		{
			return IsValueOnlyProperty(Array->Inner, Visited);
		}
		if (const FSetProperty* Set = CastField<FSetProperty>(Property))
		{
			return IsValueOnlyProperty(Set->ElementProp, Visited);
		}
		if (const FMapProperty* Map = CastField<FMapProperty>(Property))
		{
			return IsValueOnlyProperty(Map->KeyProp, Visited) && IsValueOnlyProperty(Map->ValueProp, Visited);
		}
		if (const FOptionalProperty* Optional = CastField<FOptionalProperty>(Property))
		{
			return IsValueOnlyProperty(Optional->GetValueProperty(), Visited);
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			return IsValueOnlyStruct(StructProperty->Struct, Visited);
		}
		return true;
	}

	bool IsValueOnlyStruct(const UStruct* Struct, TSet<const UStruct*>& Visited)
	{
		if (!Struct)
		{
			return true;
		}
		bool bAlreadyVisited = false;
		Visited.Add(Struct, &bAlreadyVisited);
		if (bAlreadyVisited)
		{
			return true;
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (!IsValueOnlyProperty(*It, Visited))
			{
				return false;
			}
		}
		return true;
	}

	bool AreStructsValueOnly(const FADSKNodeDescriptor& Descriptor)
	{
		TSet<const UStruct*> Visited;
		return IsValueOnlyStruct(Descriptor.ConfigStruct, Visited) && IsValueOnlyStruct(Descriptor.MemoryStruct, Visited)
			&& IsValueOnlyStruct(Descriptor.StateStruct, Visited);
	}

	int32 CompareNames(const FString& A, const FString& B)
	{
		return A.Compare(B, ESearchCase::CaseSensitive);
	}

	const TCHAR* KindName(EADSKNodeKind Kind)
	{
		switch (Kind)
		{
		case EADSKNodeKind::Leaf:
			return TEXT("leaf");
		case EADSKNodeKind::Sequence:
			return TEXT("sequence");
		case EADSKNodeKind::Guard:
			return TEXT("guard");
		case EADSKNodeKind::Selector:
			return TEXT("selector");
		case EADSKNodeKind::Emit:
			return TEXT("emit");
		case EADSKNodeKind::Scope:
			return TEXT("scope");
		case EADSKNodeKind::Parallel:
			return TEXT("parallel");
		case EADSKNodeKind::Composite:
			return TEXT("composite");
		case EADSKNodeKind::Sensor:
			return TEXT("sensor");
		case EADSKNodeKind::Set:
			return TEXT("set");
		case EADSKNodeKind::Concurrent:
			return TEXT("concurrent");
		}
		return TEXT("unknown");
	}

	const TCHAR* FieldTypeName(const FADSKFieldMap& Field)
	{
		switch (Field.Kind)
		{
		case EADSKFieldKind::ObjectPath:
			return TEXT("ObjectPath");
		case EADSKFieldKind::Choice:
			return TEXT("Choice");
		case EADSKFieldKind::Name:
			return TEXT("Name");
		case EADSKFieldKind::String:
			return TEXT("String");
		case EADSKFieldKind::ClassPath:
			return TEXT("ClassPath");
		case EADSKFieldKind::Tag:
			return TEXT("Tag");
		case EADSKFieldKind::Value:
			break;
		}
		return LexToString(Field.ValueType);
	}

	using FSchemaWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

	void WriteMax(FSchemaWriter& Writer, int32 Max)
	{
		if (Max == ADSK::UnboundedChildren)
		{
			Writer.WriteNull(TEXT("maxChildren"));
		}
		else
		{
			Writer.WriteValue(TEXT("maxChildren"), Max);
		}
	}

	void WriteFields(FSchemaWriter& Writer, const FADSKNodeDescriptor& Node)
	{
		TArray<const FADSKFieldMap*> Sorted;
		for (const FADSKFieldMap& Field : Node.Fields)
		{
			Sorted.Add(&Field);
		}
		Sorted.Sort([](const FADSKFieldMap& A, const FADSKFieldMap& B) { return CompareNames(A.XmlName, B.XmlName) < 0; });
		Writer.WriteArrayStart(TEXT("fields"));
		for (const FADSKFieldMap* Field : Sorted)
		{
			Writer.WriteObjectStart();
			Writer.WriteValue(TEXT("name"), Field->XmlName);
			Writer.WriteValue(TEXT("type"), FString(FieldTypeName(*Field)));
			Writer.WriteValue(TEXT("default"), Field->DefaultValue);
			if (Field->Kind == EADSKFieldKind::Choice)
			{
				Writer.WriteArrayStart(TEXT("values"));
				for (const FString& Name : Field->ChoiceNames)
				{
					Writer.WriteValue(Name);
				}
				Writer.WriteArrayEnd();
			}
			if (Field->MetaClass)
			{
				Writer.WriteValue(TEXT("metaClass"), Field->MetaClass->GetPathName());
			}
			Writer.WriteObjectEnd();
		}
		Writer.WriteArrayEnd();
	}

	void WritePorts(FSchemaWriter& Writer, const FADSKNodeDescriptor& Node)
	{
		TArray<const FADSKPortDescriptor*> Sorted;
		for (const FADSKPortDescriptor& Port : Node.Ports)
		{
			Sorted.Add(&Port);
		}
		Sorted.Sort([](const FADSKPortDescriptor& A, const FADSKPortDescriptor& B) { return CompareNames(A.Name, B.Name) < 0; });
		Writer.WriteArrayStart(TEXT("ports"));
		for (const FADSKPortDescriptor* Port : Sorted)
		{
			Writer.WriteObjectStart();
			Writer.WriteValue(TEXT("name"), Port->Name);
			Writer.WriteValue(TEXT("type"), FString(LexToString(Port->Type)));
			Writer.WriteValue(TEXT("direction"), Port->Direction == EADSKPortDirection::In ? TEXT("in") : TEXT("out"));
			Writer.WriteObjectEnd();
		}
		Writer.WriteArrayEnd();
	}

	void WriteIntrinsicAttributes(FSchemaWriter& Writer, const FADSKNodeDescriptor& Node)
	{
		Writer.WriteArrayStart(TEXT("attributes"));
		if (Node.Kind == EADSKNodeKind::Guard)
		{
			for (const TCHAR* Name : {TEXT("is"), TEXT("below"), TEXT("above")})
			{
				Writer.WriteObjectStart(); Writer.WriteValue(TEXT("name"), Name); Writer.WriteValue(TEXT("required"), false); Writer.WriteObjectEnd();
			}
		}
		if (Node.Kind == EADSKNodeKind::Emit) { Writer.WriteObjectStart(); Writer.WriteValue(TEXT("name"), TEXT("event")); Writer.WriteValue(TEXT("required"), true); Writer.WriteObjectEnd(); }
		if (Node.Kind == EADSKNodeKind::Set)
		{
			for (const TPair<const TCHAR*, bool> Attribute : {TPair<const TCHAR*, bool>(TEXT("to"), true), TPair<const TCHAR*, bool>(TEXT("value"), false), TPair<const TCHAR*, bool>(TEXT("from"), false)})
			{
				Writer.WriteObjectStart(); Writer.WriteValue(TEXT("name"), Attribute.Key); Writer.WriteValue(TEXT("required"), Attribute.Value); Writer.WriteObjectEnd();
			}
		}
		Writer.WriteArrayEnd();
	}

	void WriteNode(FSchemaWriter& Writer, const FADSKNodeDescriptor& Node)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("name"), Node.Name);
		Writer.WriteValue(TEXT("version"), Node.SchemaVersion);
		Writer.WriteValue(TEXT("kind"), KindName(Node.Kind));
		if (Node.SelectorPolicy == EADSKSelectorPolicy::Reactive)
		{
			Writer.WriteValue(TEXT("reactive"), true);
		}
		if (Node.SelectorPolicy == EADSKSelectorPolicy::Utility)
		{
			Writer.WriteValue(TEXT("utility"), true);
		}
		if (Node.OnScore)
		{
			Writer.WriteValue(TEXT("scores"), true);
		}
		if (Node.OnCheck)
		{
			Writer.WriteValue(TEXT("checks"), true);
		}
		Writer.WriteValue(TEXT("minChildren"), Node.MinChildren);
		WriteMax(Writer, Node.MaxChildren);
		WriteFields(Writer, Node);
		WritePorts(Writer, Node);
		WriteIntrinsicAttributes(Writer, Node);
		// Written only when present, so nodes without params keep their schema text.
		if (!Node.ParamsProperty.IsNone())
		{
			Writer.WriteValue(TEXT("params"), true);
		}
		if (Node.bTakesArgs)
		{
			Writer.WriteValue(TEXT("args"), true);
		}
		Writer.WriteObjectEnd();
	}
}

using namespace ADSKNodeRegistryPrivate;

const TCHAR* LexToString(EADSKRegisterResult Result)
{
	switch (Result)
	{
	case EADSKRegisterResult::Accepted:
		return TEXT("Accepted");
	case EADSKRegisterResult::Frozen:
		return TEXT("Frozen");
	case EADSKRegisterResult::InvalidName:
		return TEXT("InvalidName");
	case EADSKRegisterResult::DuplicateName:
		return TEXT("DuplicateName");
	case EADSKRegisterResult::ReservedName:
		return TEXT("ReservedName");
	case EADSKRegisterResult::InvalidArity:
		return TEXT("InvalidArity");
	case EADSKRegisterResult::InvalidFieldName:
		return TEXT("InvalidFieldName");
	case EADSKRegisterResult::DuplicateField:
		return TEXT("DuplicateField");
	case EADSKRegisterResult::MissingProperty:
		return TEXT("MissingProperty");
	case EADSKRegisterResult::UnsupportedFieldType:
		return TEXT("UnsupportedFieldType");
	case EADSKRegisterResult::InvalidPortName:
		return TEXT("InvalidPortName");
	case EADSKRegisterResult::DuplicatePort:
		return TEXT("DuplicatePort");
	case EADSKRegisterResult::PortFieldCollision:
		return TEXT("PortFieldCollision");
	case EADSKRegisterResult::UnsafeStruct:
		return TEXT("UnsafeStruct");
	case EADSKRegisterResult::UnsupportedPortType:
		return TEXT("UnsupportedPortType");
	case EADSKRegisterResult::InvalidParamsProperty:
		return TEXT("InvalidParamsProperty");
	}
	return TEXT("Unknown");
}

const FADSKFieldMap* FADSKNodeDescriptor::FindField(FStringView XmlName) const
{
	return Fields.FindByPredicate(
		[XmlName](const FADSKFieldMap& Field) { return XmlName.Equals(Field.XmlName, ESearchCase::CaseSensitive); });
}

int32 FADSKNodeDescriptor::FindPort(FStringView PortName) const
{
	return Ports.IndexOfByPredicate(
		[PortName](const FADSKPortDescriptor& Port) { return PortName.Equals(Port.Name, ESearchCase::CaseSensitive); });
}

TSharedRef<FADSKNodeRegistry> FADSKNodeRegistry::Create()
{
	ADSK_CHECK_GAME_THREAD();
	return MakeShareable(new FADSKNodeRegistry());
}

TSharedRef<const FADSKNodeRegistry> FADSKNodeRegistry::GetDefault()
{
	ADSK_CHECK_GAME_THREAD();
	return FModuleManager::GetModuleChecked<FAdamskAIModule>(TEXT("AdamskAI")).GetRegistry();
}

EADSKRegisterResult FADSKNodeRegistry::Register(FADSKNodeDescriptor Descriptor)
{
	ADSK_CHECK_GAME_THREAD();
	const EADSKRegisterResult Result = Admit(Descriptor);
	if (Result == EADSKRegisterResult::Accepted)
	{
		Descriptors.Add(MakeUnique<FADSKNodeDescriptor>(MoveTemp(Descriptor)));
	}
	else
	{
		Rejections.Add(FString::Printf(TEXT("%s: %s"), *Descriptor.Name, LexToString(Result)));
	}
	return Result;
}

EADSKRegisterResult FADSKNodeRegistry::Admit(FADSKNodeDescriptor& Descriptor) const
{
	if (bFrozen)
	{
		return EADSKRegisterResult::Frozen;
	}
	if (!IsNodeName(Descriptor.Name))
	{
		return EADSKRegisterResult::InvalidName;
	}
	if (Find(Descriptor.Name))
	{
		return EADSKRegisterResult::DuplicateName;
	}
	if (IsReservedName(Descriptor.Name))
	{
		return EADSKRegisterResult::ReservedName;
	}
	if (!HasArityShape(Descriptor))
	{
		return EADSKRegisterResult::InvalidArity;
	}
	EADSKRegisterResult Result = ResolveFields(Descriptor);
	if (Result == EADSKRegisterResult::Accepted)
	{
		Result = CheckPorts(Descriptor);
	}
	if (Result == EADSKRegisterResult::Accepted)
	{
		Result = CheckParamsProperty(Descriptor);
	}
	if (Result != EADSKRegisterResult::Accepted)
	{
		return Result;
	}
	if (!AreStructsValueOnly(Descriptor))
	{
		return EADSKRegisterResult::UnsafeStruct;
	}
	return EADSKRegisterResult::Accepted;
}

void FADSKNodeRegistry::Freeze()
{
	ADSK_CHECK_GAME_THREAD();
	if (bFrozen)
	{
		return;
	}
	bFrozen = true;
	const FString Schema = ExportSchemaJson();
	FTCHARToUTF8 Utf8(*Schema);
	FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), CatalogueFingerprint.Hash);
}

const FSHAHash& FADSKNodeRegistry::GetCatalogueFingerprint() const
{
	// No thread check: a frozen registry never changes, and the compiler reads it from compile tasks.
	checkf(bFrozen, TEXT("catalogue fingerprint requires a frozen registry"));
	return CatalogueFingerprint;
}

const FADSKNodeDescriptor* FADSKNodeRegistry::Find(FStringView NodeName) const
{
	for (const TUniquePtr<FADSKNodeDescriptor>& Descriptor : Descriptors)
	{
		if (NodeName.Equals(Descriptor->Name, ESearchCase::CaseSensitive))
		{
			return Descriptor.Get();
		}
	}
	return nullptr;
}

TArray<const FADSKNodeDescriptor*> FADSKNodeRegistry::GetDescriptors() const
{
	TArray<const FADSKNodeDescriptor*> Out;
	Out.Reserve(Descriptors.Num());
	for (const TUniquePtr<FADSKNodeDescriptor>& Descriptor : Descriptors) Out.Add(Descriptor.Get());
	return Out;
}

FString FADSKNodeRegistry::ExportSchemaJson() const
{
	ADSK_CHECK_GAME_THREAD();
	if (!ensureMsgf(bFrozen, TEXT("ExportSchemaJson requires a frozen registry")))
	{
		return FString();
	}
	TArray<const FADSKNodeDescriptor*> Sorted;
	for (const TUniquePtr<FADSKNodeDescriptor>& Descriptor : Descriptors)
	{
		Sorted.Add(Descriptor.Get());
	}
	Sorted.Sort([](const FADSKNodeDescriptor& A, const FADSKNodeDescriptor& B) { return CompareNames(A.Name, B.Name) < 0; });

	FString Output;
	const TSharedRef<FSchemaWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("schema"), 1);
	Writer->WriteArrayStart(TEXT("nodes"));
	for (const FADSKNodeDescriptor* Node : Sorted)
	{
		WriteNode(*Writer, *Node);
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return Output;
}
