// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Program/ADSKDiagnostic.h"
#include "CoreMinimal.h"
#include "Misc/SecureHash.h"

struct FADSKXmlAttribute
{
	FString Name;
	FString Value;
};

struct FADSKXmlElement
{
	FString Name;
	int32 Line = 0;
	int32 Parent = INDEX_NONE;
	TArray<int32> Children;
	/** Authored order. Expat has already rejected duplicates. */
	TArray<FADSKXmlAttribute> Attributes;

	/** Exact-case attribute lookup; null when absent. */
	const FString* FindAttribute(FStringView AttributeName) const;
};

/** Element zero is the single root after a successful read. */
struct FADSKXmlDocument
{
	TArray<FADSKXmlElement> Elements;
	/** SHA-1 of the exact UTF-8 bytes handed to the parser. */
	FSHAHash SourceHash;
};

namespace ADSKXml
{
	/** Rejects PI, DOCTYPE, CDATA and text content. True only when no diagnostic was added. */
	bool Read(const FString& SourceName, const FString& Text, FADSKXmlDocument& OutDocument,
		TArray<FADSKDiagnostic>& OutDiagnostics);
}
