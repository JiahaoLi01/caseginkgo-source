// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * ToSet() — drain the view into a TSet<ValueType>, deduplicating as elements are added.
 *           Reserves up front when the upstream view reports Num().
 */
namespace UE::Query
{
	struct FToSetClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using T = typename std::decay_t<TRange>::ValueType;
			TSet<T> Result;
			if constexpr (CSizedView<std::decay_t<TRange>>)
			{
				Result.Reserve(R.Num());
			}
			while (R.HasNext())
			{
				Result.Add(R.Next());
			}
			return Result;
		}
	};

	/** Materialize the pipeline into TSet<ValueType>. */
	inline auto ToSet() { return FToSetClosure{}; }
}
