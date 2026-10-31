// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Program/ADSKDiagnostic.h"

const TCHAR* LexToString(EADSKDiagnosticCode Code)
{
	switch (Code)
	{
	case EADSKDiagnosticCode::UnknownNode:
		return TEXT("unknown-node");
	case EADSKDiagnosticCode::UnknownElement:
		return TEXT("unknown-element");
	case EADSKDiagnosticCode::UnknownAttribute:
		return TEXT("unknown-attribute");
	case EADSKDiagnosticCode::UnknownPort:
		return TEXT("unknown-port");
	case EADSKDiagnosticCode::TypeMismatch:
		return TEXT("type-mismatch");
	case EADSKDiagnosticCode::UnresolvedReference:
		return TEXT("unresolved-reference");
	case EADSKDiagnosticCode::DuplicateId:
		return TEXT("duplicate-id");
	case EADSKDiagnosticCode::DuplicateTransition:
		return TEXT("duplicate-transition");
	case EADSKDiagnosticCode::OutOfRange:
		return TEXT("out-of-range");
	case EADSKDiagnosticCode::LimitExceeded:
		return TEXT("limit-exceeded");
	case EADSKDiagnosticCode::MissingRequired:
		return TEXT("missing-required");
	case EADSKDiagnosticCode::BadArity:
		return TEXT("bad-arity");
	case EADSKDiagnosticCode::XmlMalformed:
		return TEXT("xml-malformed");
	case EADSKDiagnosticCode::VersionUnsupported:
		return TEXT("version-unsupported");
	case EADSKDiagnosticCode::UnreferencedTree:
		return TEXT("unreferenced-tree");
	case EADSKDiagnosticCode::UnknownEnumValue:
		return TEXT("unknown-enum-value");
	case EADSKDiagnosticCode::UnreachableTransition:
		return TEXT("unreachable-transition");
	case EADSKDiagnosticCode::MissingAsset:
		return TEXT("missing-asset");
	}
	return TEXT("unknown");
}

const TCHAR* LexToString(EADSKSeverity Severity)
{
	return Severity == EADSKSeverity::Error ? TEXT("error") : TEXT("warning");
}

bool FADSKCompileResult::HasError() const
{
	return Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D) { return D.Severity == EADSKSeverity::Error; });
}

int32 FADSKCompileResult::Count(EADSKDiagnosticCode Code) const
{
	int32 Total = 0;
	for (const FADSKDiagnostic& Diagnostic : Diagnostics)
	{
		Total += Diagnostic.Code == Code ? 1 : 0;
	}
	return Total;
}

const FADSKDiagnostic* FADSKCompileResult::FindFirst(EADSKDiagnosticCode Code) const
{
	return Diagnostics.FindByPredicate([Code](const FADSKDiagnostic& D) { return D.Code == Code; });
}
