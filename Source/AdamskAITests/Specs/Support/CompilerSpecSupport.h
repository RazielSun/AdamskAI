// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Nodes/ADSKBuiltinNodes.h"
#include "Program/ADSKDefinition.h"
#include "Compiler/ADSKCompiler.h"
#include "Support/GuardFixture.h"
#include "Support/TestADSKNodes.h"
#include "Support/TestChecks.h"
#include "CoreMinimal.h"

namespace FCompilerSpecSupport
{
	inline FADSKCompileResult Load(const FString& Xml)
	{
		const TSharedRef<FADSKTestLeafLog> Log = MakeShared<FADSKTestLeafLog>();
		return FADSKCompiler::Compile(TEXT("spec.xml"), Xml, FADSKTestNodes::MakeFrozenRegistry(Log));
	}

	inline TSharedRef<const FADSKNodeRegistry> MakeProbeRegistry()
	{
		const TSharedRef<FADSKTestLeafLog> Log = MakeShared<FADSKTestLeafLog>();
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		FADSKTestNodes::RegisterTestNodes(*Registry, Log);
		FADSKNodeDescriptor Probe;
		Probe.Name = TEXT("Probe");
		Probe.ConfigStruct = FADSKTestFieldConfig::StaticStruct();
		Probe.Fields.Add({TEXT("flag"), TEXT("bFlag")});
		Probe.Fields.Add({TEXT("scale"), TEXT("Scale")});
		Probe.Fields.Add({TEXT("offset"), TEXT("Offset")});
		Probe.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
		ADSKTest::Register(*Registry, Probe);
		Registry->Freeze();
		return Registry;
	}

	inline TSharedRef<const FADSKNodeRegistry> MakePathProbeRegistry()
	{
		const TSharedRef<FADSKTestLeafLog> Log = MakeShared<FADSKTestLeafLog>();
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		FADSKTestNodes::RegisterTestNodes(*Registry, Log);
		FADSKNodeDescriptor Probe;
		Probe.Name = TEXT("PathProbe");
		Probe.ConfigStruct = FADSKTestPathConfig::StaticStruct();
		Probe.Fields.Add({TEXT("query"), TEXT("Query")});
		Probe.Fields.Add({TEXT("scale"), TEXT("Scale")});
		Probe.ValidateConfig = [](const void* Config) { return !static_cast<const FADSKTestPathConfig*>(Config)->Query.IsNull(); };
		Probe.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
		ADSKTest::Register(*Registry, Probe);
		Registry->Freeze();
		return Registry;
	}

	inline FADSKCompileResult LoadPathProbe(const FString& Xml)
	{
		return FADSKCompiler::Compile(TEXT("spec.xml"), Xml, MakePathProbeRegistry());
	}

	inline FADSKCompileResult LoadProbe(const FString& Xml)
	{
		return FADSKCompiler::Compile(TEXT("spec.xml"), Xml, MakeProbeRegistry());
	}

	/**
	 * The seventeen-line base document every rule row mutates. Line numbers are part of the
	 * contract: 3 inputs, 6 locals, 9 event, 12 state, 14 tree, 15 node, 16 tree end.
	 */
	inline TArray<FString> BaseLines()
	{
		return {
			TEXT("<Agent version=\"1\" id=\"a\">"),
			TEXT("<Inputs>"),
			TEXT("<Input name=\"In\" type=\"Vector\" required=\"true\"/><Input name=\"Flag\" type=\"Bool\" default=\"true\"/><Input name=\"Num\" type=\"Float\" default=\"1\"/>"),
			TEXT("</Inputs>"),
			TEXT("<Locals>"),
			TEXT("<Local name=\"L\" type=\"Vector\"/><Local name=\"LB\" type=\"Bool\"/>"),
			TEXT("</Locals>"),
			TEXT("<Events>"),
			TEXT("<Event name=\"Ping\" priority=\"5\"><Param name=\"P\" type=\"Vector\"/></Event>"),
			TEXT("</Events>"),
			TEXT("<StateMachine id=\"m\" initial=\"s\">"),
			TEXT("<State id=\"s\" tree=\"t\"/>"),
			TEXT("</StateMachine>"),
			TEXT("<Tree id=\"t\">"),
			TEXT("<Wait id=\"w\" duration=\"1\"/>"),
			TEXT("</Tree>"),
			TEXT("</Agent>"),
		};
	}

	inline FString Mutate(int32 Line, const char* Replacement)
	{
		TArray<FString> Lines = BaseLines();
		Lines[Line - 1] = ANSI_TO_TCHAR(Replacement);
		return FString::Join(Lines, TEXT("\n"));
	}

	inline FADSKCheck Has(const FADSKCompileResult& Result, EADSKDiagnosticCode Code, EADSKSeverity Severity, int32 Line, const char* NodeId = "")
	{
		const FName Id = FCStringAnsi::Strlen(NodeId) == 0 ? FName() : FName(ANSI_TO_TCHAR(NodeId));
		if (Result.Diagnostics.ContainsByPredicate([&](const FADSKDiagnostic& D)
		{
			return D.Code == Code && D.Severity == Severity && D.Line == Line && D.NodeId == Id && D.Source == TEXT("spec.xml");
		}))
		{
			return FADSKCheck::Pass();
		}
		return FADSKCheck::Fail(FString::Printf(TEXT("expected %d %s %s%s%s, got:%s"), Line, LexToString(Severity), LexToString(Code),
			Id.IsNone() ? TEXT("") : TEXT(" "), *(Id.IsNone() ? FString() : Id.ToString()), *ADSKTest::FormatDiagnostics(Result)));
	}

	inline FADSKCheck Rejects(int32 BaseLine, const char* Replacement, EADSKDiagnosticCode Code, int32 ExpectedLine, const char* NodeId = "")
	{
		const FADSKCompileResult Result = Load(Mutate(BaseLine, Replacement));
		if (Result.IsSuccess() || !Result.HasError())
		{
			return FADSKCheck::Fail(FString::Printf(TEXT("line %d [%hs] loaded, expected %s at line %d; diagnostics:%s"),
				BaseLine, Replacement, LexToString(Code), ExpectedLine, *ADSKTest::FormatDiagnostics(Result)));
		}
		FADSKCheck Found = Has(Result, Code, EADSKSeverity::Error, ExpectedLine, NodeId);
		if (!Found)
		{
			Found.Why = FString::Printf(TEXT("line %d [%hs]: "), BaseLine, Replacement) + Found.Why;
		}
		return Found;
	}

	inline FADSKCheck Accepts(int32 BaseLine, const char* Replacement)
	{
		const FADSKCompileResult Result = Load(Mutate(BaseLine, Replacement));
		if (Result.Diagnostics.IsEmpty() && Result.IsSuccess())
		{
			return FADSKCheck::Pass();
		}
		return FADSKCheck::Fail(FString::Printf(TEXT("line %d [%hs] should load cleanly; diagnostics:%s"), BaseLine, Replacement, *ADSKTest::FormatDiagnostics(Result)));
	}
}
