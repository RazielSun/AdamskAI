// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Validate/ADSKAssetValidation.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "GameplayTagsManager.h"
#include "Program/ADSKDefinition.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

void FADSKAssetValidation::AppendMissingAssets(const FADSKDefinition& Definition, FADSKCompileResult& InOutResult)
{
	IAssetRegistry* Registry = IAssetRegistry::Get();
	if (!Registry) { return; }
	const auto Warn = [&Definition, &InOutResult](int32 Line, FName NodeId, FString Message)
	{
		FADSKDiagnostic& Diagnostic = InOutResult.Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Code = EADSKDiagnosticCode::MissingAsset;
		Diagnostic.Severity = EADSKSeverity::Warning;
		Diagnostic.Source = Definition.GetSourceName();
		Diagnostic.Line = Line;
		Diagnostic.NodeId = NodeId;
		Diagnostic.Message = MoveTemp(Message);
	};
	for (const FADSKObjectPathField& Field : ADSK::GetObjectPathFields(Definition))
	{
		if (Field.Path.IsNull()) { continue; }
		if (!Field.bClass)
		{
			const FAssetData Asset = Registry->GetAssetByObjectPath(Field.Path);
			if (!Asset.IsValid()) { Warn(Field.Line, Field.NodeId, FString::Printf(TEXT("object '%s' not found in Asset Registry"), *Field.Path.ToString())); }
			// A redirector still loads, but the next fixup removes it; the path should name where the asset is now.
			else if (Asset.IsRedirector())
			{
				Warn(Field.Line, Field.NodeId, FString::Printf(TEXT("object '%s' was moved to '%s'"), *Field.Path.ToString(), *Registry->GetRedirectedObjectPath(Field.Path).ToString()));
			}
			continue;
		}
		// A native class is not an asset and a Blueprint class is its asset's `_C`, so classes are loaded instead.
		const bool bNative = Field.Path.GetLongPackageName().StartsWith(TEXT("/Script/"));
		const UClass* Class = Cast<UClass>(bNative ? Field.Path.ResolveObject() : Field.Path.TryLoad());
		if (!Class) { Warn(Field.Line, Field.NodeId, FString::Printf(TEXT("class '%s' not found"), *Field.Path.ToString())); }
		else if (Field.MetaClass && !Class->IsChildOf(Field.MetaClass))
		{
			Warn(Field.Line, Field.NodeId, FString::Printf(TEXT("class '%s' is not a %s"), *Field.Path.ToString(), *Field.MetaClass->GetName()));
		}
	}
	for (const FADSKTagField& Field : ADSK::GetTagFields(Definition))
	{
		if (!UGameplayTagsManager::Get().RequestGameplayTag(Field.Tag, false).IsValid())
		{
			Warn(Field.Line, Field.NodeId, FString::Printf(TEXT("gameplay tag '%s' is not registered"), *Field.Tag.ToString()));
		}
	}
	// A brain refuses a definition whose state tags are not registered, so the validator says so first.
	for (const FADSKStateDefinition& State : Definition.GetStates())
	{
		for (const FName Tag : Definition.GetStateTags().Slice(State.FirstTag, State.TagCount))
		{
			if (!UGameplayTagsManager::Get().RequestGameplayTag(Tag, false).IsValid())
			{
				Warn(State.Line, NAME_None, FString::Printf(TEXT("state '%s': gameplay tag '%s' is not registered; brains refuse this file (unknown-tag)"), *State.Id, *Tag.ToString()));
			}
		}
	}
	// Only nodes that ask for it load their assets here; everything else stays a registry lookup.
	for (const FADSKNodeDefinition& Node : Definition.GetNodes())
	{
		if (!Node.Descriptor || !Node.Descriptor->ValidateAssets || !Node.Config.IsValid()) { continue; }
		for (const FADSKFieldMap& Field : Node.Descriptor->Fields)
		{
			if (Field.Kind == EADSKFieldKind::ObjectPath) { Field.Property->ContainerPtrToValuePtr<FSoftObjectPath>(Node.Config.GetMemory())->TryLoad(); }
		}
		FString Error;
		if (Node.Descriptor->ValidateAssets(Node.Config.GetMemory(), Error)) { continue; }
		FADSKDiagnostic& Diagnostic = InOutResult.Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Code = EADSKDiagnosticCode::MissingAsset;
		Diagnostic.Severity = EADSKSeverity::Warning;
		Diagnostic.Source = Definition.GetSourceName();
		Diagnostic.Line = Node.Line;
		Diagnostic.NodeId = Node.Id;
		Diagnostic.Message = MoveTemp(Error);
	}
}
