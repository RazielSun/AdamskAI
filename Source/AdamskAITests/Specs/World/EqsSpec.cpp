// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "World/WorldQueries.h"
#include "World/WorldScenario.h"
#include "CQTest.h"

// RunEQS on the real EQS manager, with no fake host services: EQ_AdskTest_Rings (or the same query built in
// memory) runs around the agent on the open floor, and each param changes what it returns.
namespace FEqsSpecPrivate
{
	const FVector SpawnPoint(0.0, -1500.0, ADSKWorldLayout::StandZ);
	const FTimespan Timeout = FTimespan::FromSeconds(20);
	const TCHAR* MemoryPackage = TEXT("/Temp/AdamskAIWorld/EQ_Memory");

	/** A one-RunEQS agent over Query with the given `<Param>` children: found, failed, or left on the Leave event. */
	FString QueryXml(const FString& Query, const FString& Params)
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"world_eqs\"><Locals><Local name=\"Point\" type=\"Vector\"/></Locals>"
			"<Events><Event name=\"Leave\"/></Events><StateMachine id=\"m\" initial=\"querying\"><State id=\"querying\" tree=\"query\">"
			"<On event=\"tree.success\" goto=\"found\"/><On event=\"tree.failure\" goto=\"failed\"/><On event=\"Leave\" goto=\"left\"/></State>"
			"<State id=\"found\"/><State id=\"failed\"/><State id=\"left\"/></StateMachine>"
			"<Tree id=\"query\"><RunEQS id=\"query_run\" query=\"%s\" out-point=\"Point\">%s</RunEQS></Tree></Agent>"), *Query, *Params);
	}

	FString Params(float Radius, int32 Rings, bool bReachable)
	{
		return FString::Printf(TEXT("<Param name=\"Radius\" value=\"%.0f\"/><Param name=\"Rings\" value=\"%d\" type=\"Int\"/>"
			"<Param name=\"Reachable\" value=\"%s\" type=\"Bool\"/>"), Radius, Rings, bReachable ? TEXT("true") : TEXT("false"));
	}

	bool HasFinished(const FADSKWorldAgent& Agent)
	{
		return !Agent.HasInstance() || Agent.IsIn(TEXT("m/found")) || Agent.IsIn(TEXT("m/failed"));
	}
}

using namespace FEqsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FWorldEqsSpec, "AdamskAIWorld.Eqs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKWorldScenario Scenario;
	FADSKWorldAgent Agent;
	TStrongObjectPtr<UEnvQuery> MemoryQuery;
	double Mark = 0.0;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	AFTER_EACH()
	{
		if (MemoryQuery)
		{
			MemoryQuery->ClearFlags(RF_Public | RF_Standalone);
			MemoryQuery.Reset();
		}
	}

	/** Runs the query from SpawnPoint and waits for its outcome; MakeQuery runs once PIE is up. */
	void Run(TFunction<FString()> MakeQuery, const FString& QueryParams)
	{
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("spawn the agent"), [this, MakeQuery, QueryParams]
			{
				const FString Source = Scenario.WriteBehavior(TEXT("eqs.xml"), QueryXml(MakeQuery(), QueryParams));
				Agent = Scenario.SpawnAgent(SpawnPoint, Source);
			})
			.Until(TEXT("the query ends"), [this] { return HasFinished(Agent); }, Timeout);
	}

	FString AssetQuery() const { return ADSKWorldQueries::AssetPath(ADSKWorldQueries::RingsName); }

	/** The best point is the farthest one: on the outer ring, Radius from the agent. */
	TEST_METHOD(TheAssetQueryWritesTheFarthestPointIntoTheSlot)
	{
		if (!Scenario.IsOpen()) return;
		Run([this] { return AssetQuery(); }, Params(600.f, 2, true));
		TestCommandBuilder.Then(TEXT("found"), [this]
		{
			ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("found")}));
			const double Distance = FVector::Dist2D(Agent.Value(TEXT("Point")).Vector, SpawnPoint);
			ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Distance, 600.0, 60.0), FString::Printf(TEXT("the point is %.0f cm away, expected about 600"), Distance)));
			ASSERT_THAT(AreEqual(0u, Agent.Brain->GetStaleAsyncCompletions()));
		});
	}

	/** Int: a single ring lies at the inner radius, so the farthest point is 100 cm away whatever Radius says. */
	TEST_METHOD(OneRingPutsEveryPointAtTheInnerRadius)
	{
		if (!Scenario.IsOpen()) return;
		Run([this] { return AssetQuery(); }, Params(600.f, 1, true));
		TestCommandBuilder.Then(TEXT("found near"), [this]
		{
			ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("found")}));
			const double Distance = FVector::Dist2D(Agent.Value(TEXT("Point")).Vector, SpawnPoint);
			ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Distance, double(ADSKWorldQueries::InnerRadius), 30.0), FString::Printf(TEXT("the point is %.0f cm away, expected about 100"), Distance)));
		});
	}

	/** Bool: on open floor every point is reachable, so keeping only unreachable ones leaves nothing: Failed. */
	TEST_METHOD(AnEmptyResultFails)
	{
		if (!Scenario.IsOpen()) return;
		Run([this] { return AssetQuery(); }, Params(600.f, 2, false));
		TestCommandBuilder.Then(TEXT("failed"), [this]
		{
			ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("failed")}));
			ASSERT_THAT(IsTrue(Agent.Value(TEXT("Point")).Vector.IsZero(), TEXT("a failed query wrote the slot")));
		});
	}

	/** The same query built in memory: RunEQS and the preload resolve an in-memory object path like an asset's. */
	TEST_METHOD(AQueryBuiltInMemoryRunsLikeTheAsset)
	{
		if (!Scenario.IsOpen()) return;
		Run([this]
		{
			UPackage* Package = CreatePackage(MemoryPackage);
			MemoryQuery.Reset(NewObject<UEnvQuery>(Package, TEXT("EQ_Memory"), RF_Public | RF_Standalone | RF_Transient));
			ADSKWorldQueries::Fill(*MemoryQuery);
			return FString(MemoryPackage) + TEXT(".EQ_Memory");
		}, Params(400.f, 2, true));
		TestCommandBuilder.Then(TEXT("found"), [this]
		{
			ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("found")}));
			const double Distance = FVector::Dist2D(Agent.Value(TEXT("Point")).Vector, SpawnPoint);
			ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Distance, 400.0, 60.0), FString::Printf(TEXT("the point is %.0f cm away, expected about 400"), Distance)));
		});
	}

	/**
	 * Leave is queued in the frame the query starts; the brain ticks before the EQS manager does, so the state is left
	 * and the query aborted before it ever runs. No late result may write the slot afterwards.
	 */
	TEST_METHOD(LeavingTheStateAbortsTheQuery)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("start and leave at once"), [this]
			{
				Agent = Scenario.SpawnAgent(SpawnPoint, Scenario.WriteBehavior(TEXT("eqs.xml"), QueryXml(AssetQuery(), Params(600.f, 2, true))));
				ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Agent.Brain->EnqueueEvent(TEXT("Leave"))));
				Mark = Scenario.Now();
			})
			.Until(TEXT("half a second"), [this] { return Scenario.Now() - Mark >= 0.5; }, Timeout)
			.Then(TEXT("left, slot untouched"), [this]
			{
				ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("left")}));
				ASSERT_THAT(IsTrue(Agent.Value(TEXT("Point")).Vector.IsZero(), TEXT("an aborted query wrote the slot")));
				ASSERT_THAT(AreEqual(0, ADSKTest::CountRecords(Agent.Brain->GetRecorder().Get(), EADSKRecordKind::ResultAccepted)));
				// The EQS manager may still call back once for the aborted query; the brain must drop it as stale.
				ASSERT_THAT(IsTrue(Agent.Brain->GetStaleAsyncCompletions() <= 1u, FString::Printf(TEXT("%u stale completions"), Agent.Brain->GetStaleAsyncCompletions())));
			});
	}
};

#endif
