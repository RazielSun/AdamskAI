// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Sources/ADSKValidation.h"
#include "Sources/ADSKBehaviorRoots.h"
#include "Sources/ADSKSourcePath.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FADSKSourceLocation FADSKValidation::Locate(const FString& Root, FStringView Relative)
{
	FADSKSourceLocation Out;
	// A mounted path reads from its mount; its name, with the `@Mount/` prefix, is what diagnostics show.
	FString EffectiveRoot = Root, Mount, InRoot, Filename;
	const bool bNormalized = FADSKSourcePath::Normalize(Relative, Out.Path);
	InRoot = Out.Path;
	if (!bNormalized || (FADSKSourcePath::SplitMount(Out.Path, Mount, InRoot) && !FADSKBehaviorRoots::Find(Mount, EffectiveRoot))) return Out;
	if (FADSKSourcePath::Resolve(EffectiveRoot, InRoot, Filename)) Out.Filename = MoveTemp(Filename);
	return Out;
}

FADSKSourceReadResult FADSKValidation::ReadSource(const FString& Root, FStringView Relative, const TSharedRef<const FADSKNodeRegistry>& Registry)
{
	return ReadSource(Locate(Root, Relative), Registry);
}

FADSKSourceReadResult FADSKValidation::ReadSource(const FADSKSourceLocation& Location, const TSharedRef<const FADSKNodeRegistry>& Registry)
{
	FADSKSourceReadResult Out;
	const FString& Path = Location.Path;
	const FString& Filename = Location.Filename;
	Out.bReadFailed = Filename.IsEmpty();
	const int64 Size = Out.bReadFailed ? -1 : IFileManager::Get().FileSize(*Filename);
	// Normalize keeps a path that failed only its extension, so this names the file a case-insensitive filesystem found.
	const TCHAR* ReadMessage = Out.bReadFailed && Path.EndsWith(TEXT(".xml"), ESearchCase::IgnoreCase) && !Path.EndsWith(TEXT(".xml"), ESearchCase::CaseSensitive)
		? TEXT("source-read: behavior file names must end in lowercase .xml") : TEXT("source-read");
	if (Size < 0) { Out.bReadFailed = true; Out.ReportCode = TEXT("source-read"); Out.Result.Diagnostics.Add({EADSKDiagnosticCode::MissingRequired, EADSKSeverity::Error, Path, 0, NAME_None, ReadMessage}); return Out; }
	if (Size > FADSKCompiler::MaxSourceBytes) { Out.Result.Diagnostics.Add({EADSKDiagnosticCode::LimitExceeded, EADSKSeverity::Error, Path, 0, NAME_None, TEXT("limit-exceeded")}); return Out; }
	if (!FFileHelper::LoadFileToString(Out.Text, *Filename)) { Out.bReadFailed = true; Out.ReportCode = TEXT("source-read"); Out.Result.Diagnostics.Add({EADSKDiagnosticCode::MissingRequired, EADSKSeverity::Error, Path, 0, NAME_None, TEXT("source-read")}); return Out; }
	Out.Result = FADSKCompiler::Compile(Path, Out.Text, Registry); return Out;
}

TArray<FADSKSourceReadResult> FADSKValidation::ValidateFiles(const FString& Root, const TSharedRef<const FADSKNodeRegistry>& Registry)
{
	TArray<FString> Found; IFileManager::Get().FindFilesRecursive(Found, *Root, TEXT("*.xml"), true, false); TArray<FString> Paths;
	FString FullRoot = FPaths::ConvertRelativePathToFull(Root); FPaths::NormalizeDirectoryName(FullRoot); FullRoot += TEXT("/");
	// The "*.xml" wildcard matches case-insensitively on Windows; a `Guard.XML` it finds is reported by ReadSource, never skipped.
	for (const FString& File : Found) { FString Relative = File; FPaths::MakePathRelativeTo(Relative, *FullRoot); FString Normalized; if (FADSKSourcePath::Normalize(Relative, Normalized) || !Normalized.IsEmpty()) Paths.Add(MoveTemp(Normalized)); }
	Paths.Sort(); TArray<FADSKSourceReadResult> Out;
	for (const FString& Path : Paths) Out.Add(ReadSource(Root, Path, Registry));
	return Out;
}
