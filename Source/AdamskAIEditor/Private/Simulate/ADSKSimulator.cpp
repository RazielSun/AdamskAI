// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Simulate/ADSKSimulator.h"

#include "Simulate/ADSKSimulatorMemory.h"
#include "Algo/StableSort.h"
#include "Compiler/ADSKCompiler.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Program/ADSKDefinition.h"
#include "Runtime/ADSKInstance.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Trace/ADSKTrace.h"

namespace ADSKSimulatorPrivate
{
	constexpr int32 MaxPrintedTransitions = 200;
	constexpr float DefaultAfter = 0.5f;

	/** How a simulated leaf ends: its status after `After` seconds, writing Out (port name to JSON value) on success. */
	struct FLeafPlan
	{
		EADSKNodeStatus Result = EADSKNodeStatus::Succeeded;
		float After = DefaultAfter;
		TSharedPtr<FJsonObject> Out;
	};

	/** Shared by every double of one run; ids are lower-case, as node ids are unique ignoring case. */
	struct FLeafPlans
	{
		TMap<FString, FLeafPlan> ById;
		TMap<FString, FLeafPlan> ByType;
		TOptional<FLeafPlan> Fallback;
		TSet<FString> Defaulted;

		const FLeafPlan& Find(FName NodeId, const FString& Type)
		{
			const FString Id = NodeId.ToString().ToLower();
			if (const FLeafPlan* Plan = ById.Find(Id)) return *Plan;
			if (const FLeafPlan* Plan = ByType.Find(Type)) return *Plan;
			if (Fallback.IsSet()) return *Fallback;
			Defaulted.Add(NodeId.ToString());
			static const FLeafPlan Default;
			return Default;
		}
	};

	struct FTimedChange
	{
		double At = 0.0;
		TSharedPtr<FJsonObject> Inputs;
		FString Event;
		TSharedPtr<FJsonObject> Params;
	};

	struct FScenario
	{
		FString Behavior;
		int32 Seed = 1;
		float Step = 1.f / 30.f;
		double Duration = 30.0;
		TSharedPtr<FJsonObject> Inputs;
		TSharedPtr<FJsonObject> Leaves;
		TArray<FTimedChange> Timeline;
		int32 MaxTransitionsPerSecond = 10;
		bool bHoldAllowed = false;
		TArray<FString> Reach;
		TArray<FString> Never;
	};

	bool CheckKeys(const FJsonObject& Object, TConstArrayView<const TCHAR*> Known, const TCHAR* Where, FString& OutError)
	{
		for (const auto& Pair : Object.Values)
		{
			// FJsonObject keys are FString or TSharedString depending on UE_JSONOBJECT_LEGACY_STRING_KEYS.
			const FString Key(*Pair.Key);
			if (!Known.ContainsByPredicate([&Key](const TCHAR* Candidate) { return Key.Equals(Candidate, ESearchCase::CaseSensitive); }))
			{
				OutError = FString::Printf(TEXT("%s: unknown key '%s'"), Where, *Key);
				return false;
			}
		}
		return true;
	}

	bool ReadStrings(const FJsonObject& Object, const TCHAR* Key, TArray<FString>& Out, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.HasField(Key)) return true;
		if (!Object.TryGetArrayField(Key, Values)) { OutError = FString::Printf(TEXT("expect.%s must be an array of state ids"), Key); return false; }
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Text;
			if (!Value->TryGetString(Text)) { OutError = FString::Printf(TEXT("expect.%s must be an array of state ids"), Key); return false; }
			Out.Add(Text);
		}
		return true;
	}

	bool ParseScenario(const FString& Text, FScenario& Out, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			OutError = TEXT("not a JSON object");
			return false;
		}
		const TCHAR* Keys[] = {TEXT("behavior"), TEXT("seed"), TEXT("step"), TEXT("duration"), TEXT("inputs"), TEXT("leaves"), TEXT("timeline"), TEXT("expect")};
		if (!CheckKeys(*Root, Keys, TEXT("scenario"), OutError)) return false;
		if (!Root->TryGetStringField(TEXT("behavior"), Out.Behavior) || Out.Behavior.IsEmpty())
		{
			OutError = TEXT("behavior (the .xml file, relative to the scenario) is required");
			return false;
		}
		double Number = 0.0;
		if (Root->TryGetNumberField(TEXT("seed"), Number)) Out.Seed = static_cast<int32>(Number);
		if (Root->TryGetNumberField(TEXT("step"), Number)) Out.Step = static_cast<float>(Number);
		if (Root->TryGetNumberField(TEXT("duration"), Number)) Out.Duration = Number;
		if (!(Out.Step > 0.f) || !(Out.Duration > 0.0) || Out.Duration / Out.Step > 1e6)
		{
			OutError = TEXT("step and duration must be positive, with at most a million steps");
			return false;
		}
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Root->TryGetObjectField(TEXT("inputs"), Object)) Out.Inputs = *Object;
		if (Root->TryGetObjectField(TEXT("leaves"), Object)) Out.Leaves = *Object;
		const TArray<TSharedPtr<FJsonValue>>* Timeline = nullptr;
		if (Root->TryGetArrayField(TEXT("timeline"), Timeline))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Timeline)
			{
				const TSharedPtr<FJsonObject>* Entry = nullptr;
				if (!Value->TryGetObject(Entry)) { OutError = TEXT("timeline entries must be objects"); return false; }
				const TCHAR* EntryKeys[] = {TEXT("at"), TEXT("inputs"), TEXT("event"), TEXT("params")};
				if (!CheckKeys(**Entry, EntryKeys, TEXT("timeline entry"), OutError)) return false;
				FTimedChange Change;
				if (!(*Entry)->TryGetNumberField(TEXT("at"), Change.At)) { OutError = TEXT("every timeline entry needs at (seconds)"); return false; }
				if ((*Entry)->TryGetObjectField(TEXT("inputs"), Object)) Change.Inputs = *Object;
				(*Entry)->TryGetStringField(TEXT("event"), Change.Event);
				if ((*Entry)->TryGetObjectField(TEXT("params"), Object)) Change.Params = *Object;
				if (!Change.Inputs.IsValid() && Change.Event.IsEmpty()) { OutError = FString::Printf(TEXT("timeline entry at %g has neither inputs nor event"), Change.At); return false; }
				Out.Timeline.Add(MoveTemp(Change));
			}
			Algo::StableSortBy(Out.Timeline, &FTimedChange::At);
		}
		if (Root->TryGetObjectField(TEXT("expect"), Object))
		{
			const TCHAR* ExpectKeys[] = {TEXT("maxTransitionsPerSecond"), TEXT("hold"), TEXT("reach"), TEXT("never")};
			if (!CheckKeys(**Object, ExpectKeys, TEXT("expect"), OutError)) return false;
			if ((*Object)->TryGetNumberField(TEXT("maxTransitionsPerSecond"), Number)) Out.MaxTransitionsPerSecond = static_cast<int32>(Number);
			(*Object)->TryGetBoolField(TEXT("hold"), Out.bHoldAllowed);
			if (!ReadStrings(**Object, TEXT("reach"), Out.Reach, OutError) || !ReadStrings(**Object, TEXT("never"), Out.Never, OutError)) return false;
		}
		return true;
	}

	/** A JSON literal of the given type: a number, a bool, `"x,y,z"` or `[x,y,z]`, an Object id (0 or "none" is none). */
	bool ToValue(EADSKValueType Type, const FJsonValue& Json, FADSKValue& Out)
	{
		double Number = 0.0;
		bool bBool = false;
		FString Text;
		switch (Type)
		{
		case EADSKValueType::Bool:
			if (!Json.TryGetBool(bBool)) return false;
			Out = FADSKValue::MakeBool(bBool);
			return true;
		case EADSKValueType::Float:
			if (!Json.TryGetNumber(Number)) return false;
			Out = FADSKValue::MakeFloat(static_cast<float>(Number));
			return true;
		case EADSKValueType::Int:
			if (!Json.TryGetNumber(Number) || Number != FMath::RoundToDouble(Number) || FMath::Abs(Number) > MAX_int32) return false;
			Out = FADSKValue::MakeInt(static_cast<int32>(Number));
			return true;
		case EADSKValueType::Vector:
		{
			FVector Vector;
			const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
			if (Json.TryGetArray(Parts) && Parts->Num() == 3)
			{
				Vector = FVector((*Parts)[0]->AsNumber(), (*Parts)[1]->AsNumber(), (*Parts)[2]->AsNumber());
			}
			else if (!Json.TryGetString(Text) || !ADSK::TryParseVector(Text, Vector))
			{
				return false;
			}
			Out = FADSKValue::MakeVector(Vector);
			return true;
		}
		case EADSKValueType::Object:
			if (Json.TryGetString(Text) && Text == TEXT("none")) { Out = FADSKValue::MakeObject(0); return true; }
			if (!Json.TryGetNumber(Number) || Number < 0 || Number != FMath::RoundToDouble(Number)) return false;
			Out = FADSKValue::MakeObject(static_cast<uint64>(Number));
			return true;
		case EADSKValueType::Enum:
			break;
		}
		return false;
	}

	EADSKNodeStatus FinishLeaf(FADSKNodeContext& Context, const FLeafPlan& Plan, TConstArrayView<FADSKPortDescriptor> Ports)
	{
		if (Plan.Result == EADSKNodeStatus::Succeeded && Plan.Out.IsValid())
		{
			for (int32 Port = 0; Port < Ports.Num(); ++Port)
			{
				const TSharedPtr<FJsonValue> Json = Plan.Out->TryGetField(Ports[Port].Name);
				FADSKValue Value;
				if (Ports[Port].Direction == EADSKPortDirection::Out && Json.IsValid() && ToValue(Ports[Port].Type, *Json, Value))
				{
					Context.WritePort(Port, Value);
				}
			}
		}
		return Plan.Result;
	}

	/** Non-core leaves and scopes doubled: leaves end as planned, scopes only run their child. */
	TSharedPtr<const FADSKNodeRegistry> MakeDoubles(const FADSKNodeRegistry& Source, const TSharedRef<FLeafPlans>& Plans, TSet<FString>& OutLeaves, FString& OutError)
	{
		const TSharedRef<FADSKNodeRegistry> Core = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Core);
		const TSharedRef<FADSKNodeRegistry> Out = FADSKNodeRegistry::Create();
		for (const FADSKNodeDescriptor* Original : Source.GetDescriptors())
		{
			FADSKNodeDescriptor Copy = *Original;
			if (!Core->Find(Copy.Name) && Copy.Kind == EADSKNodeKind::Leaf)
			{
				OutLeaves.Add(Copy.Name);
				Copy.ValidateAssets = nullptr;
				Copy.MemoryStruct = FADSKSimLeafMemory::StaticStruct();
				Copy.OnStart = [Plans, Type = Copy.Name, Ports = Copy.Ports](FADSKNodeContext& Context)
				{
					Context.GetMemory<FADSKSimLeafMemory>().Elapsed = 0.f;
					const FLeafPlan& Plan = Plans->Find(Context.GetNodeId(), Type);
					return Plan.After <= 0.f ? FinishLeaf(Context, Plan, Ports) : EADSKNodeStatus::Running;
				};
				Copy.OnUpdate = [Plans, Type = Copy.Name, Ports = Copy.Ports](FADSKNodeContext& Context, float Delta)
				{
					float& Elapsed = Context.GetMemory<FADSKSimLeafMemory>().Elapsed;
					Elapsed += Delta;
					const FLeafPlan& Plan = Plans->Find(Context.GetNodeId(), Type);
					return Elapsed >= Plan.After - UE_KINDA_SMALL_NUMBER ? FinishLeaf(Context, Plan, Ports) : EADSKNodeStatus::Running;
				};
				Copy.OnEnd = nullptr;
			}
			else if (!Core->Find(Copy.Name) && Copy.Kind == EADSKNodeKind::Scope)
			{
				Copy.ValidateAssets = nullptr;
				Copy.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Running; };
				Copy.OnUpdate = nullptr;
				Copy.OnEnd = nullptr;
			}
			const FString Name = Copy.Name;
			const EADSKRegisterResult Result = Out->Register(MoveTemp(Copy));
			if (Result != EADSKRegisterResult::Accepted)
			{
				OutError = FString::Printf(TEXT("cannot double node %s: %s"), *Name, LexToString(Result));
				return nullptr;
			}
		}
		Out->Freeze();
		return Out;
	}

	bool ParsePlan(const FJsonValue& Json, const FString& Key, FLeafPlan& Out, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Json.TryGetObject(Object)) { OutError = FString::Printf(TEXT("leaves.%s must be an object"), *Key); return false; }
		const TCHAR* Keys[] = {TEXT("result"), TEXT("after"), TEXT("out")};
		if (!CheckKeys(**Object, Keys, *(TEXT("leaves.") + Key), OutError)) return false;
		FString Result;
		if ((*Object)->TryGetStringField(TEXT("result"), Result))
		{
			if (Result == TEXT("Succeeded")) Out.Result = EADSKNodeStatus::Succeeded;
			else if (Result == TEXT("Failed")) Out.Result = EADSKNodeStatus::Failed;
			else { OutError = FString::Printf(TEXT("leaves.%s.result must be Succeeded or Failed"), *Key); return false; }
		}
		double After = 0.0;
		if ((*Object)->TryGetNumberField(TEXT("after"), After)) Out.After = static_cast<float>(After);
		const TSharedPtr<FJsonObject>* OutPorts = nullptr;
		if ((*Object)->TryGetObjectField(TEXT("out"), OutPorts)) Out.Out = *OutPorts;
		return true;
	}

	/** Keys are a doubled leaf's node id, a doubled leaf type, or `*`; anything else is a typo worth stopping on. */
	bool ParsePlans(const FScenario& Scenario, const FADSKDefinition& Definition, const TSet<FString>& LeafTypes, FLeafPlans& Plans, FString& OutError)
	{
		if (!Scenario.Leaves.IsValid()) return true;
		TMap<FString, const FADSKNodeDefinition*> LeafNodes;
		for (const FADSKNodeDefinition& Node : Definition.GetNodes())
		{
			if (LeafTypes.Contains(Node.Descriptor->Name)) LeafNodes.Add(Node.AuthoredId.ToLower(), &Node);
		}
		for (const auto& Pair : Scenario.Leaves->Values)
		{
			const FString Key(*Pair.Key);
			FLeafPlan Plan;
			if (!ParsePlan(*Pair.Value, Key, Plan, OutError)) return false;
			const FADSKNodeDescriptor* Descriptor = nullptr;
			if (Key == TEXT("*")) { Plans.Fallback = Plan; continue; }
			if (const FADSKNodeDefinition* const* Node = LeafNodes.Find(Key.ToLower())) { Descriptor = (*Node)->Descriptor; Plans.ById.Add(Key.ToLower(), Plan); }
			else if (LeafTypes.Contains(Key)) { Descriptor = Definition.GetRegistry()->Find(Key); Plans.ByType.Add(Key, Plan); }
			else { OutError = FString::Printf(TEXT("leaves.%s names no host or project leaf of this file (a node id, a node type or *)"), *Key); return false; }
			if (!Plan.Out.IsValid()) continue;
			for (const auto& Port : Plan.Out->Values)
			{
				const int32 Index = Descriptor ? Descriptor->FindPort(FString(*Port.Key)) : INDEX_NONE;
				FADSKValue Value;
				if (Index == INDEX_NONE || Descriptor->Ports[Index].Direction != EADSKPortDirection::Out || !ToValue(Descriptor->Ports[Index].Type, *Port.Value, Value))
				{
					OutError = FString::Printf(TEXT("leaves.%s.out.%s is not an out port of that node, or not a literal of its type"), *Key, *Port.Key);
					return false;
				}
			}
		}
		return true;
	}

	/** Writes each named Input; an enum takes its value name. */
	bool ApplyInputs(const FJsonObject& Inputs, const FADSKDefinition& Definition, FADSKInstance& Instance, FString& OutError)
	{
		for (const auto& Pair : Inputs.Values)
		{
			const FString Key(*Pair.Key);
			const int32 Slot = Definition.FindSlot(Key);
			if (Slot == INDEX_NONE || !Definition.GetSlots()[Slot].bIsInput) { OutError = FString::Printf(TEXT("inputs.%s is not an Input of this file"), *Key); return false; }
			const EADSKValueType Type = Definition.GetSlots()[Slot].Type;
			EADSKInputResult Result = EADSKInputResult::TypeMismatch;
			FString Text;
			FADSKValue Value;
			if (Type == EADSKValueType::Enum)
			{
				if (Pair.Value->TryGetString(Text)) Result = Instance.SetEnumInput(Key, Text);
			}
			else if (ToValue(Type, *Pair.Value, Value))
			{
				Result = Instance.SetInput(Key, Value);
			}
			if (Result != EADSKInputResult::Accepted)
			{
				OutError = FString::Printf(TEXT("inputs.%s: not a %s literal (%s)"), *Key, LexToString(Type), LexToString(Result));
				return false;
			}
		}
		return true;
	}

	bool MakeEventParams(const FTimedChange& Change, const FADSKDefinition& Definition, TArray<FADSKNamedValue>& Out, FString& OutError)
	{
		const int32 Event = Definition.FindEvent(Change.Event);
		if (Event == INDEX_NONE || Definition.GetEvents()[Event].IsRaisedByCore())
		{
			OutError = FString::Printf(TEXT("timeline event %s is not a declared event the host may raise"), *Change.Event);
			return false;
		}
		if (!Change.Params.IsValid()) return true;
		const FADSKEventDefinition& Declared = Definition.GetEvents()[Event];
		for (const auto& Pair : Change.Params->Values)
		{
			const FString Key(*Pair.Key);
			const FADSKEventParamDefinition* Param = nullptr;
			for (int32 Index = 0; Index < Declared.ParamCount; ++Index)
			{
				const FADSKEventParamDefinition& Candidate = Definition.GetEventParams()[Declared.FirstParam + Index];
				if (Candidate.Name.Equals(Key, ESearchCase::CaseSensitive)) Param = &Candidate;
			}
			FADSKNamedValue& Named = Out.AddDefaulted_GetRef();
			Named.Name = Key;
			bool bValid = false;
			FString Text;
			if (Param && Param->Type == EADSKValueType::Enum && Pair.Value->TryGetString(Text))
			{
				const TOptional<FADSKValue> Enum = Definition.MakeEnumValue(Definition.GetEnums()[Param->Enum].Name, Text);
				bValid = Enum.IsSet();
				if (bValid) Named.Value = *Enum;
			}
			else if (Param)
			{
				bValid = ToValue(Param->Type, *Pair.Value, Named.Value);
			}
			if (!bValid)
			{
				OutError = FString::Printf(TEXT("timeline event %s: params.%s is not a parameter of it, or not a literal of its type"), *Change.Event, *Key);
				return false;
			}
		}
		return true;
	}

	FString StateId(const FADSKDefinition& Definition, int32 State)
	{
		return Definition.GetStates().IsValidIndex(State) ? Definition.GetStates()[State].Id : FString(TEXT("?"));
	}
	/** What a run observed: states entered, transitions, the busiest one-second window and where it first held. */
	struct FRunStats
	{
		TSet<FString> Entered;
		TArray<double> Window;
		int32 Transitions = 0;
		int32 Printed = 0;
		int32 Worst = 0;
		double WorstFrom = 0.0;
		TArray<FString> WorstPath;
		TArray<FString> RecentPath;
		FString HeldIn;

		/** Moves the recorder's records into the trace and these numbers, then clears it. */
		void Drain(FADSKRecorder& Recorder, const FADSKDefinition& Definition, double Time, FADSKSimulationReport& Report)
		{
			for (const FADSKRecord& Record : Recorder.Snapshot())
			{
				Report.Trace += FString::Printf(TEXT("%9.3f %s\n"), Time, *ADSK::FormatRecord(Definition, Record));
				if (Record.Kind == EADSKRecordKind::StateEntered) Entered.Add(StateId(Definition, Record.State));
				if (Record.Kind != EADSKRecordKind::TransitionTaken || !Definition.GetTransitions().IsValidIndex(Record.Transition)) continue;
				const FADSKTransitionDefinition& Taken = Definition.GetTransitions()[Record.Transition];
				if (Taken.Target == INDEX_NONE) continue;
				++Transitions;
				const FString Event = Definition.GetEvents().IsValidIndex(Taken.Event) ? Definition.GetEvents()[Taken.Event].Name : FString(TEXT("?"));
				if (Printed++ < MaxPrintedTransitions)
				{
					Report.Lines.Add(FString::Printf(TEXT("%9.3f  %s -> %s  (%s)"), Time, *StateId(Definition, Taken.Source), *StateId(Definition, Taken.Target), *Event));
				}
				RecentPath.Add(StateId(Definition, Taken.Target));
				if (RecentPath.Num() > 6) RecentPath.RemoveAt(0);
				Window.Add(Time);
				while (Window[0] < Time - 1.0 + UE_KINDA_SMALL_NUMBER) Window.RemoveAt(0);
				if (Window.Num() > Worst) { Worst = Window.Num(); WorstFrom = Window[0]; WorstPath = RecentPath; }
			}
			Recorder.Reset();
		}
	};

	/** Null when the scenario cannot run; Report then holds the error or the compile diagnostics. */
	TSharedPtr<const FADSKDefinition> Prepare(const FString& ScenarioPath, const FString& DisplayPath, const FADSKNodeRegistry& Registry,
		FScenario& Scenario, const TSharedRef<FLeafPlans>& Plans, FString& OutBehaviorName, FADSKSimulationReport& Report)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *ScenarioPath)) { Report.Error = TEXT("cannot read ") + ScenarioPath; return nullptr; }
		if (!ParseScenario(Text, Scenario, Report.Error)) { Report.Error = DisplayPath + TEXT(": ") + Report.Error; return nullptr; }

		const FString BehaviorPath = FPaths::ConvertRelativePathToFull(FPaths::GetPath(ScenarioPath), Scenario.Behavior);
		FString Source;
		if (!FFileHelper::LoadFileToString(Source, *BehaviorPath)) { Report.Error = FString::Printf(TEXT("%s: cannot read behavior %s"), *DisplayPath, *BehaviorPath); return nullptr; }
		TSet<FString> LeafTypes;
		const TSharedPtr<const FADSKNodeRegistry> Doubles = MakeDoubles(Registry, Plans, LeafTypes, Report.Error);
		if (!Doubles.IsValid()) return nullptr;
		OutBehaviorName = FPaths::GetCleanFilename(BehaviorPath);
		const FADSKCompileResult Compiled = FADSKCompiler::Compile(OutBehaviorName, Source, Doubles.ToSharedRef());
		for (const FADSKDiagnostic& Diagnostic : Compiled.Diagnostics)
		{
			Report.Rows.Add(FADSKValidateReport::MakeRow(OutBehaviorName, LexToString(Diagnostic.Severity), LexToString(Diagnostic.Code), Diagnostic.Message,
				Diagnostic.Line, Diagnostic.NodeId.IsNone() ? FString() : Diagnostic.NodeId.ToString()));
		}
		if (!Compiled.IsSuccess()) return nullptr;
		const FADSKDefinition& Definition = *Compiled.Definition;
		if (!ParsePlans(Scenario, Definition, LeafTypes, *Plans, Report.Error)) { Report.Error = DisplayPath + TEXT(": ") + Report.Error; return nullptr; }
		for (const FString& State : Scenario.Reach) if (Definition.FindState(State) == INDEX_NONE) { Report.Error = FString::Printf(TEXT("%s: expect.reach names no state '%s'"), *DisplayPath, *State); return nullptr; }
		for (const FString& State : Scenario.Never) if (Definition.FindState(State) == INDEX_NONE) { Report.Error = FString::Printf(TEXT("%s: expect.never names no state '%s'"), *DisplayPath, *State); return nullptr; }
		return Compiled.Definition;
	}

	/** Applies every timeline change due by Time, from Next on; false with Report.Error set when one cannot apply. */
	bool ApplyDue(const FScenario& Scenario, const FADSKDefinition& Definition, FADSKInstance& Instance, double Time, const FString& DisplayPath,
		int32& Next, FADSKSimulationReport& Report)
	{
		for (; Next < Scenario.Timeline.Num() && Scenario.Timeline[Next].At <= Time + UE_KINDA_SMALL_NUMBER; ++Next)
		{
			const FTimedChange& Change = Scenario.Timeline[Next];
			if (Change.Inputs.IsValid() && !ApplyInputs(*Change.Inputs, Definition, Instance, Report.Error)) { Report.Error = DisplayPath + TEXT(": ") + Report.Error; return false; }
			if (Change.Event.IsEmpty()) continue;
			TArray<FADSKNamedValue> Params;
			if (!MakeEventParams(Change, Definition, Params, Report.Error)) { Report.Error = DisplayPath + TEXT(": ") + Report.Error; return false; }
			const EADSKInputResult Result = Instance.EnqueueEvent(Change.Event, Params);
			if (Result != EADSKInputResult::Accepted)
			{
				Report.Rows.Add(FADSKValidateReport::MakeRow(DisplayPath, TEXT("warning"), TEXT("simulate-input"),
					FString::Printf(TEXT("event %s at %.3f s was refused: %s"), *Change.Event, Time, LexToString(Result))));
			}
		}
		return true;
	}

	/** The summary line, then a row for a livelock, a hold, each missed expectation and the defaulted leaves. */
	void AppendFindings(const FScenario& Scenario, const FRunStats& Stats, const FLeafPlans& Plans, const FString& DisplayPath, const FString& BehaviorName,
		double Time, int64 Steps, FADSKSimulationReport& Report)
	{
		if (Stats.Printed > MaxPrintedTransitions) Report.Lines.Add(FString::Printf(TEXT("      ... %d more transitions; the whole run is in the trace"), Stats.Printed - MaxPrintedTransitions));
		TArray<FString> EnteredSorted = Stats.Entered.Array();
		EnteredSorted.Sort();
		Report.Lines.Add(FString::Printf(TEXT("%s: %s, %.2f s in %lld steps of %.4f s, seed %d: %d transitions, at most %d in one second; entered %s"),
			*DisplayPath, *BehaviorName, Time, Steps, Scenario.Step, Scenario.Seed, Stats.Transitions, Stats.Worst, *FString::Join(EnteredSorted, TEXT(", "))));

		if (Stats.Worst > Scenario.MaxTransitionsPerSecond)
		{
			Report.Rows.Add(FADSKValidateReport::MakeRow(DisplayPath, TEXT("error"), TEXT("simulate-livelock"), FString::Printf(
				TEXT("%d transitions in one second from %.3f s (limit %d), ending ... -> %s: a loop that never waits"),
				Stats.Worst, Stats.WorstFrom, Scenario.MaxTransitionsPerSecond, *FString::Join(Stats.WorstPath, TEXT(" -> ")))));
		}
		if (!Stats.HeldIn.IsEmpty() && !Scenario.bHoldAllowed)
		{
			Report.Rows.Add(FADSKValidateReport::MakeRow(DisplayPath, TEXT("error"), TEXT("simulate-hold"),
				FString::Printf(TEXT("the brain held in %s: a completion no handler takes"), *Stats.HeldIn)));
		}
		for (const FString& State : Scenario.Reach)
		{
			if (!Stats.Entered.Contains(State)) Report.Rows.Add(FADSKValidateReport::MakeRow(DisplayPath, TEXT("error"), TEXT("simulate-expect"), FString::Printf(TEXT("never entered '%s'"), *State)));
		}
		for (const FString& State : Scenario.Never)
		{
			if (Stats.Entered.Contains(State)) Report.Rows.Add(FADSKValidateReport::MakeRow(DisplayPath, TEXT("error"), TEXT("simulate-expect"), FString::Printf(TEXT("entered '%s'"), *State)));
		}
		if (!Plans.Defaulted.IsEmpty())
		{
			TArray<FString> Defaulted = Plans.Defaulted.Array();
			Defaulted.Sort();
			Report.Rows.Add(FADSKValidateReport::MakeRow(DisplayPath, TEXT("note"), TEXT("simulate-default"), FString::Printf(
				TEXT("no leaves entry for %s: each succeeded after %.1f s"), *FString::Join(Defaulted, TEXT(", ")), DefaultAfter)));
		}
	}
}

FADSKSimulationReport FADSKSimulator::Run(const FString& ScenarioPath, const FString& DisplayPath, const TSharedRef<const FADSKNodeRegistry>& Registry)
{
	using namespace ADSKSimulatorPrivate;
	FADSKSimulationReport Report;
	FScenario Scenario;
	FString BehaviorName;
	const TSharedRef<FLeafPlans> Plans = MakeShared<FLeafPlans>();
	const TSharedPtr<const FADSKDefinition> Compiled = Prepare(ScenarioPath, DisplayPath, *Registry, Scenario, Plans, BehaviorName, Report);
	if (!Compiled.IsValid()) return Report;
	const FADSKDefinition& Definition = *Compiled;

	// Read and cleared after every call, so a capacity well above one tick's records never drops any.
	const TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(4096);
	FADSKInstance Instance(Compiled.ToSharedRef(), Recorder);
	Instance.SetRandomSeed(Scenario.Seed);
	if (Scenario.Inputs.IsValid() && !ApplyInputs(*Scenario.Inputs, Definition, Instance, Report.Error)) { Report.Error = DisplayPath + TEXT(": ") + Report.Error; return Report; }
	const FADSKStartReport Started = Instance.Start();
	if (!Started.IsSuccess())
	{
		Report.Rows.Add(FADSKValidateReport::MakeRow(DisplayPath, TEXT("error"), TEXT("simulate-start"),
			FString::Printf(TEXT("%s did not start: %s %s"), *BehaviorName, LexToString(Started.Result), *FString::Join(Started.MissingInputs, TEXT(", ")))));
		return Report;
	}

	FRunStats Stats;
	double Time = 0.0;
	Stats.Drain(*Recorder, Definition, Time, Report);
	int32 Next = 0;
	const int64 Steps = FMath::CeilToInt64(Scenario.Duration / Scenario.Step);
	for (int64 Index = 0; Index < Steps && Instance.IsRunning(); ++Index)
	{
		if (!ApplyDue(Scenario, Definition, Instance, Time, DisplayPath, Next, Report)) return Report;
		Time = (Index + 1) * static_cast<double>(Scenario.Step);
		Instance.Tick(Scenario.Step);
		Stats.Drain(*Recorder, Definition, Time, Report);
		if (Instance.IsHolding() && Stats.HeldIn.IsEmpty())
		{
			const TArray<FString> Path = Instance.GetActiveStatePath();
			Stats.HeldIn = FString::Printf(TEXT("%s at %.3f s"), Path.IsEmpty() ? TEXT("?") : *Path.Last(), Time);
		}
	}
	AppendFindings(Scenario, Stats, *Plans, DisplayPath, BehaviorName, Time, Steps, Report);
	return Report;
}
