// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

class FADSKDefinition;

enum class EADSKSeverity : uint8
{
	Error,
	Warning,
};

/** Stable diagnostic codes. Tests assert these and the structured fields, never the message. */
enum class EADSKDiagnosticCode : uint8
{
	UnknownNode,
	UnknownElement,
	UnknownAttribute,
	UnknownPort,
	TypeMismatch,
	UnresolvedReference,
	DuplicateId,
	DuplicateTransition,
	OutOfRange,
	LimitExceeded,
	MissingRequired,
	BadArity,
	XmlMalformed,
	VersionUnsupported,
	UnreferencedTree,
	UnknownEnumValue,
	UnreachableTransition,
	MissingAsset,
};

/** Kebab-case code name, e.g. `unknown-node`. */
ADAMSKAI_API const TCHAR* LexToString(EADSKDiagnosticCode Code);
ADAMSKAI_API const TCHAR* LexToString(EADSKSeverity Severity);

struct FADSKDiagnostic
{
	EADSKDiagnosticCode Code = EADSKDiagnosticCode::XmlMalformed;
	EADSKSeverity Severity = EADSKSeverity::Error;
	FString Source;
	/** Line of the owning element (attributes have no location of their own); 0 when none applies. */
	int32 Line = 0;
	FName NodeId;
	FString Message;
};

/** Definition is set exactly when no Error diagnostic exists. */
struct ADAMSKAI_API FADSKCompileResult
{
	TSharedPtr<const FADSKDefinition> Definition;
	/** Source encounter order, then compiler-added cross-reference order. */
	TArray<FADSKDiagnostic> Diagnostics;

	bool IsSuccess() const { return Definition.IsValid(); }
	bool HasError() const;
	int32 Count(EADSKDiagnosticCode Code) const;
	const FADSKDiagnostic* FindFirst(EADSKDiagnosticCode Code) const;
};
