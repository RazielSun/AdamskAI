// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Catalogue/ADSKRegistryAssembly.h"
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Program/ADSKDefinition.h"
#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKInstance.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Trace/ADSKTrace.h"
#include <initializer_list>

/** The bool conversion is explicit so `IsTrue(Check)` does not compile and cannot drop the reason; use ADSK_ASSERT. */
struct FADSKCheck
{
	bool bPassed = true;
	FString Why;

	static FADSKCheck Pass() { return FADSKCheck(); }
	static FADSKCheck Fail(FString InWhy) { return FADSKCheck{false, MoveTemp(InWhy)}; }

	explicit operator bool() const { return bPassed; }
};

/** ASSERT_THAT for an FADSKCheck: a failure is logged with its reason and returns from the test method. */
#define ADSK_ASSERT(...) \
	if (const FADSKCheck ADSKAssertCheck = (__VA_ARGS__); !this->Assert.IsTrue(ADSKAssertCheck.bPassed, ADSKAssertCheck.Why)) { return; }

namespace ADSKTest
{
	/** For fixtures and helpers, which cannot return from the test method the way ASSERT_THAT does. */
	inline void Fail(const FString& Message)
	{
		if (FAutomationTestBase* Test = FAutomationTestFramework::Get().GetCurrentTest())
		{
			Test->AddError(Message);
			return;
		}
		UE_LOG(LogAutomationTestFramework, Error, TEXT("%s"), *Message);
	}

	inline bool Expect(bool bCondition, const FString& Why)
	{
		if (!bCondition)
		{
			Fail(Why);
		}
		return bCondition;
	}

	inline bool Expect(const FADSKCheck& Check)
	{
		return Expect(Check.bPassed, Check.Why);
	}

	inline int32 CountRecords(const FADSKRecorder* Recorder, EADSKRecordKind Kind)
	{
		int32 Found = 0;
		if (Recorder)
		{
			for (const FADSKRecord& Record : Recorder->Snapshot())
			{
				Found += Record.Kind == Kind ? 1 : 0;
			}
		}
		return Found;
	}

	inline FString FormatTrace(const FADSKRecorder* Recorder, const FADSKDefinition& Definition, int32 Tail = 40)
	{
		if (!Recorder)
		{
			return TEXT("\n(no recorder)");
		}
		const TArray<FADSKRecord> Records = Recorder->Tail(Tail);
		FString Text = FString::Printf(TEXT("\ntrace, newest %d of %d records:"), Records.Num(), Recorder->Num());
		for (const FADSKRecord& Record : Records)
		{
			Text += TEXT("\n  ") + ADSK::FormatRecord(Definition, Record);
		}
		return Text;
	}

	inline FString FormatDiagnostics(const FADSKCompileResult& Result)
	{
		if (Result.Diagnostics.IsEmpty())
		{
			return TEXT("\n  (no diagnostics)");
		}
		FString Text;
		for (const FADSKDiagnostic& Diagnostic : Result.Diagnostics)
		{
			Text += FString::Printf(TEXT("\n  %d %s %s%s: %s"), Diagnostic.Line, LexToString(Diagnostic.Severity), LexToString(Diagnostic.Code),
				Diagnostic.NodeId.IsNone() ? TEXT("") : *(TEXT(" [") + Diagnostic.NodeId.ToString() + TEXT("]")), *Diagnostic.Message);
		}
		return Text;
	}

	inline FString FormatPath(const FADSKInstance& Instance)
	{
		return FString::Join(Instance.GetActiveStatePath(), TEXT("/"));
	}

	inline FADSKCheck PathIs(const FADSKInstance& Instance, std::initializer_list<const TCHAR*> Expected, const FADSKRecorder* Recorder = nullptr)
	{
		TArray<FString> Wanted;
		for (const TCHAR* State : Expected)
		{
			Wanted.Add(State);
		}
		const FString Want = FString::Join(Wanted, TEXT("/"));
		const FString Actual = FormatPath(Instance);
		if (Actual.Equals(Want, ESearchCase::CaseSensitive))
		{
			return FADSKCheck::Pass();
		}
		return FADSKCheck::Fail(FString::Printf(TEXT("active path is [%s], expected [%s]"), *Actual, *Want)
			+ (Recorder ? FormatTrace(Recorder, *Instance.GetDefinition()) : FString()));
	}

	inline FADSKCheck Start(FADSKInstance& Instance)
	{
		const FADSKStartReport Report = Instance.Start();
		if (Report.IsSuccess())
		{
			return FADSKCheck::Pass();
		}
		return FADSKCheck::Fail(FString::Printf(TEXT("start failed: %s, missing inputs [%s]"),
			LexToString(Report.Result), *FString::Join(Report.MissingInputs, TEXT(", "))));
	}

	/** A one-state agent with no tree, compiled against an empty registry so it always loads. */
	inline TSharedRef<const FADSKDefinition> StandIn()
	{
		const TSharedRef<FADSKNodeRegistry> Empty = FADSKNodeRegistry::Create();
		Empty->Freeze();
		return FADSKCompiler::Compile(TEXT("stand-in.xml"),
			TEXT("<Agent version=\"1\" id=\"stand-in\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\"/></StateMachine></Agent>"), Empty).Definition.ToSharedRef();
	}

	/** A fixture that does not compile fails the test and yields the stand-in, so the run is not taken down by a null. */
	inline TSharedRef<const FADSKDefinition> LoadOrStandIn(const FString& SourceName, const FString& Xml, const TSharedRef<const FADSKNodeRegistry>& Registry)
	{
		const FADSKCompileResult Loaded = FADSKCompiler::Compile(SourceName, Xml, Registry);
		if (Loaded.Definition)
		{
			return Loaded.Definition.ToSharedRef();
		}
		Fail(FString::Printf(TEXT("fixture %s does not compile:%s"), *SourceName, *FormatDiagnostics(Loaded)));
		return StandIn();
	}

	/** Registers a test node; a rejection fails the running test with the reason. */
	inline void Register(FADSKNodeRegistry& Registry, FADSKNodeDescriptor Descriptor)
	{
		const FString Name = Descriptor.Name;
		const EADSKRegisterResult Result = Registry.Register(MoveTemp(Descriptor));
		Expect(Result == EADSKRegisterResult::Accepted, FString::Printf(TEXT("test node %s rejected: %s"), *Name, LexToString(Result)));
	}

	/**
	 * The core and host nodes, which no provider changes, so it is assembled once per run. When it cannot be assembled,
	 * the running test fails and gets the core nodes alone.
	 */
	inline TSharedRef<const FADSKNodeRegistry> PluginCatalogue()
	{
		static TSharedPtr<const FADSKNodeRegistry> Assembled;
		if (Assembled)
		{
			return Assembled.ToSharedRef();
		}
		TArray<FString> Errors;
		if (const TSharedPtr<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::BuildPluginCatalogue(Errors))
		{
			Assembled = Registry;
			return Registry.ToSharedRef();
		}
		Fail(TEXT("plugin catalogue did not assemble: ") + FString::Join(Errors, TEXT("; ")));
		const TSharedRef<FADSKNodeRegistry> Core = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Core);
		Core->Freeze();
		return Core;
	}
}
