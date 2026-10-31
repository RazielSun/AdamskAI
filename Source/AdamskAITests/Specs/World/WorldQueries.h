// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "DataProviders/AIDataProvider_QueryParams.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_Donut.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Pathfinding.h"

/**
 * The EQS query of the AdamskAIWorld.* specs, built in code: the generator saves it as the asset EQ_AdskTest_Rings
 * (with the redirector EQ_AdskTest_Old pointing at it), and specs may build the same query in memory.
 */
namespace ADSKWorldQueries
{
	inline const TCHAR* Directory = TEXT("/AdamskAI/Tests/EQS");
	inline const TCHAR* RingsName = TEXT("EQ_AdskTest_Rings");
	inline const TCHAR* OldName = TEXT("EQ_AdskTest_Old");
	/** Float: the outer ring's radius, in cm. */
	inline const FName RadiusParam(TEXT("Radius"));
	/** Int: how many rings between 100 cm and Radius; 1 puts every point at 100 cm. */
	inline const FName RingsParam(TEXT("Rings"));
	/** Bool: keep the points a path from the querier reaches (true) or the ones none reaches (false). */
	inline const FName ReachableParam(TEXT("Reachable"));
	inline constexpr float InnerRadius = 100.f;

	inline FString AssetPath(const TCHAR* Name) { return FString::Printf(TEXT("%s/%s.%s"), Directory, Name, Name); }

	/** Binds Value to the query param Param, as the EQS editor does; Field is the provider's property of that type. */
	template <typename ValueType>
	void Bind(UObject& Node, ValueType& Value, FName Param, FName Field)
	{
		UAIDataProvider_QueryParams* Provider = NewObject<UAIDataProvider_QueryParams>(&Node);
		Provider->ParamName = Param;
		Value.DataBinding = Provider;
		Value.DataField = Field;
	}

	/**
	 * Replaces Query's options with one: 8 points per ring on navmesh-projected rings around the querier, from 100 cm
	 * to Radius in Rings rings, filtered by whether a path reaches them (== Reachable), scored by distance, farthest
	 * first. So the best point lies Radius away, or 100 cm with Rings = 1, and on open floor Reachable = false leaves
	 * nothing.
	 */
	inline void Fill(UEnvQuery& Query)
	{
		UEnvQueryGenerator_Donut* Rings = NewObject<UEnvQueryGenerator_Donut>(&Query);
		Rings->InnerRadius.DefaultValue = InnerRadius;
		Rings->OuterRadius.DefaultValue = 500.f;
		Bind(*Rings, Rings->OuterRadius, RadiusParam, TEXT("FloatValue"));
		Rings->NumberOfRings.DefaultValue = 3;
		Bind(*Rings, Rings->NumberOfRings, RingsParam, TEXT("IntValue"));
		Rings->PointsPerRing.DefaultValue = 8;
		Rings->ProjectionData.TraceMode = EEnvQueryTrace::Navigation;

		UEnvQueryTest_Pathfinding* Reachable = NewObject<UEnvQueryTest_Pathfinding>(&Query);
		Reachable->TestMode = EEnvTestPathfinding::PathExist;
		Reachable->TestPurpose = EEnvTestPurpose::Filter;
		Reachable->FilterType = EEnvTestFilterType::Match;
		Reachable->BoolValue.DefaultValue = true;
		Bind(*Reachable, Reachable->BoolValue, ReachableParam, TEXT("BoolValue"));

		UEnvQueryTest_Distance* Farthest = NewObject<UEnvQueryTest_Distance>(&Query);
		Farthest->TestPurpose = EEnvTestPurpose::Score;

		UEnvQueryOption* Option = NewObject<UEnvQueryOption>(&Query);
		Option->Generator = Rings;
		Option->Tests.Add(Reachable);
		Option->Tests.Add(Farthest);
		Query.GetOptionsMutable().Reset();
		Query.GetOptionsMutable().Add(Option);
	}
}
