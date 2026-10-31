// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Runtime/ADSKNode.h"

#include "UObject/UnrealType.h"

FADSKNodeContext::FADSKNodeContext(const FADSKNodeContextParams& Params)
	: ConfigStruct(Params.ConfigStruct)
	, Config(Params.Config)
	, MemoryStruct(Params.MemoryStruct)
	, Memory(Params.Memory)
	, Ports(Params.Ports)
	, PortSlots(Params.PortSlots)
	, Args(Params.Args)
	, Slots(Params.Slots)
	, Delta(Params.Delta)
	, Handle(Params.Handle)
	, NodeId(Params.NodeId)
	, Host(Params.Host)
	, Random(Params.Random)
	, Time(Params.Time)
	, ChildCount(Params.ChildCount)
	, StateStruct(Params.StateStruct)
	, State(Params.State)
	, SlotSink(Params.SlotSink)
	, Node(Params.Node)
{
}

FADSKValue* FADSKNodeContext::ResolvePort(int32 PortIndex, EADSKPortDirection Direction, EADSKValueType Type) const
{
	if (!Ports.IsValidIndex(PortIndex) || !PortSlots.IsValidIndex(PortIndex))
	{
		return nullptr;
	}
	const FADSKPortDescriptor& Port = Ports[PortIndex];
	const int32 Slot = PortSlots[PortIndex];
	if (Port.Direction != Direction || Port.Type != Type || !Slots.IsValidIndex(Slot) || Slots[Slot].Type != Type)
	{
		return nullptr;
	}
	return &Slots[Slot];
}

bool FADSKNodeContext::ReadPort(int32 PortIndex, FADSKValue& OutValue) const
{
	if (!Ports.IsValidIndex(PortIndex))
	{
		return false;
	}
	const FADSKValue* Slot = ResolvePort(PortIndex, EADSKPortDirection::In, Ports[PortIndex].Type);
	if (!Slot)
	{
		return false;
	}
	OutValue = *Slot;
	return true;
}

bool FADSKNodeContext::ReadBool(int32 PortIndex, bool& bOutValue) const
{
	const FADSKValue* Slot = ResolvePort(PortIndex, EADSKPortDirection::In, EADSKValueType::Bool);
	if (Slot)
	{
		bOutValue = Slot->bBool;
	}
	return Slot != nullptr;
}

bool FADSKNodeContext::ReadFloat(int32 PortIndex, float& OutValue) const
{
	const FADSKValue* Slot = ResolvePort(PortIndex, EADSKPortDirection::In, EADSKValueType::Float);
	if (Slot)
	{
		OutValue = Slot->Float;
	}
	return Slot != nullptr;
}

bool FADSKNodeContext::ReadVector(int32 PortIndex, FVector& OutValue) const
{
	const FADSKValue* Slot = ResolvePort(PortIndex, EADSKPortDirection::In, EADSKValueType::Vector);
	if (Slot)
	{
		OutValue = Slot->Vector;
	}
	return Slot != nullptr;
}

bool FADSKNodeContext::ReadInt(int32 PortIndex, int32& OutValue) const
{
	const FADSKValue* Slot = ResolvePort(PortIndex, EADSKPortDirection::In, EADSKValueType::Int);
	if (Slot)
	{
		OutValue = Slot->Int;
	}
	return Slot != nullptr;
}

bool FADSKNodeContext::ReadObject(int32 PortIndex, uint64& OutId) const
{
	const FADSKValue* Slot = ResolvePort(PortIndex, EADSKPortDirection::In, EADSKValueType::Object);
	if (Slot)
	{
		OutId = Slot->ObjectId;
	}
	return Slot != nullptr;
}

bool FADSKNodeContext::WritePort(int32 PortIndex, const FADSKValue& Value)
{
	FADSKValue* Slot = ResolvePort(PortIndex, EADSKPortDirection::Out, Value.Type);
	if (Slot && *Slot != Value)
	{
		*Slot = Value;
		if (SlotSink)
		{
			SlotSink->SlotWritten(Node, PortSlots[PortIndex], Handle);
		}
	}
	return Slot != nullptr;
}

bool FADSKNodeContext::WriteInt(int32 PortIndex, int32 Value)
{
	return WritePort(PortIndex, FADSKValue::MakeInt(Value));
}

TArray<FADSKNamedValue> FADSKNodeContext::ReadArgs() const
{
	TArray<FADSKNamedValue> Values;
	Values.Reserve(Args.Num());
	for (const FADSKNodeArg& Arg : Args)
	{
		if (Slots.IsValidIndex(Arg.Slot))
		{
			Values.Add({Arg.Name, Slots[Arg.Slot]});
		}
	}
	return Values;
}

namespace ADSKNodePrivate
{
	const FNameProperty* FindTagNameProperty(const UScriptStruct& TagStruct)
	{
		return CastField<FNameProperty>(TagStruct.FindPropertyByName(TEXT("TagName")));
	}
}

bool ADSK::IsGameplayTagStruct(const UScriptStruct* Struct)
{
	return Struct && Struct->GetFName() == TEXT("GameplayTag") && Struct->GetOuter() && Struct->GetOuter()->GetFName() == TEXT("/Script/GameplayTags")
		&& ADSKNodePrivate::FindTagNameProperty(*Struct);
}

FName ADSK::GetTagName(const UScriptStruct& TagStruct, const void* Value)
{
	return ADSKNodePrivate::FindTagNameProperty(TagStruct)->GetPropertyValue_InContainer(Value);
}

void ADSK::SetTagName(const UScriptStruct& TagStruct, void* Value, FName TagName)
{
	ADSKNodePrivate::FindTagNameProperty(TagStruct)->SetPropertyValue_InContainer(Value, TagName);
}
