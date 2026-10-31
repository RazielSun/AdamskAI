// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

struct FADSKTempTree
{
	FADSKTempTree() : Root(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/AdamskAI") / FGuid::NewGuid().ToString(EGuidFormats::Digits)))
	{
		IFileManager::Get().MakeDirectory(*Root, true);
	}

	~FADSKTempTree() { IFileManager::Get().DeleteDirectory(*Root, false, true); }

	FADSKTempTree(const FADSKTempTree&) = delete;
	FADSKTempTree& operator=(const FADSKTempTree&) = delete;

	FString Path(const FString& Relative) const { return Root / Relative; }

	FString Write(const FString& Relative, const FString& Text) const
	{
		const FString Target = Path(Relative);
		FFileHelper::SaveStringToFile(Text, *Target, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		return Target;
	}

	FString Read(const FString& Relative) const
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path(Relative));
		return Text;
	}

	bool Exists(const FString& Relative) const { return IFileManager::Get().FileExists(*Path(Relative)); }

	int32 CountFiles(const FString& Relative) const
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(Path(Relative) / TEXT("*")), true, false);
		return Found.Num();
	}

	FString Root;
};

namespace FADSKTestPaths
{
	inline FString PluginBase()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AdamskAI"));
		return Plugin.IsValid() ? Plugin->GetBaseDir() : FString();
	}

	inline FString ValidAgentXml()
	{
		return TEXT("<Agent version=\"1\" id=\"a\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"0\"/></Tree></Agent>");
	}
}
