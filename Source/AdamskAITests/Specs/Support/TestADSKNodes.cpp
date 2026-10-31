// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Support/TestADSKNodes.h"

#include "Nodes/ADSKBuiltinNodes.h"
#include "Support/TestChecks.h"

namespace FTestADSKNodesPrivate
{
	using FLogRef = TSharedRef<FADSKTestLeafLog>;

	const TCHAR* EndName(EADSKEndReason Reason)
	{
		switch (Reason)
		{
		case EADSKEndReason::Succeeded:
			return TEXT("Succeeded");
		case EADSKEndReason::Failed:
			return TEXT("Failed");
		case EADSKEndReason::Cancelled:
			return TEXT("Cancelled");
		case EADSKEndReason::PartialEntry:
			return TEXT("PartialEntry");
		}
		return TEXT("?");
	}

	FADSKNodeDescriptor MakeLeaf(const TCHAR* Name, const FLogRef& Log, EADSKNodeStatus DefaultStart)
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = Name;
		const FString TypeName = Name;
		Descriptor.OnStart = [Log, TypeName, DefaultStart](FADSKNodeContext& Context)
		{
			++Log->Starts;
			Log->Handles.Add(Context.GetHandle());
			Log->Lines.Add(FString::Printf(TEXT("Start:%s:%s"), *TypeName, *Context.GetNodeId().ToString()));
			const EADSKNodeStatus* Scripted = Log->ScriptedStarts.Find(Context.GetNodeId());
			return Scripted ? *Scripted : DefaultStart;
		};
		Descriptor.OnUpdate = [Log, TypeName](FADSKNodeContext& Context, float Delta)
		{
			++Log->Updates;
			Log->Lines.Add(FString::Printf(TEXT("Update:%s:%s:%.3f"), *TypeName, *Context.GetNodeId().ToString(), Delta));
			const EADSKNodeStatus* Scripted = Log->ScriptedUpdates.Find(Context.GetNodeId());
			return Scripted ? *Scripted : EADSKNodeStatus::Running;
		};
		Descriptor.OnEnd = [Log, TypeName](FADSKNodeContext& Context, EADSKEndReason Reason)
		{
			++Log->Ends;
			Log->Lines.Add(FString::Printf(TEXT("End:%s:%s:%s"), *TypeName, *Context.GetNodeId().ToString(), EndName(Reason)));
		};
		return Descriptor;
	}

	FADSKNodeDescriptor MakeMoveTo(const FLogRef& Log)
	{
		FADSKNodeDescriptor Descriptor = MakeLeaf(TEXT("MoveTo"), Log, EADSKNodeStatus::Running);
		Descriptor.ConfigStruct = FADSKTestMoveConfig::StaticStruct();
		Descriptor.Fields.Add({TEXT("acceptanceRadius"), TEXT("AcceptanceRadius")});
		Descriptor.Ports.Add({TEXT("goal"), EADSKValueType::Vector, EADSKPortDirection::In});
		Descriptor.ValidateConfig = [](const void* Config)
		{
			return static_cast<const FADSKTestMoveConfig*>(Config)->AcceptanceRadius >= 0.f;
		};
		const FADSKStartFn Base = Descriptor.OnStart;
		Descriptor.OnStart = [Log, Base](FADSKNodeContext& Context)
		{
			FVector Goal = FVector::ZeroVector;
			Context.ReadVector(0, Goal);
			Log->Lines.Add(FString::Printf(TEXT("Goal:%s:%s"), *Context.GetNodeId().ToString(), *Goal.ToString()));
			return Base(Context);
		};
		return Descriptor;
	}

	FADSKNodeDescriptor MakeLookAtPoint(const FLogRef& Log)
	{
		FADSKNodeDescriptor Descriptor = MakeLeaf(TEXT("LookAtPoint"), Log, EADSKNodeStatus::Running);
		Descriptor.Ports.Add({TEXT("target"), EADSKValueType::Vector, EADSKPortDirection::In});
		return Descriptor;
	}

	FADSKNodeDescriptor MakeAcquireResource(const FLogRef& Log)
	{
		FADSKNodeDescriptor Descriptor = MakeLeaf(TEXT("AcquireResource"), Log, EADSKNodeStatus::Failed);
		const FADSKStartFn Base = Descriptor.OnStart;
		Descriptor.OnStart = [Log, Base](FADSKNodeContext& Context)
		{
			if (Log->PartialEntryIds.Contains(Context.GetNodeId()))
			{
				Context.MarkPartiallyEntered();
			}
			return Base(Context);
		};
		return Descriptor;
	}

	FADSKNodeDescriptor MakeSense(const FLogRef& Log)
	{
		FADSKNodeDescriptor Descriptor = MakeLeaf(TEXT("Sense"), Log, EADSKNodeStatus::Succeeded);
		Descriptor.Ports.Add({TEXT("point"), EADSKValueType::Vector, EADSKPortDirection::Out});
		const FADSKStartFn Base = Descriptor.OnStart;
		Descriptor.OnStart = [Base](FADSKNodeContext& Context)
		{
			Context.WritePort(0, FADSKValue::MakeVector(FVector(1.0, 2.0, 3.0)));
			return Base(Context);
		};
		return Descriptor;
	}

	FADSKNodeDescriptor MakeIntProbe(const FLogRef& Log)
	{
		FADSKNodeDescriptor Descriptor = MakeLeaf(TEXT("IntProbe"), Log, EADSKNodeStatus::Succeeded);
		Descriptor.ConfigStruct = FADSKTestIntConfig::StaticStruct();
		Descriptor.Fields.Add({TEXT("amount"), TEXT("Amount")});
		Descriptor.Ports.Add({TEXT("value"), EADSKValueType::Int, EADSKPortDirection::In});
		Descriptor.Ports.Add({TEXT("result"), EADSKValueType::Int, EADSKPortDirection::Out});
		const FADSKStartFn Base = Descriptor.OnStart;
		Descriptor.OnStart = [Log, Base](FADSKNodeContext& Context)
		{
			int32 Value = 0;
			if (!Context.ReadInt(0, Value) || !Context.WriteInt(1, Value + Context.GetConfig<FADSKTestIntConfig>().Amount))
			{
				return EADSKNodeStatus::Failed;
			}
			return Base(Context);
		};
		return Descriptor;
	}

	FADSKNodeDescriptor MakeScoped(const FLogRef& Log)
	{
		FADSKNodeDescriptor Descriptor = MakeLeaf(TEXT("Scoped"), Log, EADSKNodeStatus::Running);
		Descriptor.Kind = EADSKNodeKind::Scope;
		Descriptor.MinChildren = Descriptor.MaxChildren = 1;
		return Descriptor;
	}

	/** Logs `Choice:<id>:<mood>:<name>=<value>,...` and succeeds. */
	FADSKNodeDescriptor MakeChoosy(const FLogRef& Log)
	{
		FADSKNodeDescriptor Descriptor = MakeLeaf(TEXT("Choosy"), Log, EADSKNodeStatus::Succeeded);
		Descriptor.ConfigStruct = FADSKTestChoiceConfig::StaticStruct();
		Descriptor.Fields.Add({TEXT("mood"), TEXT("Mood")});
		Descriptor.ParamsProperty = GET_MEMBER_NAME_CHECKED(FADSKTestChoiceConfig, Params);
		const FADSKStartFn Base = Descriptor.OnStart;
		Descriptor.OnStart = [Log, Base](FADSKNodeContext& Context)
		{
			const FADSKTestChoiceConfig& Config = Context.GetConfig<FADSKTestChoiceConfig>();
			const FString Params = FString::JoinBy(Config.Params, TEXT(","), [](const FADSKNamedParam& P)
			{
				return P.Type == EADSKParamType::Int ? FString::Printf(TEXT("%s=%d"), *P.Name.ToString(), P.IntValue)
					: P.Type == EADSKParamType::Bool ? FString::Printf(TEXT("%s=%s"), *P.Name.ToString(), P.bBoolValue ? TEXT("true") : TEXT("false"))
					: FString::Printf(TEXT("%s=%g"), *P.Name.ToString(), P.Value);
			});
			Log->Lines.Add(FString::Printf(TEXT("Choice:%s:%s:%s"), *Context.GetNodeId().ToString(), *StaticEnum<EADSKTestMood>()->GetNameStringByValue(static_cast<int64>(Config.Mood)), *Params));
			return Base(Context);
		};
		return Descriptor;
	}

}

using namespace FTestADSKNodesPrivate;

void FADSKTestNodes::RegisterTestNodes(FADSKNodeRegistry& Registry, const TSharedRef<FADSKTestLeafLog>& Log, bool bReverseOrder)
{
	TArray<FADSKNodeDescriptor> Descriptors;
	Descriptors.Add(MakeMoveTo(Log));
	Descriptors.Add(MakeLookAtPoint(Log));
	Descriptors.Add(MakeAcquireResource(Log));
	Descriptors.Add(MakeSense(Log));
	Descriptors.Add(MakeIntProbe(Log));
	Descriptors.Add(MakeScoped(Log));
	Descriptors.Add(MakeChoosy(Log));
	if (bReverseOrder)
	{
		Algo::Reverse(Descriptors);
	}
	for (FADSKNodeDescriptor& Descriptor : Descriptors)
	{
		ADSKTest::Register(Registry, MoveTemp(Descriptor));
	}
}

TSharedRef<const FADSKNodeRegistry> FADSKTestNodes::MakeFrozenRegistry(const TSharedRef<FADSKTestLeafLog>& Log, bool bReverseOrder)
{
	const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
	if (bReverseOrder)
	{
		RegisterTestNodes(*Registry, Log, true);
		FADSKBuiltinNodes::RegisterAll(*Registry);
	}
	else
	{
		FADSKBuiltinNodes::RegisterAll(*Registry);
		RegisterTestNodes(*Registry, Log, false);
	}
	Registry->Freeze();
	return Registry;
}
