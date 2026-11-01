// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Program/ADSKNormalizedDump.h"

#include "Program/ADSKDefinition.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "UObject/EnumProperty.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace ADSKNormalizedDumpPrivate
{
	FString FormatFloat(float Value)
	{
		return FString::Printf(TEXT("%.9g"), Value);
	}

	FString FormatVector(const FVector& Value)
	{
		return FString::Printf(TEXT("%s,%s,%s"), *FormatFloat(static_cast<float>(Value.X)),
			*FormatFloat(static_cast<float>(Value.Y)), *FormatFloat(static_cast<float>(Value.Z)));
	}

	FString FormatValue(const FADSKDefinition& Definition, const FADSKValue& Value)
	{
		switch (Value.Type)
		{
		case EADSKValueType::Bool:
			return Value.bBool ? TEXT("true") : TEXT("false");
		case EADSKValueType::Float:
			return FormatFloat(Value.Float);
		case EADSKValueType::Vector:
			return FormatVector(Value.Vector);
		case EADSKValueType::Int:
			return FString::FromInt(Value.Int);
		case EADSKValueType::Enum:
		case EADSKValueType::Object:
			return ADSK::FormatValue(Definition, Value);
		}
		return TEXT("?");
	}

	/** Legacy `is` with a literal keeps its frozen text; newer forms name the operator (` below=3`). */
	FString FormatComparison(const FADSKDefinition& Definition, EADSKCompareOp Op, const FADSKValue& Value, int32 Slot, const TCHAR* LegacyPrefix)
	{
		if (Op == EADSKCompareOp::Equal && Slot == INDEX_NONE)
		{
			return LegacyPrefix + FormatValue(Definition, Value);
		}
		const FString Operand = Slot != INDEX_NONE ? FString::Printf(TEXT("slot=%d"), Slot) : TEXT("=") + FormatValue(Definition, Value);
		return FString::Printf(TEXT(" %s%s%s"), ADSK::CompareOpAttribute(Op), Slot != INDEX_NONE ? TEXT(" ") : TEXT(""), *Operand);
	}

	/** Fields that postdate the frozen v1 tables, with the default that keeps an old file's dump unchanged. */
	bool IsLegacyDefault(const FADSKNodeDescriptor& Descriptor, const FADSKFieldMap& Field, const FString& Formatted)
	{
		struct FLegacyField { const TCHAR* Node; const TCHAR* Field; const TCHAR* Default; };
		static const FLegacyField LegacyFields[] = {{TEXT("Guard"), TEXT("invert"), TEXT("false")}, {TEXT("Wait"), TEXT("deviation"), TEXT("0")},
			{TEXT("RunEQS"), TEXT("pick"), TEXT("Best")}, {TEXT("MoveTo"), TEXT("repathDistance"), TEXT("0")},
			{TEXT("MoveTo"), TEXT("allowPartialPath"), TEXT("true")}, {TEXT("MoveToActor"), TEXT("allowPartialPath"), TEXT("true")},
			{TEXT("MoveTo"), TEXT("strafe"), TEXT("true")}, {TEXT("MoveToActor"), TEXT("strafe"), TEXT("true")}, {TEXT("TimeLimit"), TEXT("deviation"), TEXT("0")}};
		for (const FLegacyField& Legacy : LegacyFields)
		{
			if (Descriptor.Name == Legacy.Node && Field.XmlName == Legacy.Field && Formatted == Legacy.Default)
			{
				return true;
			}
		}
		return false;
	}

	FString FormatField(const FADSKFieldMap& Field, const void* Config)
	{
		const void* Value = Field.Property->ContainerPtrToValuePtr<void>(Config);
		if (Field.Kind == EADSKFieldKind::ObjectPath || Field.Kind == EADSKFieldKind::ClassPath)
		{
			return static_cast<const FSoftObjectPath*>(Value)->ToString();
		}
		if (Field.Kind == EADSKFieldKind::Tag)
		{
			const FName Tag = ADSK::GetTagName(*CastField<FStructProperty>(Field.Property)->Struct, Value);
			return Tag.IsNone() ? FString() : Tag.ToString();
		}
		if (Field.Kind == EADSKFieldKind::Choice)
		{
			const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Field.Property);
			const int64 Enumerator = EnumProperty ? EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value)
				: CastField<FByteProperty>(Field.Property)->GetSignedIntPropertyValue(Value);
			return Field.Enum->GetNameStringByValue(Enumerator);
		}
		if (Field.Kind == EADSKFieldKind::Name)
		{
			return static_cast<const FName*>(Value)->ToString();
		}
		if (Field.Kind == EADSKFieldKind::String)
		{
			return *static_cast<const FString*>(Value);
		}
		switch (Field.ValueType)
		{
		case EADSKValueType::Bool:
			return CastField<FBoolProperty>(Field.Property)->GetPropertyValue(Value) ? TEXT("true") : TEXT("false");
		case EADSKValueType::Float:
			return FormatFloat(CastField<FFloatProperty>(Field.Property)->GetPropertyValue(Value));
		case EADSKValueType::Vector:
			return FormatVector(*static_cast<const FVector*>(Value));
		case EADSKValueType::Int:
			return FString::FromInt(CastField<FIntProperty>(Field.Property)->GetPropertyValue(Value));
		case EADSKValueType::Enum:
			break;
		}
		return TEXT("?");
	}

	void Line(FString& Out, const FString& Text)
	{
		Out += Text;
		Out += TEXT("\n");
	}

	void DumpSlotsAndEvents(const FADSKDefinition& Definition, FString& Out)
	{
		if (!Definition.GetEnums().IsEmpty())
		{
			Line(Out, TEXT("[enums]"));
			int32 EnumIndex = 0;
			for (const FADSKEnumDefinition& Enum : Definition.GetEnums())
			{
				Line(Out, FString::Printf(TEXT("%d %s values=%d+%d"), EnumIndex++, *Enum.Name, Enum.FirstValue, Enum.ValueCount));
			}
			Line(Out, TEXT("[enum-values]"));
			int32 ValueIndex = 0;
			for (const FString& Value : Definition.GetEnumValueNames())
			{
				Line(Out, FString::Printf(TEXT("%d %s"), ValueIndex++, *Value));
			}
		}
		Line(Out, TEXT("[slots]"));
		int32 Index = 0;
		for (const FADSKSlotDefinition& Slot : Definition.GetSlots())
		{
			FString SlotLine = FString::Printf(TEXT("%d %s %s %s required=%s default=%s"), Index++, *Slot.Name, LexToString(Slot.Type),
				Slot.bIsInput ? TEXT("input") : TEXT("local"), Slot.bRequired ? TEXT("true") : TEXT("false"),
				*FormatValue(Definition, Slot.Default));
			if (Slot.Type == EADSKValueType::Enum)
			{
				SlotLine += FString::Printf(TEXT(" enum=%d"), Slot.Enum);
			}
			Line(Out, SlotLine);
		}
		Line(Out, TEXT("[events]"));
		Index = 0;
		for (const FADSKEventDefinition& Event : Definition.GetEvents())
		{
			FString EventLine = FString::Printf(TEXT("%d %s priority=%d reserved=%s params=%d+%d"), Index++, *Event.Name, Event.Priority,
				Event.bReserved ? TEXT("true") : TEXT("false"), Event.FirstParam, Event.ParamCount);
			if (Event.ChangeSlot != INDEX_NONE)
			{
				EventLine += FString::Printf(TEXT(" change=%d"), Event.ChangeSlot);
			}
			Line(Out, EventLine);
		}
		Line(Out, TEXT("[params]"));
		Index = 0;
		for (const FADSKEventParamDefinition& Param : Definition.GetEventParams())
		{
			FString ParamLine = FString::Printf(TEXT("%d %s %s"), Index++, *Param.Name, LexToString(Param.Type));
			if (Param.Type == EADSKValueType::Enum)
			{
				ParamLine += FString::Printf(TEXT(" enum=%d"), Param.Enum);
			}
			Line(Out, ParamLine);
		}
	}

	void DumpStates(const FADSKDefinition& Definition, FString& Out)
	{
		Line(Out, TEXT("[states]"));
		int32 Index = 0;
		for (const FADSKStateDefinition& State : Definition.GetStates())
		{
			Line(Out, FString::Printf(TEXT("%d %s parent=%d children=%d+%d initial=%d depth=%d root=%d transitions=%d+%d"),
				Index++, *State.Id, State.Parent, State.FirstChild, State.ChildCount, State.Initial, State.Depth, State.RootNode,
				State.FirstTransition, State.TransitionCount));
			// Added after the legacy format: printed only when the state has tags, so older files dump unchanged.
			if (State.TagCount > 0)
			{
				Line(Out, TEXT("  tags ") + FString::JoinBy(Definition.GetStateTags().Slice(State.FirstTag, State.TagCount), TEXT(","), [](FName Tag) { return Tag.ToString(); }));
			}
			if (State.Select != EADSKStateSelect::Initial)
			{
				Line(Out, State.Select == EADSKStateSelect::Best ? TEXT("  select best") : TEXT("  select weighted"));
			}
			if (State.History != EADSKStateHistory::None)
			{
				Line(Out, State.History == EADSKStateHistory::Shallow ? TEXT("  history shallow") : TEXT("  history deep"));
			}
			if (State.bScored)
			{
				Line(Out, FString::Printf(TEXT("  score slot=%d weight=%s bias=%s"), State.ScoreSlot, *FormatFloat(State.ScoreWeight), *FormatFloat(State.ScoreBias)));
			}
		}
		Line(Out, TEXT("[state-children]"));
		Line(Out, FString::JoinBy(Definition.GetStateChildren(), TEXT(","), [](int32 Child) { return FString::FromInt(Child); }));
		Line(Out, TEXT("[transitions]"));
		Index = 0;
		for (const FADSKTransitionDefinition& Transition : Definition.GetTransitions())
		{
			FString TransitionLine = FString::Printf(TEXT("%d source=%d event=%d target=%d binds=%d+%d"), Index++,
				Transition.Source, Transition.Event, Transition.Target, Transition.FirstBind, Transition.BindCount);
			const FADSKCondition& Condition = Transition.Condition;
			if (Condition.IsSet())
			{
				TransitionLine += FString::Printf(TEXT(" condition=%d negated=%s"), Condition.Slot,
					Condition.bNegated ? TEXT("true") : TEXT("false"));
				if (Condition.bCompares)
				{
					TransitionLine += FormatComparison(Definition, Condition.Op, Condition.Operand, Condition.OperandSlot, TEXT(" value="));
				}
			}
			// Added after the legacy format, like tags.
			if (Transition.Chance < 1.f)
			{
				TransitionLine += FString::Printf(TEXT(" chance=%s"), *FormatFloat(Transition.Chance));
			}
			if (Transition.ChanceSlot != INDEX_NONE)
			{
				TransitionLine += FString::Printf(TEXT(" chance-slot=%d"), Transition.ChanceSlot);
			}
			if (Transition.TermCount > 0)
			{
				TransitionLine += FString::Printf(TEXT(" terms=%d+%d"), Transition.FirstTerm, Transition.TermCount);
			}
			Line(Out, TransitionLine);
		}
		Line(Out, TEXT("[binds]"));
		Index = 0;
		for (const FADSKBindDefinition& Bind : Definition.GetBinds())
		{
			Line(Out, FString::Printf(TEXT("%d param=%d slot=%d"), Index++, Bind.Param, Bind.Slot));
		}
	}

	/** Added after the legacy format: printed only when a file has `<When>`/`<Any>` terms. */
	void DumpConditionTerms(const FADSKDefinition& Definition, FString& Out)
	{
		if (Definition.GetConditionTerms().IsEmpty())
		{
			return;
		}
		Line(Out, TEXT("[condition-terms]"));
		int32 Index = 0;
		for (const FADSKCondition& Term : Definition.GetConditionTerms())
		{
			FString TermLine = FString::Printf(TEXT("%d slot=%d negated=%s"), Index++, Term.Slot, Term.bNegated ? TEXT("true") : TEXT("false"));
			if (Term.bCompares)
			{
				TermLine += FormatComparison(Definition, Term.Op, Term.Operand, Term.OperandSlot, TEXT(" value="));
			}
			if (Term.bAnyWithNext)
			{
				TermLine += TEXT(" any-with-next");
			}
			Line(Out, TermLine);
		}
	}

	void DumpNode(const FADSKDefinition& Definition, const FADSKNodeDefinition& Node, int32 Index, FString& Out)
	{
		const FADSKNodeDescriptor& Descriptor = *Node.Descriptor;
		Line(Out, FString::Printf(TEXT("%d %s %s parent=%d children=%d+%d ports=%d+%d"), Index, *Node.AuthoredId,
			*Descriptor.Name, Node.Parent, Node.FirstChild, Node.ChildCount, Node.FirstPortBinding, Node.PortCount));
		if (Node.EmitEvent != INDEX_NONE)
		{
			Line(Out, FString::Printf(TEXT("  emit event=%d"), Node.EmitEvent));
			for (int32 Arg = 0; Arg < Node.EmitArgCount; ++Arg)
			{
				const FADSKEmitArgDefinition& EmitArg = Definition.GetEmitArgs()[Node.FirstEmitArg + Arg];
				Line(Out, EmitArg.Slot != INDEX_NONE ? FString::Printf(TEXT("  emit-arg %d slot=%d"), Arg, EmitArg.Slot)
					: FString::Printf(TEXT("  emit-arg %d value=%s"), Arg, *FormatValue(Definition, EmitArg.Value)));
			}
		}
		if (Descriptor.Kind == EADSKNodeKind::Set)
		{
			Line(Out, Node.Assign.Source != INDEX_NONE ? FString::Printf(TEXT("  set slot=%d from=%d"), Node.Assign.Target, Node.Assign.Source)
				: FString::Printf(TEXT("  set slot=%d value=%s"), Node.Assign.Target, *FormatValue(Definition, Node.Assign.Value)));
		}
		for (int32 Arg = 0; Arg < Node.ArgCount; ++Arg)
		{
			const FADSKNodeArg& NodeArg = Definition.GetNodeArgs()[Node.FirstArg + Arg];
			Line(Out, FString::Printf(TEXT("  arg %s slot=%d"), *NodeArg.Name, NodeArg.Slot));
		}
		if (Node.Condition.bCompares)
		{
			Line(Out, TEXT("  compare") + FormatComparison(Definition, Node.Condition.Op, Node.Condition.Operand, Node.Condition.OperandSlot, TEXT("=")));
		}
		if (Node.TermCount > 0)
		{
			Line(Out, FString::Printf(TEXT("  terms=%d+%d"), Node.FirstTerm, Node.TermCount));
		}
		if (!Descriptor.ParamsProperty.IsNone() && Node.Config.IsValid())
		{
			const FArrayProperty* Array = CastField<FArrayProperty>(Descriptor.ConfigStruct->FindPropertyByName(Descriptor.ParamsProperty));
			for (const FADSKNamedParam& Param : *Array->ContainerPtrToValuePtr<TArray<FADSKNamedParam>>(Node.Config.GetMemory()))
			{
				// Float params keep the original line; the others name their type.
				const FString Value = Param.Type == EADSKParamType::Int ? FString::Printf(TEXT("Int:%d"), Param.IntValue)
					: Param.Type == EADSKParamType::Bool ? FString(Param.bBoolValue ? TEXT("Bool:true") : TEXT("Bool:false"))
					: FormatFloat(Param.Value);
				Line(Out, FString::Printf(TEXT("  param %s=%s"), *Param.Name.ToString(), *Value));
			}
		}
		for (int32 Port = 0; Port < Node.PortCount; ++Port)
		{
			const FADSKPortDescriptor& PortDescriptor = Descriptor.Ports[Port];
			Line(Out, FString::Printf(TEXT("  port %s %s %s slot=%d"), *PortDescriptor.Name, LexToString(PortDescriptor.Type),
				PortDescriptor.Direction == EADSKPortDirection::In ? TEXT("in") : TEXT("out"),
				Definition.GetPortBindings()[Node.FirstPortBinding + Port]));
		}
		if (!Node.Config.IsValid())
		{
			return;
		}
		for (const FADSKFieldMap& Field : Descriptor.Fields)
		{
			const FString Formatted = FormatField(Field, Node.Config.GetMemory());
			// Fields added after the legacy format are omitted while they hold their compatibility default.
			if (IsLegacyDefault(Descriptor, Field, Formatted))
			{
				continue;
			}
			Line(Out, FString::Printf(TEXT("  config %s=%s"), *Field.XmlName, *Formatted));
		}
	}
}

FString ADSKTest::DumpNormalizedTables(const FADSKDefinition& Definition)
{
	using namespace ADSKNormalizedDumpPrivate;
	FString Out;
	Line(Out, FString::Printf(TEXT("agent %s version=%d"), *Definition.GetAgentId(), Definition.GetLanguageVersion()));
	DumpSlotsAndEvents(Definition, Out);
	DumpStates(Definition, Out);
	Line(Out, TEXT("[trees]"));
	int32 Index = 0;
	for (const FADSKTreeDefinition& Tree : Definition.GetTrees())
	{
		Line(Out, FString::Printf(TEXT("%d %s root=%d"), Index++, *Tree.Id, Tree.RootNode));
	}
	Line(Out, TEXT("[nodes]"));
	Index = 0;
	for (const FADSKNodeDefinition& Node : Definition.GetNodes())
	{
		DumpNode(Definition, Node, Index++, Out);
	}
	Line(Out, TEXT("[node-children]"));
	Line(Out, FString::JoinBy(Definition.GetNodeChildren(), TEXT(","), [](int32 Child) { return FString::FromInt(Child); }));
	DumpConditionTerms(Definition, Out);
	return Out;
}
