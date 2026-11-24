// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * ToArray(): the canonical terminal — drain the view into a TArray<ValueType> and return it.
 * Reserves up front when upstream reports Num(), otherwise grows naturally.
 */
namespace UE::Query
{
	struct FToArrayClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using T = typename std::decay_t<TRange>::ValueType;
			TArray<T> Result;
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

	/** Materialize the pipeline into TArray<ValueType>. Infix usage: `... | ToArray()`. */
	inline auto ToArray() { return FToArrayClosure{}; }
}
