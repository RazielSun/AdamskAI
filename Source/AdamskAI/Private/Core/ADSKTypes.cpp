// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Core/ADSKTypes.h"
#include "Program/ADSKDefinition.h"

namespace ADSKTypesPrivate
{
	bool IsDigit(TCHAR C)
	{
		return C >= TEXT('0') && C <= TEXT('9');
	}

	/** Consumes one or more digits; returns the new index or INDEX_NONE when none are present. */
	int32 SkipDigits(FStringView Text, int32 Index)
	{
		const int32 Start = Index;
		while (Index < Text.Len() && IsDigit(Text[Index]))
		{
			++Index;
		}
		return Index > Start ? Index : INDEX_NONE;
	}

	bool SameFloat(double A, double B)
	{
		return A == B || (FMath::IsNaN(A) && FMath::IsNaN(B));
	}
}

using namespace ADSKTypesPrivate;

FADSKValue FADSKValue::MakeBool(bool bInValue)
{
	FADSKValue Result;
	Result.Type = EADSKValueType::Bool;
	Result.bBool = bInValue;
	return Result;
}

FADSKValue FADSKValue::MakeFloat(float InValue)
{
	FADSKValue Result;
	Result.Type = EADSKValueType::Float;
	Result.Float = InValue;
	return Result;
}

FADSKValue FADSKValue::MakeVector(const FVector& InValue)
{
	FADSKValue Result;
	Result.Type = EADSKValueType::Vector;
	Result.Vector = InValue;
	return Result;
}

FADSKValue FADSKValue::MakeInt(int32 InValue)
{
	FADSKValue Result;
	Result.Type = EADSKValueType::Int;
	Result.Int = InValue;
	return Result;
}

FADSKValue FADSKValue::MakeEnum(int16 InEnum, int32 InValue)
{
	FADSKValue Result;
	Result.Type = EADSKValueType::Enum;
	Result.Enum = InEnum;
	Result.Int = InValue;
	return Result;
}

FADSKValue FADSKValue::MakeObject(uint64 InId)
{
	FADSKValue Result;
	Result.Type = EADSKValueType::Object;
	Result.ObjectId = InId;
	return Result;
}

FADSKValue FADSKValue::MakeZero(EADSKValueType InType)
{
	FADSKValue Result;
	Result.Type = InType;
	return Result;
}

bool FADSKValue::IsFinite() const
{
	switch (Type)
	{
	case EADSKValueType::Float:
		return FMath::IsFinite(Float);
	case EADSKValueType::Vector:
		return FMath::IsFinite(Vector.X) && FMath::IsFinite(Vector.Y) && FMath::IsFinite(Vector.Z);
	default:
		return true;
	}
}

bool FADSKValue::operator==(const FADSKValue& Other) const
{
	if (Type != Other.Type)
	{
		return false;
	}
	switch (Type)
	{
	case EADSKValueType::Bool:
		return bBool == Other.bBool;
	case EADSKValueType::Float:
		return SameFloat(Float, Other.Float);
	case EADSKValueType::Vector:
		return SameFloat(Vector.X, Other.Vector.X) && SameFloat(Vector.Y, Other.Vector.Y) && SameFloat(Vector.Z, Other.Vector.Z);
	case EADSKValueType::Int:
		return Int == Other.Int;
	case EADSKValueType::Enum:
		return Enum == Other.Enum && Int == Other.Int;
	case EADSKValueType::Object:
		return ObjectId == Other.ObjectId;
	}
	return false;
}

bool ADSK::TryParseBool(FStringView Text, bool& OutValue)
{
	if (Text.Equals(TEXT("true"), ESearchCase::CaseSensitive))
	{
		OutValue = true;
		return true;
	}
	if (Text.Equals(TEXT("false"), ESearchCase::CaseSensitive))
	{
		OutValue = false;
		return true;
	}
	return false;
}

bool ADSK::TryParseFloat(FStringView Text, float& OutValue)
{
	int32 Index = (!Text.IsEmpty() && Text[0] == TEXT('-')) ? 1 : 0;
	Index = SkipDigits(Text, Index);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	if (Index < Text.Len() && Text[Index] == TEXT('.'))
	{
		Index = SkipDigits(Text, Index + 1);
		if (Index == INDEX_NONE)
		{
			return false;
		}
	}
	if (Index != Text.Len())
	{
		return false;
	}
	const double Parsed = FCString::Atod(*FString(Text));
	if (!FMath::IsFinite(Parsed) || FMath::Abs(Parsed) > static_cast<double>(TNumericLimits<float>::Max()))
	{
		return false;
	}
	OutValue = static_cast<float>(Parsed);
	return true;
}

bool ADSK::TryParseInt(FStringView Text, int32& OutValue)
{
	const int32 Start = (!Text.IsEmpty() && Text[0] == TEXT('-')) ? 1 : 0;
	if (SkipDigits(Text, Start) != Text.Len())
	{
		return false;
	}
	// Leading zeros are legal; only the significant digits can overflow.
	int32 Significant = Start;
	while (Significant < Text.Len() - 1 && Text[Significant] == TEXT('0'))
	{
		++Significant;
	}
	if (Text.Len() - Significant > 10)
	{
		return false;
	}
	const int64 Magnitude = FCString::Atoi64(*FString(Text.RightChop(Significant)));
	const int64 Parsed = Start ? -Magnitude : Magnitude;
	if (Parsed > MAX_int32 || Parsed < MIN_int32)
	{
		return false;
	}
	OutValue = static_cast<int32>(Parsed);
	return true;
}

bool ADSK::TryParseVector(FStringView Text, FVector& OutValue)
{
	double Components[3];
	FStringView Rest = Text;
	for (int32 Component = 0; Component < 3; ++Component)
	{
		int32 Comma = INDEX_NONE;
		const bool bLast = Component == 2;
		if (!bLast && !Rest.FindChar(TEXT(','), Comma))
		{
			return false;
		}
		const FStringView Part = bLast ? Rest : Rest.Left(Comma);
		float Parsed = 0.f;
		if (!TryParseFloat(Part, Parsed))
		{
			return false;
		}
		Components[Component] = Parsed;
		if (!bLast)
		{
			Rest = Rest.RightChop(Comma + 1);
		}
	}
	OutValue = FVector(Components[0], Components[1], Components[2]);
	return true;
}

bool ADSK::TryParseValue(EADSKValueType Type, FStringView Text, FADSKValue& OutValue)
{
	switch (Type)
	{
	case EADSKValueType::Bool:
	{
		bool bParsed = false;
		if (!TryParseBool(Text, bParsed))
		{
			return false;
		}
		OutValue = FADSKValue::MakeBool(bParsed);
		return true;
	}
	case EADSKValueType::Float:
	{
		float Parsed = 0.f;
		if (!TryParseFloat(Text, Parsed))
		{
			return false;
		}
		OutValue = FADSKValue::MakeFloat(Parsed);
		return true;
	}
	case EADSKValueType::Vector:
	{
		FVector Parsed;
		if (!TryParseVector(Text, Parsed))
		{
			return false;
		}
		OutValue = FADSKValue::MakeVector(Parsed);
		return true;
	}
	case EADSKValueType::Int:
	{
		int32 Parsed = 0;
		if (!TryParseInt(Text, Parsed))
		{
			return false;
		}
		OutValue = FADSKValue::MakeInt(Parsed);
		return true;
	}
	case EADSKValueType::Object:
		if (!Text.Equals(TEXT("none"), ESearchCase::CaseSensitive))
		{
			return false;
		}
		OutValue = FADSKValue::MakeObject(0);
		return true;
	case EADSKValueType::Enum:
		return false;
	}
	return false;
}

bool ADSK::IsIdentifier(FStringView Text)
{
	const auto IsAlpha = [](TCHAR C) { return (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z')); };
	const auto IsAlnum = [&IsAlpha](TCHAR C) { return IsAlpha(C) || (C >= TEXT('0') && C <= TEXT('9')); };
	if (Text.IsEmpty() || !IsAlpha(Text[0])) return false;
	for (int32 Index = 1; Index < Text.Len(); ++Index) if (!IsAlnum(Text[Index]) && Text[Index] != TEXT('-')) return false;
	return true;
}

const TCHAR* LexToString(EADSKValueType Type)
{
	switch (Type)
	{
	case EADSKValueType::Bool:
		return TEXT("Bool");
	case EADSKValueType::Float:
		return TEXT("Float");
	case EADSKValueType::Vector:
		return TEXT("Vector");
	case EADSKValueType::Int:
		return TEXT("Int");
	case EADSKValueType::Enum:
		return TEXT("Enum");
	case EADSKValueType::Object:
		return TEXT("Object");
	}
	return TEXT("Unknown");
}

FString LexToString(const FADSKValue& Value)
{
	switch (Value.Type)
	{
	case EADSKValueType::Bool:
		return Value.bBool ? TEXT("true") : TEXT("false");
	case EADSKValueType::Float:
		return FString::SanitizeFloat(Value.Float);
	case EADSKValueType::Vector:
		return FString::Printf(TEXT("%s,%s,%s"), *FString::SanitizeFloat(Value.Vector.X),
			*FString::SanitizeFloat(Value.Vector.Y), *FString::SanitizeFloat(Value.Vector.Z));
	case EADSKValueType::Int:
		return FString::FromInt(Value.Int);
	case EADSKValueType::Enum:
		return FString::Printf(TEXT("Enum#%d:%d"), Value.Enum, Value.Int);
	case EADSKValueType::Object:
		return Value.ObjectId == 0 ? FString(TEXT("none")) : FString::Printf(TEXT("Object#%llu"), Value.ObjectId);
	}
	return FString();
}

FString ADSK::FormatValue(const FADSKDefinition& Definition, const FADSKValue& Value)
{
	if (Value.Type != EADSKValueType::Enum) return LexToString(Value);
	FString TypeName, ValueName;
	return Definition.ResolveEnumValue(Value, TypeName, ValueName) ? TypeName + TEXT(":") + ValueName : LexToString(Value);
}
