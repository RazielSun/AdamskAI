// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Program/ADSKDefinition.h"

#include "UObject/UnrealType.h"

int32 FADSKDefinition::FindState(FStringView Id) const
{
	return States.IndexOfByPredicate([Id](const FADSKStateDefinition& S) { return Id.Equals(S.Id, ESearchCase::CaseSensitive); });
}

int32 FADSKDefinition::FindEvent(FStringView Name) const
{
	return Events.IndexOfByPredicate([Name](const FADSKEventDefinition& E) { return Name.Equals(E.Name, ESearchCase::CaseSensitive); });
}

int32 FADSKDefinition::FindSlot(FStringView Name) const
{
	return Slots.IndexOfByPredicate([Name](const FADSKSlotDefinition& S) { return Name.Equals(S.Name, ESearchCase::CaseSensitive); });
}

int32 FADSKDefinition::FindEnum(FStringView Name) const
{
	return Enums.IndexOfByPredicate([Name](const FADSKEnumDefinition& E) { return Name.Equals(E.Name, ESearchCase::CaseSensitive); });
}

TOptional<FADSKValue> FADSKDefinition::MakeEnumValue(FStringView EnumName, FStringView ValueName) const
{
	const int32 EnumIndex = FindEnum(EnumName);
	if (!Enums.IsValidIndex(EnumIndex) || EnumIndex > MAX_int16) return {};
	const FADSKEnumDefinition& Definition = Enums[EnumIndex];
	for (int32 Value = 0; Value < Definition.ValueCount; ++Value)
	{
		if (ValueName.Equals(EnumValueNames[Definition.FirstValue + Value], ESearchCase::CaseSensitive)) return FADSKValue::MakeEnum(EnumIndex, Value);
	}
	return {};
}

bool FADSKDefinition::ResolveEnumValue(const FADSKValue& Value, FString& OutTypeName, FString& OutValueName) const
{
	OutTypeName.Reset();
	OutValueName.Reset();
	if (Value.Type != EADSKValueType::Enum || !Enums.IsValidIndex(Value.Enum)) return false;
	const FADSKEnumDefinition& Definition = Enums[Value.Enum];
	if (Value.Int < 0 || Value.Int >= Definition.ValueCount) return false;
	OutTypeName = Definition.Name;
	OutValueName = EnumValueNames[Definition.FirstValue + Value.Int];
	return true;
}

int32 FADSKDefinition::FindNode(FName Id) const
{
	return Nodes.IndexOfByPredicate([Id](const FADSKNodeDefinition& N) { return N.Id == Id; });
}

TArray<FADSKObjectPathField> ADSK::GetObjectPathFields(const FADSKDefinition& Definition)
{
	TArray<FADSKObjectPathField> Result;
	const TConstArrayView<FADSKNodeDefinition> Nodes = Definition.GetNodes();
	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
	{
		const FADSKNodeDefinition& Node = Nodes[NodeIndex];
		if (!Node.Descriptor || !Node.Config.IsValid())
		{
			continue;
		}
		for (const FADSKFieldMap& Field : Node.Descriptor->Fields)
		{
			if ((Field.Kind != EADSKFieldKind::ObjectPath && Field.Kind != EADSKFieldKind::ClassPath) || !Field.Property)
			{
				continue;
			}
			FADSKObjectPathField& Entry = Result.AddDefaulted_GetRef();
			Entry.NodeIndex = NodeIndex;
			Entry.NodeId = Node.Id;
			Entry.Line = Node.Line;
			Entry.Path = *Field.Property->ContainerPtrToValuePtr<FSoftObjectPath>(Node.Config.GetMemory());
			Entry.bClass = Field.Kind == EADSKFieldKind::ClassPath;
			Entry.MetaClass = Field.MetaClass;
		}
	}
	return Result;
}

TArray<FADSKTagField> ADSK::GetTagFields(const FADSKDefinition& Definition)
{
	TArray<FADSKTagField> Result;
	const TConstArrayView<FADSKNodeDefinition> Nodes = Definition.GetNodes();
	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
	{
		const FADSKNodeDefinition& Node = Nodes[NodeIndex];
		if (!Node.Descriptor || !Node.Config.IsValid())
		{
			continue;
		}
		for (const FADSKFieldMap& Field : Node.Descriptor->Fields)
		{
			if (Field.Kind != EADSKFieldKind::Tag || !Field.Property)
			{
				continue;
			}
			const FName Tag = ADSK::GetTagName(*CastField<FStructProperty>(Field.Property)->Struct, Field.Property->ContainerPtrToValuePtr<void>(Node.Config.GetMemory()));
			if (!Tag.IsNone())
			{
				Result.Add({NodeIndex, Node.Id, Node.Line, Tag});
			}
		}
	}
	return Result;
}

bool ADSK::Compare(const FADSKValue& Current, EADSKCompareOp Op, const FADSKValue& Operand)
{
	if (Op == EADSKCompareOp::Equal)
	{
		return Current == Operand;
	}
	if (Current.Type != Operand.Type)
	{
		return false;
	}
	switch (Current.Type)
	{
	case EADSKValueType::Int:
		return Op == EADSKCompareOp::Below ? Current.Int < Operand.Int : Current.Int > Operand.Int;
	case EADSKValueType::Float:
		return Op == EADSKCompareOp::Below ? Current.Float < Operand.Float : Current.Float > Operand.Float;
	default:
		return false;
	}
}

bool ADSK::EvaluateCondition(const FADSKCondition& Condition, TConstArrayView<FADSKValue> Slots)
{
	if (!Condition.IsSet())
	{
		return true;
	}
	const FADSKValue& Current = Slots[Condition.Slot];
	const bool bPass = Condition.bCompares
		? Compare(Current, Condition.Op, Condition.OperandSlot != INDEX_NONE ? Slots[Condition.OperandSlot] : Condition.Operand)
		: Current.bBool;
	return bPass != Condition.bNegated;
}

bool ADSK::EvaluateTerms(TConstArrayView<FADSKCondition> Terms, TConstArrayView<FADSKValue> Slots)
{
	bool bGroupHolds = false;
	for (const FADSKCondition& Term : Terms)
	{
		bGroupHolds = bGroupHolds || EvaluateCondition(Term, Slots);
		if (!Term.bAnyWithNext)
		{
			if (!bGroupHolds)
			{
				return false;
			}
			bGroupHolds = false;
		}
	}
	return true;
}

bool ADSK::EvaluateTransitionCondition(const FADSKDefinition& Definition, const FADSKTransitionDefinition& Transition, TConstArrayView<FADSKValue> Slots)
{
	return EvaluateCondition(Transition.Condition, Slots)
		&& EvaluateTerms(Definition.GetConditionTerms().Slice(Transition.FirstTerm, Transition.TermCount), Slots);
}

const TCHAR* ADSK::CompareOpAttribute(EADSKCompareOp Op)
{
	switch (Op)
	{
	case EADSKCompareOp::Equal: return TEXT("is");
	case EADSKCompareOp::Below: return TEXT("below");
	case EADSKCompareOp::Above: return TEXT("above");
	}
	return TEXT("?");
}
