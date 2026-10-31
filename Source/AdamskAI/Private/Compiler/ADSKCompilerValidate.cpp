// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Compiler/ADSKCompilerInternal.h"

namespace ADSKCompilerInternal
{
	FCompileContext::FCompileContext(const FADSKXmlDocument& InDocument, const TSharedRef<const FADSKNodeRegistry>& InRegistry,
		const FString& InSource, TArray<FADSKDiagnostic>& InDiagnostics)
		: Document(InDocument)
		, Registry(InRegistry)
		, Source(InSource)
		, Diagnostics(InDiagnostics)
	{
		for (const TCHAR* Name : {TEXT("tree.success"), TEXT("tree.failure")})
		{
			FEventInfo& Reserved = Events.AddDefaulted_GetRef();
			Reserved.Name = Name;
			Reserved.bReserved = true;
		}
	}

	void FCompileContext::Report(
		EADSKSeverity Severity, EADSKDiagnosticCode Code, int32 Line, const FString& NodeId, const TCHAR* Message)
	{
		FADSKDiagnostic& Diagnostic = Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Code = Code;
		Diagnostic.Severity = Severity;
		Diagnostic.Source = Source;
		Diagnostic.Line = Line;
		Diagnostic.NodeId = NodeId.IsEmpty() ? NAME_None : FName(*NodeId);
		Diagnostic.Message = Message;
	}

	void FCompileContext::Error(EADSKDiagnosticCode Code, int32 Line, const TCHAR* Message, const FString& NodeId)
	{
		Report(EADSKSeverity::Error, Code, Line, NodeId, Message);
	}

	int32 FCompileContext::FindSlot(const FString& Name) const
	{
		return Slots.IndexOfByPredicate([this, &Name](const FSlot& S)
		{
			return (S.FormalTree == INDEX_NONE || S.FormalTree == CurrentTree) && S.Name.Equals(Name, ESearchCase::CaseSensitive);
		});
	}

	int32 FCompileContext::FindEnum(const FString& Name) const
	{
		return Enums.IndexOfByPredicate([&Name](const FEnum& E) { return E.Name.Equals(Name, ESearchCase::CaseSensitive); });
	}

	int32 FCompileContext::FindEvent(const FString& Name) const
	{
		return Events.IndexOfByPredicate([&Name](const FEventInfo& E) { return E.Name.Equals(Name, ESearchCase::CaseSensitive); });
	}

	int32 FCompileContext::FindState(const FString& Id) const
	{
		return States.IndexOfByPredicate([&Id](const FState& S) { return S.Id.Equals(Id, ESearchCase::CaseSensitive); });
	}

	int32 FCompileContext::FindTree(const FString& Id) const
	{
		return Trees.IndexOfByPredicate([&Id](const FTree& T) { return T.Id.Equals(Id, ESearchCase::CaseSensitive); });
	}

	TArray<FString> FCompileContext::SlotNames() const
	{
		TArray<FString> Names;
		for (const FSlot& Slot : Slots)
		{
			if (Slot.FormalTree == INDEX_NONE || Slot.FormalTree == CurrentTree) { Names.Add(Slot.Name); }
		}
		return Names;
	}

	TArray<FString> FCompileContext::EventNames() const
	{
		TArray<FString> Names;
		for (const FEventInfo& Event : Events) { Names.Add(Event.Name); }
		return Names;
	}

	TArray<FString> FCompileContext::StateIds() const
	{
		TArray<FString> Ids;
		for (const FState& State : States) { Ids.Add(State.Id); }
		return Ids;
	}

	TArray<FString> FCompileContext::TreeIds() const
	{
		TArray<FString> Ids;
		for (const FTree& Tree : Trees) { Ids.Add(Tree.Id); }
		return Ids;
	}

	FString DidYouMean(FStringView Name, TConstArrayView<FString> Candidates)
	{
		// Levenshtein distance; a letter differing only in case costs one, so `moveto` still finds `MoveTo`.
		const auto Distance = [](FStringView A, FStringView B)
		{
			TArray<int32, TInlineAllocator<64>> Row;
			Row.SetNumUninitialized(B.Len() + 1);
			for (int32 J = 0; J <= B.Len(); ++J) { Row[J] = J; }
			for (int32 I = 1; I <= A.Len(); ++I)
			{
				int32 Diagonal = Row[0];
				Row[0] = I;
				for (int32 J = 1; J <= B.Len(); ++J)
				{
					const int32 Above = Row[J];
					Row[J] = FMath::Min3(Row[J] + 1, Row[J - 1] + 1, Diagonal + (A[I - 1] == B[J - 1] ? 0 : 1));
					Diagonal = Above;
				}
			}
			return Row[B.Len()];
		};
		const int32 Allowed = FMath::Max(1, Name.Len() / 3);
		int32 Best = Allowed + 1;
		const FString* Match = nullptr;
		for (const FString& Candidate : Candidates)
		{
			const int32 D = Distance(Name, Candidate);
			// A short name is close to everything; only a case slip counts for it.
			if (D > 0 && D < Best && (D < Candidate.Len() || Name.Equals(Candidate, ESearchCase::IgnoreCase)))
			{
				Best = D;
				Match = &Candidate;
			}
		}
		return Match ? FString::Printf(TEXT("; did you mean '%s'?"), **Match) : FString();
	}

	void RejectUnknownAttributes(FCompileContext& Context, const FADSKXmlElement& Element, TConstArrayView<const TCHAR*> Allowed)
	{
		for (const FADSKXmlAttribute& Attribute : Element.Attributes)
		{
			const bool bKnown = Allowed.ContainsByPredicate(
				[&Attribute](const TCHAR* Name) { return Attribute.Name.Equals(Name, ESearchCase::CaseSensitive); });
			if (!bKnown)
			{
				TArray<FString> Names;
				for (const TCHAR* Name : Allowed) { Names.Add(Name); }
				Context.Error(EADSKDiagnosticCode::UnknownAttribute, Element.Line,
					*FString::Printf(TEXT("unknown attribute '%s'%s"), *Attribute.Name, *DidYouMean(Attribute.Name, Names)));
			}
		}
	}

	const FString* RequireAttribute(FCompileContext& Context, const FADSKXmlElement& Element, const TCHAR* Name)
	{
		const FString* Value = Element.FindAttribute(Name);
		if (!Value)
		{
			Context.Error(EADSKDiagnosticCode::MissingRequired, Element.Line, *FString::Printf(TEXT("missing required attribute '%s'"), Name));
		}
		return Value;
	}

	void RejectChildren(FCompileContext& Context, const FADSKXmlElement& Element)
	{
		for (const int32 Child : Element.Children)
		{
			Context.Error(EADSKDiagnosticCode::UnknownElement, Context.Element(Child).Line, TEXT("unexpected child element"));
		}
	}

	bool ParseValueType(const FString& Text, EADSKValueType& OutType)
	{
		for (const EADSKValueType Candidate : {EADSKValueType::Bool, EADSKValueType::Float, EADSKValueType::Vector, EADSKValueType::Int, EADSKValueType::Object})
		{
			if (Text.Equals(LexToString(Candidate), ESearchCase::CaseSensitive))
			{
				OutType = Candidate;
				return true;
			}
		}
		return false;
	}
}

using namespace ADSKCompilerInternal;

namespace ADSKCompilerValidatePrivate
{
	enum ESectionRank { Enums, Inputs, Locals, Sensors, Events, StateMachine, Tree };
	int32 SectionRank(const FString& Name)
	{
		const TCHAR* Order[] = {TEXT("Enums"), TEXT("Inputs"), TEXT("Locals"), TEXT("Sensors"), TEXT("Events"), TEXT("StateMachine"), TEXT("Tree")};
		for (int32 Rank = 0; Rank < UE_ARRAY_COUNT(Order); ++Rank)
		{
			if (Name.Equals(Order[Rank], ESearchCase::CaseSensitive))
			{
				return Rank;
			}
		}
		return INDEX_NONE;
	}

	void ValidateEnums(FCompileContext& Context, const FADSKXmlElement& Section)
	{
		RejectUnknownAttributes(Context, Section, {});
		for (const int32 Child : Section.Children)
		{
			const FADSKXmlElement& Element = Context.Element(Child);
			if (!Element.Name.Equals(TEXT("Enum"), ESearchCase::CaseSensitive)) { Context.Error(EADSKDiagnosticCode::UnknownElement, Element.Line, TEXT("unexpected element")); continue; }
			RejectUnknownAttributes(Context, Element, {TEXT("name")});
			const FString* Name = RequireAttribute(Context, Element, TEXT("name"));
			if (!Name || !ADSK::IsIdentifier(*Name)) { if (Name) Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("invalid enum name")); continue; }
			EADSKValueType Builtin;
			if (Context.FindEnum(*Name) != INDEX_NONE || ParseValueType(*Name, Builtin) || Name->Equals(TEXT("Enum"), ESearchCase::CaseSensitive)) { Context.Error(EADSKDiagnosticCode::DuplicateId, Element.Line, TEXT("duplicate or builtin enum name")); continue; }
			if (Context.Enums.Num() >= 16) { Context.Error(EADSKDiagnosticCode::LimitExceeded, Element.Line, TEXT("more than 16 enums")); continue; }
			FEnum Enum; Enum.Name = *Name;
			for (const int32 ValueChild : Element.Children)
			{
				const FADSKXmlElement& Value = Context.Element(ValueChild);
				if (!Value.Name.Equals(TEXT("Value"), ESearchCase::CaseSensitive)) { Context.Error(EADSKDiagnosticCode::UnknownElement, Value.Line, TEXT("unexpected element")); continue; }
				RejectChildren(Context, Value); RejectUnknownAttributes(Context, Value, {TEXT("name")}); const FString* ValueName = RequireAttribute(Context, Value, TEXT("name"));
				if (!ValueName || !ADSK::IsIdentifier(*ValueName)) { if (ValueName) Context.Error(EADSKDiagnosticCode::TypeMismatch, Value.Line, TEXT("invalid enum value name")); continue; }
				if (Enum.Values.ContainsByPredicate([ValueName](const FString& Existing) { return Existing.Equals(*ValueName, ESearchCase::CaseSensitive); })) { Context.Error(EADSKDiagnosticCode::DuplicateId, Value.Line, TEXT("duplicate enum value")); continue; }
				if (Enum.Values.Num() >= 32) { Context.Error(EADSKDiagnosticCode::LimitExceeded, Value.Line, TEXT("more than 32 enum values")); continue; }
				Enum.Values.Add(*ValueName);
			}
			if (Enum.Values.IsEmpty()) Context.Error(EADSKDiagnosticCode::BadArity, Element.Line, TEXT("enum needs a value"));
			Context.Enums.Add(MoveTemp(Enum));
		}
	}

	void ValidateInput(FCompileContext& Context, const FADSKXmlElement& Element, FSlot& Slot, bool& bOk)
	{
		const FString* Required = Element.FindAttribute(TEXT("required"));
		const FString* Default = Element.FindAttribute(TEXT("default"));
		bool bHasDefault = false;
		if (Required && !ADSK::TryParseBool(*Required, Slot.bRequired))
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("required must be true or false"));
			bOk = false;
		}
		if (Default && Slot.Type == EADSKValueType::Enum)
		{
			const FEnum& Enum = Context.Enums[Slot.Enum];
			const int32 Value = Enum.Values.IndexOfByPredicate([Default](const FString& V) { return V.Equals(*Default, ESearchCase::CaseSensitive); });
			if (Value == INDEX_NONE) { Context.Error(EADSKDiagnosticCode::UnknownEnumValue, Element.Line, TEXT("unknown enum default")); bOk = false; }
			else { Slot.Default = FADSKValue::MakeEnum(Slot.Enum, Value); bHasDefault = true; }
		}
		else if (Default && !ADSK::TryParseValue(Slot.Type, *Default, Slot.Default))
		{
			Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("default does not match the declared type"));
			bOk = false;
		}
		else if (Default)
		{
			bHasDefault = true;
		}
		if (!bOk)
		{
			return;
		}
		if (Slot.bRequired && bHasDefault)
		{
			Context.Error(EADSKDiagnosticCode::BadArity, Element.Line, TEXT("an input is required or has a default, never both"));
		}
		else if (!Slot.bRequired && !bHasDefault && Slot.Type == EADSKValueType::Enum)
		{
			Slot.Default = FADSKValue::MakeEnum(Slot.Enum, 0);
		}
		else if (!Slot.bRequired && !bHasDefault)
		{
			Context.Error(EADSKDiagnosticCode::MissingRequired, Element.Line, TEXT("an input needs required=\"true\" or a default"));
		}
	}

	void ValidateSlot(FCompileContext& Context, const FADSKXmlElement& Element, bool bInput)
	{
		RejectChildren(Context, Element);
		const TCHAR* Input[] = {TEXT("name"), TEXT("type"), TEXT("required"), TEXT("default")};
		RejectUnknownAttributes(Context, Element, bInput ? TConstArrayView<const TCHAR*>(Input) : TConstArrayView<const TCHAR*>(Input, 2));
		const FString* Name = RequireAttribute(Context, Element, TEXT("name"));
		const FString* Type = RequireAttribute(Context, Element, TEXT("type"));
		if (!Name || !Type)
		{
			return;
		}
		FSlot Slot;
		Slot.Name = *Name;
		Slot.bInput = bInput;
		Slot.Line = Element.Line;
		if (!ParseValueType(*Type, Slot.Type))
		{
			if ((Slot.Enum = Context.FindEnum(*Type)) == INDEX_NONE) { Context.Error(EADSKDiagnosticCode::TypeMismatch, Element.Line, TEXT("unknown or unsupported slot type")); return; }
			Slot.Type = EADSKValueType::Enum;
		}
		Slot.Default = Slot.Type == EADSKValueType::Enum ? FADSKValue::MakeEnum(Slot.Enum, 0) : FADSKValue::MakeZero(Slot.Type);
		bool bOk = true;
		if (bInput)
		{
			ValidateInput(Context, Element, Slot, bOk);
		}
		if (Context.FindSlot(Slot.Name) != INDEX_NONE)
		{
			Context.Error(EADSKDiagnosticCode::DuplicateId, Element.Line, TEXT("duplicate slot name"));
			return;
		}
		Context.Slots.Add(MoveTemp(Slot));
	}

	void ValidateSlotSection(FCompileContext& Context, const FADSKXmlElement& Section, const TCHAR* ChildName, bool bInput)
	{
		RejectUnknownAttributes(Context, Section, TConstArrayView<const TCHAR*>());
		for (const int32 Child : Section.Children)
		{
			const FADSKXmlElement& Element = Context.Element(Child);
			if (Element.Name.Equals(ChildName, ESearchCase::CaseSensitive))
			{
				ValidateSlot(Context, Element, bInput);
			}
			else
			{
				Context.Error(EADSKDiagnosticCode::UnknownElement, Element.Line, TEXT("unexpected element"));
			}
		}
	}

	/** Strict int32: `-?digits`, inside range. */
	bool ParsePriority(const FString& Text, int32& OutValue, bool& bOutOfRange)
	{
		bOutOfRange = false;
		const int32 Start = Text.StartsWith(TEXT("-")) ? 1 : 0;
		if (Text.Len() == Start)
		{
			return false;
		}
		for (int32 Index = Start; Index < Text.Len(); ++Index)
		{
			if (!FChar::IsDigit(Text[Index]))
			{
				return false;
			}
		}
		const int64 Parsed = Text.Len() - Start > 12 ? MAX_int64 : FCString::Atoi64(*Text);
		if (Parsed > MAX_int32 || Parsed < MIN_int32)
		{
			bOutOfRange = true;
			return false;
		}
		OutValue = static_cast<int32>(Parsed);
		return true;
	}

	void ValidateEventParams(FCompileContext& Context, const FADSKXmlElement& Element, FEventInfo& Event)
	{
		for (const int32 Child : Element.Children)
		{
			const FADSKXmlElement& Param = Context.Element(Child);
			if (!Param.Name.Equals(TEXT("Param"), ESearchCase::CaseSensitive))
			{
				Context.Error(EADSKDiagnosticCode::UnknownElement, Param.Line, TEXT("unexpected element"));
				continue;
			}
			RejectChildren(Context, Param);
			RejectUnknownAttributes(Context, Param, {TEXT("name"), TEXT("type")});
			const FString* Name = RequireAttribute(Context, Param, TEXT("name"));
			const FString* Type = RequireAttribute(Context, Param, TEXT("type"));
			FParam Parsed;
			if (!Name || !Type)
			{
				continue;
			}
			Parsed.Name = *Name;
			const bool bBuiltin = ParseValueType(*Type, Parsed.Type);
			if (!bBuiltin && (Parsed.Enum = Context.FindEnum(*Type)) != INDEX_NONE)
			{
				Parsed.Type = EADSKValueType::Enum;
			}
			if (!bBuiltin && Parsed.Enum == INDEX_NONE)
			{
				Context.Error(EADSKDiagnosticCode::TypeMismatch, Param.Line, TEXT("type must be Bool, Float, Vector, Int, Object or a declared enum"));
			}
			else if (Event.Params.ContainsByPredicate([&Parsed](const FParam& P) { return P.Name.Equals(Parsed.Name, ESearchCase::CaseSensitive); }))
			{
				Context.Error(EADSKDiagnosticCode::DuplicateId, Param.Line, TEXT("duplicate event parameter"));
			}
			else
			{
				Event.Params.Add(MoveTemp(Parsed));
			}
		}
	}

	void ValidateEvent(FCompileContext& Context, const FADSKXmlElement& Element)
	{
		RejectUnknownAttributes(Context, Element, {TEXT("name"), TEXT("priority"), TEXT("change")});
		const FString* Name = RequireAttribute(Context, Element, TEXT("name"));
		FEventInfo Event;
		Event.Line = Element.Line;
		if (const FString* Change = Element.FindAttribute(TEXT("change")))
		{
			Event.ChangeSlot = Context.FindSlot(*Change);
			if (Event.ChangeSlot == INDEX_NONE)
			{
				Context.Error(EADSKDiagnosticCode::UnresolvedReference, Element.Line, TEXT("change names no Input or Local"));
			}
		}
		if (const FString* Priority = Element.FindAttribute(TEXT("priority")))
		{
			bool bOutOfRange = false;
			if (!ParsePriority(*Priority, Event.Priority, bOutOfRange))
			{
				Context.Error(bOutOfRange ? EADSKDiagnosticCode::OutOfRange : EADSKDiagnosticCode::TypeMismatch, Element.Line,
					TEXT("priority must be an int32"));
			}
		}
		ValidateEventParams(Context, Element, Event);
		if (Event.ChangeSlot != INDEX_NONE && !Event.Params.IsEmpty())
		{
			Context.Error(EADSKDiagnosticCode::BadArity, Element.Line, TEXT("a change event takes no parameters; conditions read the slot"));
		}
		if (!Name)
		{
			return;
		}
		Event.Name = *Name;
		if (Context.FindEvent(Event.Name) != INDEX_NONE)
		{
			Context.Error(EADSKDiagnosticCode::DuplicateId, Element.Line, TEXT("duplicate or reserved event name"));
			return;
		}
		Context.Events.Add(MoveTemp(Event));
	}

	void ValidateEventSection(FCompileContext& Context, const FADSKXmlElement& Section)
	{
		RejectUnknownAttributes(Context, Section, TConstArrayView<const TCHAR*>());
		for (const int32 Child : Section.Children)
		{
			const FADSKXmlElement& Element = Context.Element(Child);
			if (Element.Name.Equals(TEXT("Event"), ESearchCase::CaseSensitive))
			{
				ValidateEvent(Context, Element);
			}
			else
			{
				Context.Error(EADSKDiagnosticCode::UnknownElement, Element.Line, TEXT("unexpected element"));
			}
		}
	}

	void ValidateAgentChild(FCompileContext& Context, int32 Child, int32 Rank)
	{
		const FADSKXmlElement& Element = Context.Element(Child);
		switch (Rank)
		{
		case Enums:
			ValidateEnums(Context, Element);
			break;
		case Inputs:
			ValidateSlotSection(Context, Element, TEXT("Input"), true);
			break;
		case Locals:
			ValidateSlotSection(Context, Element, TEXT("Local"), false);
			break;
		case Sensors:
			ValidateSensors(Context, Child);
			break;
		case Events:
			ValidateEventSection(Context, Element);
			break;
		case StateMachine:
			ValidateStateMachine(Context, Child);
			break;
		default:
			ValidateTree(Context, Child);
			break;
		}
	}

	void ValidateAgent(FCompileContext& Context)
	{
		const FADSKXmlElement& Root = Context.Element(0);
		RejectUnknownAttributes(Context, Root, {TEXT("version"), TEXT("id")});
		if (const FString* Id = RequireAttribute(Context, Root, TEXT("id")))
		{
			Context.AgentId = *Id;
		}
		int32 LastRank = -1;
		bool bHasMachine = false;
		for (const int32 Child : Root.Children)
		{
			const FADSKXmlElement& Element = Context.Element(Child);
			const int32 Rank = SectionRank(Element.Name);
			if (Rank == INDEX_NONE)
			{
				Context.Error(EADSKDiagnosticCode::UnknownElement, Element.Line, TEXT("unexpected element"));
				continue;
			}
			const bool bRepeatedSection = Rank == LastRank && Rank < Tree;
			if (Rank < LastRank || bRepeatedSection)
			{
				Context.Error(EADSKDiagnosticCode::BadArity, Element.Line, TEXT("section out of order or repeated"));
				if (bRepeatedSection)
				{
					continue;
				}
			}
			LastRank = FMath::Max(LastRank, Rank);
			bHasMachine = bHasMachine || Rank == StateMachine;
			ValidateAgentChild(Context, Child, Rank);
		}
		if (!bHasMachine)
		{
			Context.Error(EADSKDiagnosticCode::MissingRequired, Root.Line, TEXT("Agent requires one StateMachine"));
		}
	}
}

void ADSKCompilerInternal::ValidateDocument(FCompileContext& Context)
{
	ADSKCompilerValidatePrivate::ValidateAgent(Context);
	const bool bSourceWalkClean = !Context.Diagnostics.ContainsByPredicate(
		[](const FADSKDiagnostic& Diagnostic) { return Diagnostic.Severity == EADSKSeverity::Error; });
	ResolveReferences(Context, bSourceWalkClean);
}
