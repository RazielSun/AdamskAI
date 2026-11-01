// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Catalogue/ADSKRegistryAssembly.h"
#include "HAL/FileManager.h"
#include "Math/RandomStream.h"
#include "Misc/Paths.h"
#include "Settings/ADSKSettings.h"
#include "Support/ScriptedRun.h"
#include "CQTest.h"

namespace FProjectBehaviorSpecPrivate
{
	/** Floats land around both kinds of thresholds files use: probabilities in [0,1) and distances in cm. */
	FADSKValue RandomOf(const FADSKDefinition& Definition, const FADSKSlotDefinition& Slot, FRandomStream& Random)
	{
		switch (Slot.Type)
		{
		case EADSKValueType::Bool: return FADSKValue::MakeBool(Random.FRand() < 0.5f);
		case EADSKValueType::Float: return FADSKValue::MakeFloat(Random.FRand() < 0.5f ? Random.FRand() : Random.FRandRange(0.f, 400.f));
		case EADSKValueType::Int: return FADSKValue::MakeInt(Random.RandRange(0, 3));
		case EADSKValueType::Vector: return FADSKValue::MakeVector(FVector(Random.FRandRange(-1000.f, 1000.f), Random.FRandRange(-1000.f, 1000.f), 0.f));
		case EADSKValueType::Enum: return FADSKValue::MakeEnum(static_cast<int16>(Slot.Enum), Random.RandRange(0, Definition.GetEnums()[Slot.Enum].ValueCount - 1));
		case EADSKValueType::Object: return FADSKValue::MakeObject(Random.RandRange(0, 2));
		}
		return FADSKValue();
	}
}

using namespace FProjectBehaviorSpecPrivate;

/** Each episode leaves one event out, so a final state does not end them all. Proves only start and no hold. */
TEST_CLASS_WITH_FLAGS(FProjectBehaviorSpec, "AdamskAI.Project.Smoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(EveryProjectBehaviorFileStartsAndNeverHolds)
	{
		TArray<FString> Errors;
		const TSharedPtr<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::TryGet(Errors);
		ASSERT_THAT(IsTrue(Registry.IsValid(), TEXT("the catalogue did not assemble: ") + FString::Join(Errors, TEXT("; "))));
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *UADSKSettings::GetBehaviorRootDir(), TEXT("*.xml"), true, false);
		Files.Sort();
		if (Files.IsEmpty())
		{
			TestRunner->AddInfo(TEXT("the project has no behavior files"));
		}
		for (const FString& File : Files)
		{
			const FString Name = FPaths::GetCleanFilename(File);
			TSet<FString> Seen;
			int32 LeafStates = 0;
			for (int32 Skipped = INDEX_NONE; ; ++Skipped)
			{
				FADSKScriptedRun Run(File, Registry.ToSharedRef(), Skipped + 2);
				ADSK_ASSERT(Run.Loaded());
				const FADSKDefinition& Definition = *Run.Definition;
				TArray<int32> Events;
				for (int32 Event = 0; Event < Definition.GetEvents().Num(); ++Event)
				{
					if (!Definition.GetEvents()[Event].IsRaisedByCore() && Event != Skipped) { Events.Add(Event); }
				}
				if (Skipped >= Definition.GetEvents().Num()) { break; }
				LeafStates = 0;
				for (const FADSKStateDefinition& State : Definition.GetStates()) { LeafStates += State.ChildCount == 0; }

				FRandomStream Random(Skipped + 100);
				auto Randomize = [&]
				{
					for (const FADSKSlotDefinition& Slot : Definition.GetSlots())
					{
						if (Slot.bIsInput) { Run.Instance->SetInput(Slot.Name, RandomOf(Definition, Slot, Random)); }
					}
				};
				Randomize();
				ADSK_ASSERT(ADSKTest::Start(*Run.Instance));
				for (int32 Tick = 0; Tick < 1200; ++Tick)
				{
					if (Tick % 17 == 0) { Randomize(); }
					// only doubles: a core leaf such as Wait finishes by its own rules
					if (Tick % 7 == 0 && Run.IsDoubleActive())
					{
						Run.Finish(Random.FRand() < 0.7f ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Failed);
					}
					if (!Events.IsEmpty() && Tick % 23 == 0)
					{
						const FADSKEventDefinition& Event = Definition.GetEvents()[Events[Random.RandRange(0, Events.Num() - 1)]];
						TArray<FADSKNamedValue> Parameters;
						for (int32 Param = 0; Param < Event.ParamCount; ++Param)
						{
							const FADSKEventParamDefinition& Declared = Definition.GetEventParams()[Event.FirstParam + Param];
							Parameters.Add({Declared.Name, FADSKValue::MakeZero(Declared.Type)});
						}
						Run.Instance->EnqueueEvent(Event.Name, Parameters);
					}
					Run.Tick(0.1f);
					Seen.Add(Run.State());
				}
				if (Run.bEverHeld) { TestRunner->AddError(FString::Printf(TEXT("%s (episode %d) held in state '%s'"), *Name, Skipped, *Run.HeldIn)); }
				ASSERT_THAT(IsFalse(Run.bEverHeld, FString::Printf(TEXT("%s held in state '%s'"), *Name, *Run.HeldIn)));
			}
			TestRunner->AddInfo(FString::Printf(TEXT("%s reached %d of %d leaf states"), *Name, Seen.Num(), LeafStates));
		}
	}
};

#endif
