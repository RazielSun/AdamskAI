// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Runtime/ADSKNode.h"
#include "CoreMinimal.h"
#include "Misc/SecureHash.h"

/** Outcome of FADSKNodeRegistry::Register, in validation order. */
enum class EADSKRegisterResult : uint8
{
	Accepted,
	Frozen,
	InvalidName,
	DuplicateName,
	ReservedName,
	InvalidArity,
	InvalidFieldName,
	DuplicateField,
	MissingProperty,
	UnsupportedFieldType,
	InvalidPortName,
	DuplicatePort,
	PortFieldCollision,
	UnsafeStruct,
	UnsupportedPortType,
	/** Bad ParamsProperty, or bTakesArgs with it or on a kind other than Leaf, Scope, Composite. */
	InvalidParamsProperty,
};

ADAMSKAI_API const TCHAR* LexToString(EADSKRegisterResult Result);

/** Freeze before sharing; definitions retain it. Names are exact-case. */
class ADAMSKAI_API FADSKNodeRegistry
{
public:
	static TSharedRef<FADSKNodeRegistry> Create();

	/** The module's frozen registry holding the built-in nodes. The module must be loaded. */
	static TSharedRef<const FADSKNodeRegistry> GetDefault();

	FADSKNodeRegistry(const FADSKNodeRegistry&) = delete;
	FADSKNodeRegistry& operator=(const FADSKNodeRegistry&) = delete;

	EADSKRegisterResult Register(FADSKNodeDescriptor Descriptor);
	/** `<name>: <result>` for every refused Register, in call order; assembly fails on any, whoever ignored the result. */
	TConstArrayView<FString> GetRejections() const { return Rejections; }

	/** Idempotent; no registration is accepted afterwards. */
	void Freeze();
	bool IsFrozen() const { return bFrozen; }
	const FSHAHash& GetCatalogueFingerprint() const;

	/** Exact-case lookup; the returned pointer lives as long as the registry. */
	const FADSKNodeDescriptor* Find(FStringView NodeName) const;

	int32 Num() const { return Descriptors.Num(); }

	/** Every descriptor in registration order; the pointers live as long as the registry. */
	TArray<const FADSKNodeDescriptor*> GetDescriptors() const;

	/** Condensed deterministic schema JSON. Requires a frozen registry; empty string otherwise. */
	FString ExportSchemaJson() const;

private:
	FADSKNodeRegistry() = default;
	EADSKRegisterResult Admit(FADSKNodeDescriptor& Descriptor) const;

	TArray<TUniquePtr<FADSKNodeDescriptor>> Descriptors;
	TArray<FString> Rejections;
	bool bFrozen = false;
	FSHAHash CatalogueFingerprint;
};
