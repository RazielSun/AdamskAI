// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Compiler/ADSKCompilerInternal.h"

using namespace ADSKCompilerInternal;

const FADSKXmlAttribute* ADSKCompilerInternal::FindCompareAttribute(FCompileContext& Context, const FADSKXmlElement& Element, EADSKCompareOp& OutOp, const FString& NodeId)
{
	const FADSKXmlAttribute* Found = nullptr;
	for (const FADSKXmlAttribute& Attribute : Element.Attributes)
	{
		for (const EADSKCompareOp Op : {EADSKCompareOp::Equal, EADSKCompareOp::Below, EADSKCompareOp::Above})
		{
			if (!Attribute.Name.Equals(ADSK::CompareOpAttribute(Op), ESearchCase::CaseSensitive))
			{
				continue;
			}
			if (Found)
			{
				Context.Error(EADSKDiagnosticCode::BadArity, Element.Line, TEXT("is, below and above conflict"), NodeId);
				return nullptr;
			}
			Found = &Attribute;
			OutOp = Op;
		}
	}
	return Found;
}

bool ADSKCompilerInternal::ParseCompareOperand(FCompileContext& Context, const FSlot& Slot, EADSKCompareOp Op, const FString& Text, int32 Line,
	const FString& NodeId, FADSKValue& OutValue, int32& OutSlot)
{
	OutSlot = INDEX_NONE;
	if (Op == EADSKCompareOp::Equal)
	{
		if (Slot.Type == EADSKValueType::Int)
		{
			if (ADSK::TryParseValue(EADSKValueType::Int, Text, OutValue)) return true;
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Line, TEXT("invalid condition value"), NodeId);
			return false;
		}
		if (Slot.Type == EADSKValueType::Enum)
		{
			const int32 Value = Context.Enums[Slot.Enum].Values.IndexOfByPredicate([&Text](const FString& V) { return V.Equals(Text, ESearchCase::CaseSensitive); });
			if (Value != INDEX_NONE)
			{
				OutValue = FADSKValue::MakeEnum(Slot.Enum, Value);
				return true;
			}
			Context.Error(EADSKDiagnosticCode::UnknownEnumValue, Line, TEXT("unknown enum value"), NodeId);
			return false;
		}
		if (Slot.Type == EADSKValueType::Object)
		{
			// `none`, or another Object slot: "is the target still the one we chose".
			if (ADSK::TryParseValue(EADSKValueType::Object, Text, OutValue)) return true;
			const int32 Operand = Context.FindSlot(Text);
			if (Operand != INDEX_NONE && Context.Slots[Operand].Type == EADSKValueType::Object)
			{
				OutSlot = Operand;
				return true;
			}
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Line, TEXT("an Object compares with none or an Object slot"), NodeId);
			return false;
		}
		Context.Error(EADSKDiagnosticCode::TypeMismatch, Line, TEXT("is needs Int, Enum or Object"), NodeId);
		return false;
	}
	if (Slot.Type != EADSKValueType::Int && Slot.Type != EADSKValueType::Float)
	{
		Context.Error(EADSKDiagnosticCode::TypeMismatch, Line, TEXT("below and above need Int or Float"), NodeId);
		return false;
	}
	if (ADSK::TryParseValue(Slot.Type, Text, OutValue))
	{
		return true;
	}
	const int32 Operand = Context.FindSlot(Text);
	if (Operand != INDEX_NONE && Context.Slots[Operand].Type == Slot.Type)
	{
		OutSlot = Operand;
		return true;
	}
	Context.Error(EADSKDiagnosticCode::TypeMismatch, Line, TEXT("operand is neither a literal nor a slot of the condition's type"), NodeId);
	return false;
}

void ADSKCompilerInternal::ParseConditionAttributes(FCompileContext& Context, const FADSKXmlElement& Element, const FString& NodeId,
	FADSKCondition& OutCondition, bool& bOutHas, bool& bOutSyntaxOk)
{
	bOutHas = false;
	const FString* If = Element.FindAttribute(TEXT("if"));
	const FString* Unless = Element.FindAttribute(TEXT("unless"));
	EADSKCompareOp Op = EADSKCompareOp::Equal;
	const int32 DiagnosticsBefore = Context.Diagnostics.Num();
	const FADSKXmlAttribute* Compare = FindCompareAttribute(Context, Element, Op, NodeId);
	if (Context.Diagnostics.Num() != DiagnosticsBefore)
	{
		bOutSyntaxOk = false;
		return;
	}
	if (If && Unless)
	{
		Context.Error(EADSKDiagnosticCode::BadArity, Element.Line, TEXT("if and unless conflict"), NodeId);
		bOutSyntaxOk = false;
		return;
	}
	if (Compare && !If && !Unless)
	{
		Context.Error(EADSKDiagnosticCode::MissingRequired, Element.Line, TEXT("is, below and above need if or unless"), NodeId);
		bOutSyntaxOk = false;
		return;
	}
	if (!If && !Unless)
	{
		return;
	}
	bOutHas = true;
	const FString& Name = If ? *If : *Unless;
	OutCondition.bNegated = Unless != nullptr;
	OutCondition.Slot = Context.FindSlot(Name);
	if (OutCondition.Slot == INDEX_NONE)
	{
		Context.Error(EADSKDiagnosticCode::UnresolvedReference, Element.Line,
			*FString::Printf(TEXT("unknown condition slot '%s'%s"), *Name, *DidYouMean(Name, Context.SlotNames())), NodeId);
		return;
	}
	const FSlot& Slot = Context.Slots[OutCondition.Slot];
	if (Compare)
	{
		OutCondition.bCompares = true;
		OutCondition.Op = Op;
		ParseCompareOperand(Context, Slot, Op, Compare->Value, Element.Line, NodeId, OutCondition.Operand, OutCondition.OperandSlot);
	}
	else if (Slot.Type != EADSKValueType::Bool)
	{
		Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("condition needs Bool"), NodeId);
	}
}

bool ADSKCompilerInternal::IsConditionElement(const FADSKXmlElement& Element)
{
	return Element.Name.Equals(TEXT("When"), ESearchCase::CaseSensitive) || Element.Name.Equals(TEXT("Any"), ESearchCase::CaseSensitive);
}

void ADSKCompilerInternal::ValidateConditionElement(FCompileContext& Context, const FADSKXmlElement& Element, const FString& NodeId,
	TArray<FADSKCondition>& OutTerms, bool& bOutSyntaxOk)
{
	if (Element.Name.Equals(TEXT("When"), ESearchCase::CaseSensitive))
	{
		RejectChildren(Context, Element);
		RejectUnknownAttributes(Context, Element, {TEXT("if"), TEXT("unless"), TEXT("is"), TEXT("below"), TEXT("above")});
		FADSKCondition Term;
		bool bHas = false;
		bool bTermSyntaxOk = true;
		ParseConditionAttributes(Context, Element, NodeId, Term, bHas, bTermSyntaxOk);
		if (bTermSyntaxOk && !bHas)
		{
			Context.Error(EADSKDiagnosticCode::MissingRequired, Element.Line, TEXT("When needs if or unless"), NodeId);
			bTermSyntaxOk = false;
		}
		bOutSyntaxOk = bOutSyntaxOk && bTermSyntaxOk;
		if (bTermSyntaxOk && Term.IsSet())
		{
			OutTerms.Add(Term);
		}
		return;
	}
	RejectUnknownAttributes(Context, Element, TConstArrayView<const TCHAR*>());
	const int32 FirstTerm = OutTerms.Num();
	int32 Whens = 0;
	for (const int32 Child : Element.Children)
	{
		const FADSKXmlElement& ChildElement = Context.Element(Child);
		if (ChildElement.Name.Equals(TEXT("When"), ESearchCase::CaseSensitive))
		{
			++Whens;
			ValidateConditionElement(Context, ChildElement, NodeId, OutTerms, bOutSyntaxOk);
		}
		else
		{
			Context.Error(EADSKDiagnosticCode::UnknownElement, ChildElement.Line, TEXT("Any takes only When children"), NodeId);
			bOutSyntaxOk = false;
		}
	}
	if (Whens < 2)
	{
		Context.Error(EADSKDiagnosticCode::BadArity, Element.Line, TEXT("Any needs at least two When children"), NodeId);
		bOutSyntaxOk = false;
	}
	for (int32 Term = FirstTerm; Term < OutTerms.Num() - 1; ++Term)
	{
		OutTerms[Term].bAnyWithNext = true;
	}
}
