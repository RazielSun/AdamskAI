// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Compiler/ADSKCompiler.h"

#include "Runtime/ADSKNodeRegistry.h"
#include "Compiler/ADSKCompilerInternal.h"
#include "Xml/ADSKXmlReader.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Trace/ADSKStats.h"

namespace ADSKCompilerPrivate
{
	void AddRootDiagnostic(TArray<FADSKDiagnostic>& Diagnostics, const FString& Source, EADSKDiagnosticCode Code, int32 Line, const TCHAR* Message)
	{
		FADSKDiagnostic& Diagnostic = Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Code = Code;
		Diagnostic.Source = Source;
		Diagnostic.Line = Line;
		Diagnostic.Message = Message;
	}

	/** The root must be one `Agent` carrying exactly `version="1"`; the walk assumes it. */
	bool CheckRoot(const FADSKXmlDocument& Document, const FString& Source, TArray<FADSKDiagnostic>& Diagnostics)
	{
		const FADSKXmlElement& Root = Document.Elements[0];
		if (!Root.Name.Equals(TEXT("Agent"), ESearchCase::CaseSensitive))
		{
			AddRootDiagnostic(Diagnostics, Source, EADSKDiagnosticCode::UnknownElement, Root.Line, TEXT("root element must be Agent"));
			return false;
		}
		const FString* Version = Root.FindAttribute(TEXT("version"));
		if (!Version)
		{
			AddRootDiagnostic(Diagnostics, Source, EADSKDiagnosticCode::MissingRequired, Root.Line, TEXT("Agent requires version"));
			return false;
		}
		if (!Version->Equals(TEXT("1"), ESearchCase::CaseSensitive))
		{
			AddRootDiagnostic(Diagnostics, Source, EADSKDiagnosticCode::VersionUnsupported, Root.Line, TEXT("only version 1 is supported"));
			return false;
		}
		return true;
	}
}

using namespace ADSKCompilerPrivate;

using namespace ADSKCompilerInternal;

FADSKCompileResult FADSKCompiler::Compile(
	const FString& SourceName, const FString& Text, const TSharedRef<const FADSKNodeRegistry>& Registry)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FADSKCompiler::Compile);
	LLM_SCOPE_BYTAG(AdamskAI);
	FADSKCompileResult Result;
	if (!ensureMsgf(Registry->IsFrozen(), TEXT("FADSKCompiler requires a frozen registry")))
	{
		FADSKDiagnostic& Diagnostic = Result.Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Code = EADSKDiagnosticCode::MissingRequired;
		Diagnostic.Source = SourceName;
		Diagnostic.Message = TEXT("registry is not frozen");
		return Result;
	}
	FADSKXmlDocument Document;
	if (!ADSKXml::Read(SourceName, Text, Document, Result.Diagnostics))
	{
		return Result;
	}
	if (!CheckRoot(Document, SourceName, Result.Diagnostics))
	{
		return Result;
	}
	FCompileContext Context(Document, Registry, SourceName, Result.Diagnostics);
	ValidateDocument(Context);
	if (!Result.HasError())
	{
		Result.Definition = ADSKLink::FLinker::Link(Context);
	}
	return Result;
}
