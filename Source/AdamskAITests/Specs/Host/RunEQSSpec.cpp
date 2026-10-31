// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Components/ActorTestSpawner.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "Nodes/ADSKHostNodes.h"
#include "Compiler/ADSKCompiler.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "CQTest.h"
#include "Support/TestChecks.h"
#include "Support/FakeHostServices.h"

namespace FRunEQSSpecPrivate
{
	FEnvQueryResult MakeSuccess(const FVector& Point)
	{
		FEnvQueryResult Result(EEnvQueryStatus::Success);
		Result.ItemType = UEnvQueryItemType_Point::StaticClass();
		Result.RawData.SetNumZeroed(sizeof(FNavLocation));
		UEnvQueryItemType_Point::SetValue(Result.RawData.GetData(), FNavLocation(Point));
		Result.Items.Add(FEnvQueryItem(0));
		return Result;
	}

	struct FFixture
	{
		FActorTestSpawner Spawner;
		TStrongObjectPtr<UEnvQuery> Query;
		UADSKBrainComponent* Brain = nullptr;
		AActor* Actor = nullptr;
		TUniquePtr<FADSKScopedFakeHostServices> Services;
		TFunction<void(const FEnvQueryResult&)> Finish;
		int32 NextId = 42;
		int32 Executes = 0;
		int32 LastAbort = INDEX_NONE;
		int32 Aborts = 0;
		EEnvQueryRunMode::Type LastMode = EEnvQueryRunMode::AllMatching;
		TArray<FADSKNamedParam> Params;
		bool bFinishInExecute = false;

		/** Extra is spliced into the RunEQS element: attributes before `>`, children after. */
		FFixture(bool bWithActor = true, bool bLoadedQuery = true, const TCHAR* Path = nullptr, const TCHAR* ExtraAttributes = TEXT(""), const TCHAR* ExtraChildren = TEXT(""))
		{
			UPackage* Package = CreatePackage(TEXT("/Temp/ADSKRunEQS"));
			Query.Reset(NewObject<UEnvQuery>(Package, TEXT("EQS_Test"), RF_Transient));
			Services = MakeUnique<FADSKScopedFakeHostServices>(Spawner.GetWorld());
			(*Services)->Run = [this](AActor&, const UEnvQuery&, EEnvQueryRunMode::Type Mode, TConstArrayView<FADSKNamedParam> InParams, TFunction<void(const FEnvQueryResult&)> OnFinished)
			{
				++Executes;
				LastMode = Mode;
				Params = TArray<FADSKNamedParam>(InParams);
				Finish = OnFinished;
				if (bFinishInExecute) { OnFinished(MakeSuccess(FVector(1, 2, 3))); }
				return NextId;
			};
			(*Services)->Abort = [this](UWorld*, int32 Id) { ++Aborts; LastAbort = Id; };
			TArray<FString> Errors;
			const TSharedRef<const FADSKNodeRegistry> Registry = ADSKTest::PluginCatalogue();
			const FString QueryPath = bLoadedQuery ? Query->GetPathName() : FString(TEXT("/Temp/ADSKRunEQS.EQS_Unloaded"));
			const FString Xml = FString::Printf(TEXT("<Agent version=\"1\" id=\"a\"><Inputs/><Locals><Local name=\"P\" type=\"Vector\"/></Locals><Events/><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
				TEXT("<Tree id=\"t\"><Sequence id=\"q\"><RunEQS id=\"e\" query=\"%s\" out-point=\"P\" %s>%s</RunEQS><Wait id=\"w\" duration=\"100\"/></Sequence></Tree></Agent>"),
				Path ? Path : *QueryPath, ExtraAttributes, ExtraChildren);
			const TSharedRef<const FADSKDefinition> Definition = ADSKTest::LoadOrStandIn(TEXT("eqs.xml"), Xml, Registry);
			if (bWithActor)
			{
				Actor = &Spawner.SpawnActor<AActor>();
				Brain = NewObject<UADSKBrainComponent>(Actor);
				Actor->AddInstanceComponent(Brain);
				Brain->RegisterComponent();
			}
			else
			{
				Brain = NewObject<UADSKBrainComponent>();
				Brain->AddToRoot();
			}
			Brain->InitializeDefinition(Definition, FADSKRecorder::Create(256));
		}

		~FFixture()
		{
			Brain->Cleanup();
			if (!Actor) { Brain->RemoveFromRoot(); }
		}

		int32 Pending() const { return Brain->GetAsyncOps().GetOps().Num(); }
		FVector Point() const { FADSKValue Value; Brain->GetInstance()->GetValue(TEXT("P"), Value); return Value.Vector; }
	};
}

using namespace FRunEQSSpecPrivate;

TEST_CLASS_WITH_FLAGS(FRunEQSSpec, "AdamskAI.Host.RunEQS",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(MapResultMapsPointAndRejectsEmptyOrFailed)
	{
		TOptional<FADSKValue> Payload;
		ASSERT_THAT(AreEqual(EADSKNodeStatus::Succeeded, ADSKRunEQS::MapResult(MakeSuccess(FVector(4, 5, 6)), Payload)));
		ASSERT_THAT(IsTrue(Payload.IsSet(), TEXT("no payload")));
		ASSERT_THAT(IsTrue(Payload->Vector.Equals(FVector(4, 5, 6)), Payload->Vector.ToString()));
		ASSERT_THAT(AreEqual(EADSKNodeStatus::Failed, ADSKRunEQS::MapResult(FEnvQueryResult(EEnvQueryStatus::Success), Payload)));
		ASSERT_THAT(IsFalse(Payload.IsSet(), TEXT("a payload for an empty result")));
		ASSERT_THAT(AreEqual(EADSKNodeStatus::Failed, ADSKRunEQS::MapResult(FEnvQueryResult(EEnvQueryStatus::Failed), Payload)));
		ASSERT_THAT(AreEqual(EADSKNodeStatus::Failed, ADSKRunEQS::MapResult(FEnvQueryResult(EEnvQueryStatus::Aborted), Payload)));
	}

	TEST_METHOD(SuccessWritesThePointForTheNextTick)
	{
		FFixture F;
		F.Brain->StartLogic();
		ASSERT_THAT(AreEqual(1, F.Executes));
		ASSERT_THAT(AreEqual(1, F.Pending()));
		F.Finish(MakeSuccess(FVector(7, 8, 9)));
		F.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(F.Point().Equals(FVector(7, 8, 9)), F.Point().ToString()));
		ASSERT_THAT(AreEqual(0, F.Pending()));
		ASSERT_THAT(AreEqual(0, F.Aborts));
	}


	TEST_METHOD(EmptyResultFailsWithoutPayload)
	{
		FFixture F;
		F.Brain->StartLogic();
		F.Finish(FEnvQueryResult(EEnvQueryStatus::Success));
		F.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(F.Point().IsZero(), F.Point().ToString()));
		ASSERT_THAT(AreEqual(0, F.Pending()));
		ASSERT_THAT(AreEqual(0, F.Aborts));
	}

	TEST_METHOD(StartFailureRemovesThePartialEntry)
	{
		FFixture F;
		F.NextId = INDEX_NONE;
		F.Brain->StartLogic();
		F.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(1, F.Executes));
		ASSERT_THAT(AreEqual(0, F.Pending()));
		ASSERT_THAT(AreEqual(0, F.Aborts));
	}

	TEST_METHOD(CancellationAbortsTheSavedId)
	{
		FFixture F;
		F.Brain->StartLogic();
		F.Brain->StopLogic();
		ASSERT_THAT(AreEqual(1, F.Aborts));
		ASSERT_THAT(AreEqual(42, F.LastAbort));
		ASSERT_THAT(AreEqual(0, F.Pending()));
	}

	TEST_METHOD(LateCallbackAfterStopIsStale)
	{
		FFixture F;
		F.Brain->StartLogic();
		F.Brain->StopLogic();
		F.Finish(MakeSuccess(FVector(1, 1, 1)));
		ASSERT_THAT(AreEqual<uint32>(1, F.Brain->GetStaleAsyncCompletions()));
		ASSERT_THAT(AreEqual(0, F.Pending()));
	}

	TEST_METHOD(CompletionBeforeSetCancelInstallsNoAbort)
	{
		FFixture F;
		F.bFinishInExecute = true;
		F.Brain->StartLogic();
		F.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(F.Point().Equals(FVector(1, 2, 3)), F.Point().ToString()));
		F.Brain->StopLogic();
		ASSERT_THAT(AreEqual(0, F.Aborts));
		ASSERT_THAT(AreEqual(0, F.Pending()));
	}

	TEST_METHOD(NullHostOrUnresolvedQueryFailsWithoutExecuting)
	{
		{
			FFixture F(/*bWithActor=*/false);
			F.Brain->StartLogic();
			F.Brain->TickBrain(0.1f);
			ASSERT_THAT(AreEqual(0, F.Executes));
			ASSERT_THAT(AreEqual(0, F.Pending()));
		}
		FFixture G(/*bWithActor=*/true, /*bLoadedQuery=*/false);
		G.Brain->StartLogic();
		G.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(0, G.Executes));
		ASSERT_THAT(AreEqual(0, G.Pending()));
	}

	TEST_METHOD(DefaultRunsTheBestItemWithoutParams)
	{
		FFixture F;
		F.Brain->StartLogic();
		ASSERT_THAT(IsTrue(F.LastMode == EEnvQueryRunMode::SingleResult, TEXT("the query did not run in SingleResult mode")));
		ASSERT_THAT(AreEqual(0, F.Params.Num()));
	}

	TEST_METHOD(PickAndParamsReachTheRequest)
	{
		FFixture F(true, true, nullptr, TEXT("pick=\"RandomBest5\""), TEXT("<Param name=\"Min Distance\" value=\"150\"/><Param name=\"Max Distance\" value=\"-2.5\"/>"));
		F.Brain->StartLogic();
		ASSERT_THAT(IsTrue(F.LastMode == EEnvQueryRunMode::RandomBest5Pct, TEXT("the query did not run in RandomBest5Pct mode")));
		ASSERT_THAT(AreEqual(2, F.Params.Num()));
		ASSERT_THAT(AreEqual(FName(TEXT("Min Distance")), F.Params[0].Name));
		ASSERT_THAT(IsTrue(F.Params[0].Value == 150.f, FString::Printf(TEXT("Min Distance is %f"), F.Params[0].Value)));
		ASSERT_THAT(AreEqual(FName(TEXT("Max Distance")), F.Params[1].Name));
		ASSERT_THAT(IsTrue(F.Params[1].Value == -2.5f, FString::Printf(TEXT("Max Distance is %f"), F.Params[1].Value)));
	}

	TEST_METHOD(IntAndBoolParamsKeepTheirType)
	{
		FFixture F(true, true, nullptr, TEXT(""), TEXT("<Param name=\"Count\" value=\"7\" type=\"Int\"/><Param name=\"Strict\" value=\"true\" type=\"Bool\"/><Param name=\"Range\" value=\"2\"/>"));
		F.Brain->StartLogic();
		ASSERT_THAT(AreEqual(3, F.Params.Num()));
		ASSERT_THAT(AreEqual(EADSKParamType::Int, F.Params[0].Type));
		ASSERT_THAT(AreEqual(7, F.Params[0].IntValue));
		ASSERT_THAT(AreEqual(EADSKParamType::Bool, F.Params[1].Type));
		ASSERT_THAT(IsTrue(F.Params[1].bBoolValue, TEXT("the Bool param is false")));
		ASSERT_THAT(AreEqual(EADSKParamType::Float, F.Params[2].Type));
		ASSERT_THAT(IsTrue(F.Params[2].Value == 2.f, FString::Printf(TEXT("the Float param is %f"), F.Params[2].Value)));
	}

	TEST_METHOD(LegacyRandomBest25StillPicksFromTheBestQuarter)
	{
		FFixture F(true, true, nullptr, TEXT("randomBest25=\"true\""));
		F.Brain->StartLogic();
		ASSERT_THAT(IsTrue(F.LastMode == EEnvQueryRunMode::RandomBest25Pct, TEXT("the query did not run in RandomBest25Pct mode")));
	}

	TEST_METHOD(UndeclaredParamsAreNamed)
	{
		FFixture F;
		const FADSKNamedParam Params[] = {{TEXT("Range"), 1.f}, {TEXT("Other"), 2.f}};
		// An empty query declares nothing, so every authored param is reported, in authored order.
		ASSERT_THAT(AreEqual(FString(TEXT("'Range', 'Other'")), ADSKRunEQS::FindUnknownParams(*F.Query, Params)));
		ASSERT_THAT(IsTrue(ADSKRunEQS::FindUnknownParams(*F.Query, {}).IsEmpty(), ADSKRunEQS::FindUnknownParams(*F.Query, {})));
	}

	TEST_METHOD(ConfigDiagnosticsAreExact)
	{
		TArray<FString> Errors;
		const TSharedPtr<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::BuildPluginCatalogue(Errors);
		const FString Format(TEXT("<Agent version=\"1\" id=\"a\"><Inputs/><Locals><Local name=\"P\" type=\"Vector\"/></Locals><Events/><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><RunEQS id=\"e\" @ATTR@ out-point=\"P\"/></Tree></Agent>"));
		const FADSKCompileResult Missing = FADSKCompiler::Compile(TEXT("eqs.xml"), Format.Replace(TEXT("@ATTR@"), TEXT("")), Registry.ToSharedRef());
		ASSERT_THAT(AreEqual(1, Missing.Count(EADSKDiagnosticCode::OutOfRange)));
		ASSERT_THAT(IsFalse(Missing.IsSuccess(), ADSKTest::FormatDiagnostics(Missing)));
		const FADSKCompileResult Malformed = FADSKCompiler::Compile(TEXT("eqs.xml"), Format.Replace(TEXT("@ATTR@"), TEXT("query=\"nope\"")), Registry.ToSharedRef());
		ASSERT_THAT(AreEqual(1, Malformed.Count(EADSKDiagnosticCode::TypeMismatch)));
		ASSERT_THAT(IsFalse(Malformed.IsSuccess(), ADSKTest::FormatDiagnostics(Malformed)));
		const FString Query(TEXT("query=\"/Game/Q.Q\" "));
		const FADSKCompileResult UnknownPick = FADSKCompiler::Compile(TEXT("eqs.xml"), Format.Replace(TEXT("@ATTR@"), *(Query + TEXT("pick=\"Worst\""))), Registry.ToSharedRef());
		ASSERT_THAT(AreEqual(1, UnknownPick.Count(EADSKDiagnosticCode::UnknownEnumValue)));
		const FADSKCompileResult BothModes = FADSKCompiler::Compile(TEXT("eqs.xml"), Format.Replace(TEXT("@ATTR@"), *(Query + TEXT("pick=\"RandomBest5\" randomBest25=\"true\""))), Registry.ToSharedRef());
		ASSERT_THAT(AreEqual(1, BothModes.Count(EADSKDiagnosticCode::OutOfRange)));
	}

	TEST_METHOD(ParamDiagnosticsAreExact)
	{
		TArray<FString> Errors;
		const TSharedPtr<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::BuildPluginCatalogue(Errors);
		const FString Format(TEXT("<Agent version=\"1\" id=\"a\"><Inputs/><Locals><Local name=\"P\" type=\"Vector\"/></Locals><Events/><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><RunEQS id=\"e\" query=\"/Game/Q.Q\" out-point=\"P\">@PARAMS@</RunEQS></Tree></Agent>"));
		const auto Load = [&](const TCHAR* Params) { return FADSKCompiler::Compile(TEXT("eqs.xml"), Format.Replace(TEXT("@PARAMS@"), Params), Registry.ToSharedRef()); };
		const FADSKCompileResult TwoParams = Load(TEXT("<Param name=\"A\" value=\"1\"/><Param name=\"B\" value=\"2\"/>"));
		ASSERT_THAT(IsTrue(TwoParams.IsSuccess(), ADSKTest::FormatDiagnostics(TwoParams)));
		ASSERT_THAT(AreEqual(1, Load(TEXT("<Param name=\"A\" value=\"1\"/><Param name=\"a\" value=\"2\"/>")).Count(EADSKDiagnosticCode::DuplicateId)));
		ASSERT_THAT(AreEqual(1, Load(TEXT("<Param name=\"A\" value=\"far\"/>")).Count(EADSKDiagnosticCode::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Load(TEXT("<Param name=\"A\"/>")).Count(EADSKDiagnosticCode::MissingRequired)));
		const FADSKCompileResult TypedParams = Load(TEXT("<Param name=\"A\" value=\"1\" type=\"Float\"/><Param name=\"B\" value=\"-3\" type=\"Int\"/><Param name=\"C\" value=\"true\" type=\"Bool\"/>"));
		ASSERT_THAT(IsTrue(TypedParams.IsSuccess(), ADSKTest::FormatDiagnostics(TypedParams)));
		ASSERT_THAT(AreEqual(1, Load(TEXT("<Param name=\"A\" value=\"1\" type=\"Name\"/>")).Count(EADSKDiagnosticCode::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Load(TEXT("<Param name=\"A\" value=\"1\" type=\"int\"/>")).Count(EADSKDiagnosticCode::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Load(TEXT("<Param name=\"A\" value=\"1.5\" type=\"Int\"/>")).Count(EADSKDiagnosticCode::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Load(TEXT("<Param name=\"A\" value=\"1\" type=\"Bool\"/>")).Count(EADSKDiagnosticCode::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Load(TEXT("<Param name=\"A\" value=\"1\" kind=\"Float\"/>")).Count(EADSKDiagnosticCode::UnknownAttribute)));
		// Params are not tree children, so a leaf still takes none.
		ASSERT_THAT(AreEqual(1, Load(TEXT("<Wait id=\"w\"/>")).Count(EADSKDiagnosticCode::BadArity)));
	}
};

#endif
