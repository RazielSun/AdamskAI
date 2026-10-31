// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Xml/ADSKXmlReader.h"

#include "Compiler/ADSKCompiler.h"
#include "Containers/StringConv.h"
#include "Misc/ScopeExit.h"

THIRD_PARTY_INCLUDES_START
#include "expat.h"
THIRD_PARTY_INCLUDES_END

namespace ADSKXmlReaderPrivate
{
	struct FReadState
	{
		XML_Parser Parser = nullptr;
		const FString* SourceName = nullptr;
		FADSKXmlDocument* Document = nullptr;
		TArray<FADSKDiagnostic>* Diagnostics = nullptr;
		/** Open elements; its depth never exceeds the element limit. */
		TArray<int32> Open;
		/** True once an adapter callback has recorded its own diagnostic and stopped the parser. */
		bool bAborted = false;
	};

	void AddDiagnostic(const FReadState& State, EADSKDiagnosticCode Code, int32 Line, const FString& Message)
	{
		FADSKDiagnostic& Diagnostic = State.Diagnostics->AddDefaulted_GetRef();
		Diagnostic.Code = Code;
		Diagnostic.Severity = EADSKSeverity::Error;
		Diagnostic.Source = *State.SourceName;
		Diagnostic.Line = Line;
		Diagnostic.Message = Message;
	}

	/** Records an authored diagnostic and stops the parser without Expat's generic abort error. */
	void Abort(FReadState& State, EADSKDiagnosticCode Code, const FString& Message)
	{
		AddDiagnostic(State, Code, XML_GetCurrentLineNumber(State.Parser), Message);
		State.bAborted = true;
		XML_StopParser(State.Parser, XML_FALSE);
	}

	FReadState& StateOf(void* UserData)
	{
		return *static_cast<FReadState*>(UserData);
	}

	void XMLCALL OnXmlDecl(void*, const XML_Char*, const XML_Char*, int)
	{
	}

	void XMLCALL OnComment(void*, const XML_Char*)
	{
	}

	void XMLCALL OnStart(void* UserData, const XML_Char* Name, const XML_Char** Attributes)
	{
		FReadState& State = StateOf(UserData);
		if (State.bAborted)
		{
			return;
		}
		TArray<FADSKXmlElement>& Elements = State.Document->Elements;
		if (Elements.Num() >= FADSKCompiler::MaxElements)
		{
			Abort(State, EADSKDiagnosticCode::LimitExceeded, TEXT("too many XML elements"));
			return;
		}
		const int32 Index = Elements.AddDefaulted();
		FADSKXmlElement& Element = Elements[Index];
		Element.Name = FString(FUTF8ToTCHAR(Name).Get());
		Element.Line = XML_GetCurrentLineNumber(State.Parser);
		Element.Parent = State.Open.IsEmpty() ? INDEX_NONE : State.Open.Last();
		for (int32 At = 0; Attributes[At]; At += 2)
		{
			Element.Attributes.Add({FString(FUTF8ToTCHAR(Attributes[At]).Get()), FString(FUTF8ToTCHAR(Attributes[At + 1]).Get())});
		}
		if (Element.Parent != INDEX_NONE)
		{
			Elements[Element.Parent].Children.Add(Index);
		}
		State.Open.Add(Index);
	}

	void XMLCALL OnEnd(void* UserData, const XML_Char*)
	{
		FReadState& State = StateOf(UserData);
		if (!State.bAborted && !State.Open.IsEmpty())
		{
			State.Open.Pop();
		}
	}

	void XMLCALL OnText(void* UserData, const XML_Char* Text, int Length)
	{
		FReadState& State = StateOf(UserData);
		for (int32 Index = 0; Index < Length && !State.bAborted; ++Index)
		{
			const char C = Text[Index];
			if (C != ' ' && C != '\t' && C != '\r' && C != '\n')
			{
				Abort(State, EADSKDiagnosticCode::XmlMalformed, TEXT("text content is not supported"));
			}
		}
	}

	void XMLCALL OnProcessingInstruction(void* UserData, const XML_Char*, const XML_Char*)
	{
		FReadState& State = StateOf(UserData);
		if (!State.bAborted)
		{
			Abort(State, EADSKDiagnosticCode::XmlMalformed, TEXT("processing instructions are not supported"));
		}
	}

	void XMLCALL OnStartCdata(void* UserData)
	{
		FReadState& State = StateOf(UserData);
		if (!State.bAborted)
		{
			Abort(State, EADSKDiagnosticCode::XmlMalformed, TEXT("CDATA is not supported"));
		}
	}

	void XMLCALL OnStartDoctype(void* UserData, const XML_Char*, const XML_Char*, const XML_Char*, int)
	{
		FReadState& State = StateOf(UserData);
		if (!State.bAborted)
		{
			Abort(State, EADSKDiagnosticCode::XmlMalformed, TEXT("DOCTYPE is not supported"));
		}
	}

	void InstallHandlers(FReadState& State)
	{
		XML_SetUserData(State.Parser, &State);
		XML_SetXmlDeclHandler(State.Parser, OnXmlDecl);
		XML_SetElementHandler(State.Parser, OnStart, OnEnd);
		XML_SetCharacterDataHandler(State.Parser, OnText);
		XML_SetCommentHandler(State.Parser, OnComment);
		XML_SetProcessingInstructionHandler(State.Parser, OnProcessingInstruction);
		XML_SetStartCdataSectionHandler(State.Parser, OnStartCdata);
		XML_SetStartDoctypeDeclHandler(State.Parser, OnStartDoctype);
	}

	void AddParseError(const FReadState& State)
	{
		const XML_Error Error = XML_GetErrorCode(State.Parser);
		const FString Message = FString::Printf(TEXT("%s (column %d)"), *FString(FUTF8ToTCHAR(XML_ErrorString(Error)).Get()),
			static_cast<int32>(XML_GetCurrentColumnNumber(State.Parser)) + 1);
		AddDiagnostic(State, EADSKDiagnosticCode::XmlMalformed, XML_GetCurrentLineNumber(State.Parser), Message);
	}
}

using namespace ADSKXmlReaderPrivate;

const FString* FADSKXmlElement::FindAttribute(FStringView AttributeName) const
{
	for (const FADSKXmlAttribute& Attribute : Attributes)
	{
		if (AttributeName.Equals(Attribute.Name, ESearchCase::CaseSensitive))
		{
			return &Attribute.Value;
		}
	}
	return nullptr;
}

bool ADSKXml::Read(const FString& SourceName, const FString& Text, FADSKXmlDocument& OutDocument,
	TArray<FADSKDiagnostic>& OutDiagnostics)
{
	// With the length: an embedded NUL must reach the parser (which rejects it), not end the source early.
	const FTCHARToUTF8 Utf8(*Text, Text.Len());
	FReadState State;
	State.SourceName = &SourceName;
	State.Document = &OutDocument;
	State.Diagnostics = &OutDiagnostics;
	if (Utf8.Length() > FADSKCompiler::MaxSourceBytes)
	{
		AddDiagnostic(State, EADSKDiagnosticCode::LimitExceeded, 0, TEXT("source exceeds 256 KiB of UTF-8"));
		return false;
	}
	FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), OutDocument.SourceHash.Hash);

	State.Parser = XML_ParserCreate("UTF-8");
	if (!State.Parser)
	{
		AddDiagnostic(State, EADSKDiagnosticCode::XmlMalformed, 0, TEXT("XML parser could not be created"));
		return false;
	}
	ON_SCOPE_EXIT
	{
		XML_ParserFree(State.Parser);
	};
	InstallHandlers(State);

	const int32 DiagnosticsBefore = OutDiagnostics.Num();
	if (XML_Parse(State.Parser, Utf8.Get(), Utf8.Length(), XML_TRUE) == XML_STATUS_ERROR && !State.bAborted)
	{
		AddParseError(State);
	}
	return OutDiagnostics.Num() == DiagnosticsBefore;
}
