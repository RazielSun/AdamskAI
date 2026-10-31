// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Compiler/ADSKCompiler.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Runtime/ADSKInstance.h"
#include "Support/TempTree.h"
#include "Support/TestChecks.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "CQTest.h"

namespace FConformanceSpecPrivate
{
	/** docs/semantics.md: seed 1, 0.1 s ticks, 100 simulated seconds. */
	constexpr int32 MaxTicks = 1000;
	constexpr float Step = 0.1f;

	struct FRun
	{
		bool bPassed = false;
		/** The tick `pass` was entered on; 0 is the Start. */
		int32 PassTick = INDEX_NONE;
		FString Failure;
	};

	/** Core nodes only: conformance files test the language, not a host. */
	TSharedRef<const FADSKNodeRegistry> CoreRegistry()
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		Registry->Freeze();
		return Registry;
	}

	FRun Run(const FString& Name, const FString& Text, const TSharedRef<const FADSKNodeRegistry>& Registry)
	{
		FRun Result;
		const FADSKCompileResult Compiled = FADSKCompiler::Compile(Name, Text, Registry);
		if (!Compiled.IsSuccess() || !Compiled.Diagnostics.IsEmpty())
		{
			Result.Failure = TEXT("does not compile cleanly:") + ADSKTest::FormatDiagnostics(Compiled);
			return Result;
		}
		const TSharedRef<const FADSKDefinition> Definition = Compiled.Definition.ToSharedRef();
		if (Definition->FindState(TEXT("pass")) == INDEX_NONE)
		{
			Result.Failure = TEXT("has no state named pass");
			return Result;
		}
		const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(512);
		FADSKInstance Instance(Definition, Recorder);
		Instance.SetRandomSeed(1);
		const FADSKStartReport Report = Instance.Start();
		if (!Report.IsSuccess())
		{
			Result.Failure = FString::Printf(TEXT("did not start: %s"), LexToString(Report.Result));
			return Result;
		}
		for (int32 Tick = 0; Tick <= MaxTicks; ++Tick)
		{
			if (Tick > 0)
			{
				Instance.Tick(Step);
			}
			if (!Instance.IsRunning())
			{
				Result.Failure = FString::Printf(TEXT("stopped at tick %d"), Tick) + ADSKTest::FormatTrace(Recorder.Get(), *Definition);
				return Result;
			}
			const TArray<FString> Path = Instance.GetActiveStatePath();
			if (Path.Last() == TEXT("pass"))
			{
				Result.bPassed = true;
				Result.PassTick = Tick;
				return Result;
			}
			if (Path.Last() == TEXT("fail"))
			{
				Result.Failure = FString::Printf(TEXT("entered fail at tick %d"), Tick) + ADSKTest::FormatTrace(Recorder.Get(), *Definition);
				return Result;
			}
		}
		Result.Failure = FString::Printf(TEXT("no pass in %d ticks; at %s%s"), MaxTicks, *FString::Join(Instance.GetActiveStatePath(), TEXT("/")),
			Instance.IsHolding() ? TEXT(", holding") : TEXT("")) + ADSKTest::FormatTrace(Recorder.Get(), *Definition);
		return Result;
	}

	FString Directory()
	{
		return FADSKTestPaths::PluginBase() / TEXT("Fixtures/conformance");
	}

	TArray<FString> Files()
	{
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(Directory() / TEXT("*.xml")), true, false);
		Names.Sort();
		return Names;
	}

	/** A probe file whose tree emits Go once, with Machine as the state machine's children. */
	FString Probe(const TCHAR* Machine)
	{
		return TEXT("<Agent version=\"1\" id=\"probe\"><Events><Event name=\"Go\"/></Events><StateMachine id=\"m\" initial=\"s\">") + FString(Machine)
			+ TEXT("</StateMachine><Tree id=\"emit\"><Sequence id=\"emit-then-wait\"><Emit id=\"emit-go\" event=\"Go\"/>"
				"<Wait id=\"hold-on\" duration=\"100\"/></Sequence></Tree></Agent>");
	}
}

using namespace FConformanceSpecPrivate;

TEST_CLASS_WITH_FLAGS(FConformanceSpec, "AdamskAI.Conformance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(EveryFileReachesPassTheSameWayTwice)
	{
		const TSharedRef<const FADSKNodeRegistry> Registry = CoreRegistry();
		const TArray<FString> Names = Files();
		ASSERT_THAT(IsTrue(Names.Num() >= 15, FString::Printf(TEXT("only %d files in %s"), Names.Num(), *Directory())));
		TArray<FString> Failures;
		for (const FString& Name : Names)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *(Directory() / Name)))
			{
				Failures.Add(Name + TEXT(": unreadable"));
				continue;
			}
			const FRun First = Run(Name, Text, Registry);
			if (!First.bPassed)
			{
				Failures.Add(Name + TEXT(": ") + First.Failure);
				continue;
			}
			const FRun Second = Run(Name, Text, Registry);
			if (Second.PassTick != First.PassTick)
			{
				Failures.Add(FString::Printf(TEXT("%s: passed at tick %d, then at %d (%s)"), *Name, First.PassTick, Second.PassTick, *Second.Failure));
			}
		}
		ASSERT_THAT(IsTrue(Failures.IsEmpty(), FString::Join(Failures, TEXT("\n"))));
	}

	TEST_METHOD(TheRunnerReportsEveryWayToFail)
	{
		const TSharedRef<const FADSKNodeRegistry> Registry = CoreRegistry();
		auto Fails = [&](const TCHAR* Machine, const TCHAR* Expected)
		{
			const FRun Result = Run(TEXT("probe.xml"), Probe(Machine), Registry);
			return !Result.bPassed && Result.Failure.Contains(Expected) ? FADSKCheck::Pass()
				: FADSKCheck::Fail(FString::Printf(TEXT("expected a failure containing [%s], got passed=%d [%s]"), Expected, Result.bPassed, *Result.Failure));
		};
		ADSK_ASSERT(Fails(TEXT("<State id=\"s\" tree=\"emit\"><On event=\"Go\" goto=\"fail\"/></State><State id=\"fail\"/><State id=\"pass\"/>"), TEXT("entered fail at tick 1")));
		ADSK_ASSERT(Fails(TEXT("<State id=\"s\" tree=\"emit\"/><State id=\"pass\"/>"), TEXT("no pass in 1000 ticks; at m/s")));
		ADSK_ASSERT(Fails(TEXT("<State id=\"s\" tree=\"emit\"><On event=\"Go\" goto=\"done\"/></State><State id=\"done\"/>"), TEXT("has no state named pass")));
		ADSK_ASSERT(Fails(TEXT("<State id=\"s\" tree=\"emit\"><On event=\"Nope\" goto=\"pass\"/></State><State id=\"pass\"/>"), TEXT("does not compile cleanly")));
		const FRun Passed = Run(TEXT("probe.xml"), Probe(TEXT("<State id=\"s\" tree=\"emit\"><On event=\"Go\" goto=\"pass\"/></State><State id=\"pass\"/>")), Registry);
		ASSERT_THAT(IsTrue(Passed.bPassed && Passed.PassTick == 1, FString::Printf(TEXT("passed=%d at tick %d: %s"), Passed.bPassed, Passed.PassTick, *Passed.Failure)));
	}
};

#endif
