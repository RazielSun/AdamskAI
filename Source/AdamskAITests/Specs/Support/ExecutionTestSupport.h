// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Runtime/ADSKInstance.h"
#include "Support/TestADSKNodes.h"
#include "Support/TestChecks.h"

struct FADSKTestRuntime
{
	explicit FADSKTestRuntime(const FString& Xml, int32 RecorderCapacity = 512)
		: Log(MakeShared<FADSKTestLeafLog>())
		, Registry(FADSKTestNodes::MakeFrozenRegistry(Log))
		, Definition(ADSKTest::LoadOrStandIn(TEXT("execution.xml"), Xml, Registry))
		, Recorder(RecorderCapacity >= 2 ? FADSKRecorder::Create(RecorderCapacity) : nullptr)
	{
		Instance = MakeUnique<FADSKInstance>(Definition.ToSharedRef(), Recorder);
	}

	int32 Count(EADSKRecordKind Kind) const
	{
		return ADSKTest::CountRecords(Recorder.Get(), Kind);
	}

	FString Trace() const
	{
		return ADSKTest::FormatTrace(Recorder.Get(), *Definition);
	}

	FADSKCheck PathIs(std::initializer_list<const TCHAR*> Expected) const
	{
		return ADSKTest::PathIs(*Instance, Expected, Recorder.Get());
	}

	FADSKCheck HasLine(const TCHAR* Fragment) const
	{
		if (Log->Lines.ContainsByPredicate([Fragment](const FString& Line) { return Line.Contains(Fragment); }))
		{
			return FADSKCheck::Pass();
		}
		return FADSKCheck::Fail(FString::Printf(TEXT("no leaf log line contains [%s]; log:\n  %s"), Fragment, *FString::Join(Log->Lines, TEXT("\n  "))));
	}

	FADSKCheck LacksLine(const TCHAR* Fragment) const
	{
		const FString* Found = Log->Lines.FindByPredicate([Fragment](const FString& Line) { return Line.Contains(Fragment); });
		return Found ? FADSKCheck::Fail(FString::Printf(TEXT("leaf log line [%s] contains [%s]"), **Found, Fragment)) : FADSKCheck::Pass();
	}

	FADSKCheck Balanced() const
	{
		if (Log->IsBalanced())
		{
			return FADSKCheck::Pass();
		}
		return FADSKCheck::Fail(FString::Printf(TEXT("%d leaf starts but %d ends; log:\n  %s"), Log->Starts, Log->Ends, *FString::Join(Log->Lines, TEXT("\n  "))) + Trace());
	}

	TSharedRef<FADSKTestLeafLog> Log;
	TSharedRef<const FADSKNodeRegistry> Registry;
	TSharedPtr<const FADSKDefinition> Definition;
	TSharedPtr<FADSKRecorder> Recorder;
	TUniquePtr<FADSKInstance> Instance;
};

inline FADSKNamedValue ADSKParam(const TCHAR* Name, const FADSKValue& Value)
{
	return {Name, Value};
}
