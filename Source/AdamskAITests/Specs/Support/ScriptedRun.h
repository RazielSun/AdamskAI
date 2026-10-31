// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Misc/FileHelper.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKInstance.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Support/Golden.h"
#include "Support/TestChecks.h"
#include "Trace/ADSKTrace.h"

/**
 * Non-core leaves and scopes keep their descriptors, so a file compiles exactly as shipped, but run doubles: a leaf
 * stays Running until the test finishes it, and on success writes ScriptedPoint to its Vector out ports.
 */
namespace FADSKScriptedCatalogue
{
	inline const FVector ScriptedPoint(100.0, 200.0, 0.0);

	inline TSharedRef<const FADSKNodeRegistry> Make(const FADSKNodeRegistry& Source, TSet<FString>* OutDoubles = nullptr)
	{
		const TSharedRef<FADSKNodeRegistry> Core = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Core);
		const TSharedRef<FADSKNodeRegistry> Out = FADSKNodeRegistry::Create();
		for (const FADSKNodeDescriptor* Original : Source.GetDescriptors())
		{
			FADSKNodeDescriptor Copy = *Original;
			const bool bDouble = !Core->Find(Copy.Name) && (Copy.Kind == EADSKNodeKind::Leaf || Copy.Kind == EADSKNodeKind::Scope);
			if (bDouble)
			{
				if (OutDoubles) { OutDoubles->Add(Copy.Name); }
				Copy.ValidateAssets = nullptr;
				Copy.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Running; };
				Copy.OnUpdate = nullptr;
				Copy.OnEnd = nullptr;
				if (Copy.Kind == EADSKNodeKind::Leaf)
				{
					Copy.OnEnd = [Ports = Copy.Ports](FADSKNodeContext& Context, EADSKEndReason Reason)
					{
						for (int32 Port = 0; Reason == EADSKEndReason::Succeeded && Port < Ports.Num(); ++Port)
						{
							if (Ports[Port].Direction == EADSKPortDirection::Out && Ports[Port].Type == EADSKValueType::Vector)
							{
								Context.WritePort(Port, FADSKValue::MakeVector(ScriptedPoint));
							}
						}
					};
				}
			}
			ADSKTest::Register(*Out, MoveTemp(Copy));
		}
		Out->Freeze();
		return Out;
	}
}

/** A file that does not load fails the test and runs the stand-in, so the scenario fails on its assertions, not a crash. */
struct FADSKScriptedRun
{
	FADSKScriptedRun(const FString& File, const TSharedRef<const FADSKNodeRegistry>& Source, int32 Seed = 7)
		: Recorder(FADSKRecorder::Create(4096))
		, Definition(ADSKTest::StandIn())
	{
		FString Text;
		if (!ADSKTest::Expect(FFileHelper::LoadFileToString(Text, *File), TEXT("cannot read ") + File))
		{
			Result.Diagnostics.Add({EADSKDiagnosticCode::XmlMalformed, EADSKSeverity::Error, File, 0, NAME_None, TEXT("cannot read the file")});
		}
		else
		{
			Result = FADSKCompiler::Compile(File, Text, FADSKScriptedCatalogue::Make(*Source, &Doubles));
			if (Result.Definition)
			{
				Definition = Result.Definition.ToSharedRef();
			}
			else
			{
				ADSKTest::Fail(FString::Printf(TEXT("%s does not compile:%s"), *File, *ADSKTest::FormatDiagnostics(Result)));
			}
		}
		Instance = MakeUnique<FADSKInstance>(Definition, Recorder);
		Instance->SetRandomSeed(Seed);
	}

	FADSKCheck Loaded() const
	{
		return Result.IsSuccess() ? FADSKCheck::Pass() : FADSKCheck::Fail(TEXT("the behavior file did not load; its diagnostics are reported above"));
	}

	/** True when the deepest running node is a double, which only the test can finish. */
	bool IsDoubleActive() const
	{
		const int32 Node = Definition->FindNode(Instance->GetActiveNodeId());
		return Node != INDEX_NONE && Instance->GetActiveLeafHandle().IsValid() && Doubles.Contains(Definition->GetNodes()[Node].Descriptor->Name);
	}
	FString Active() const { return Instance->GetActiveNodeId().ToString(); }
	FString State() const
	{
		const TArray<FString> Path = Instance->GetActiveStatePath();
		return Path.IsEmpty() ? FString() : Path.Last();
	}

	FADSKCheck Finish(EADSKNodeStatus Status)
	{
		const FADSKActivationHandle Handle = Instance->GetActiveLeafHandle();
		if (!Handle.IsValid())
		{
			return FADSKCheck::Fail(TEXT("no leaf is running to finish; ") + Where());
		}
		const EADSKInputResult Posted = Instance->PostActionResult(Handle, Status);
		if (Posted != EADSKInputResult::Accepted)
		{
			return FADSKCheck::Fail(FString::Printf(TEXT("the result was refused: %s; "), LexToString(Posted)) + Where());
		}
		Tick(0.f);
		return FADSKCheck::Pass();
	}

	FADSKCheck RunUntil(TFunctionRef<bool()> Done, float Step = 0.25f, int32 MaxTicks = 200)
	{
		for (int32 Index = 0; Index < MaxTicks; ++Index)
		{
			if (Done())
			{
				return FADSKCheck::Pass();
			}
			Tick(Step);
		}
		return Done() ? FADSKCheck::Pass() : FADSKCheck::Fail(FString::Printf(TEXT("still waiting after %d ticks; "), MaxTicks) + Where());
	}

	FString Where() const
	{
		return FString::Printf(TEXT("state [%s], active node [%s]"), *ADSKTest::FormatPath(*Instance), *Active())
			+ ADSKTest::FormatTrace(Recorder.Get(), *Definition);
	}

	void Tick(float Delta)
	{
		Instance->Tick(Delta);
		if (Instance->IsHolding() && !bEverHeld) { HeldIn = State(); }
		bEverHeld |= Instance->IsHolding();
	}

	int32 Count(EADSKRecordKind Kind) const
	{
		return ADSKTest::CountRecords(Recorder.Get(), Kind);
	}

	/** The whole trace so far against Fixtures/golden/<Name>.trace. */
	FADSKCheck MatchesGolden(const TCHAR* Name) const
	{
		return ADSKTest::MatchesGolden(Name, *Recorder, *Definition);
	}

	TSharedPtr<FADSKRecorder> Recorder;
	FADSKCompileResult Result;
	/** The compiled file, or the stand-in when it did not load. */
	TSharedRef<const FADSKDefinition> Definition;
	TUniquePtr<FADSKInstance> Instance;
	TSet<FString> Doubles;
	/** A Hold is almost always an unhandled completion; every scenario checks it never happened. */
	bool bEverHeld = false;
	FString HeldIn;
};
