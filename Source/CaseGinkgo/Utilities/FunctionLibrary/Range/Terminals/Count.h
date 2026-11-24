// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * Count(): count remaining elements. Short-circuits to Num() if the upstream view is sized and
 * no lazy filters intervene — the chain between the source and Count must be all-sized for Num()
 * to propagate. Otherwise we drive the view to completion.
 */
namespace UE::Query
{
	struct FCountClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		int32 operator()(TRange&& R) const
		{
			if constexpr (CSizedView<std::decay_t<TRange>>)
			{
				return R.Num();
			}
			else
			{
				int32 N = 0;
				while (R.HasNext())
				{
					R.Next();
					++N;
				}
				return N;
			}
		}
	};

	/** Return the number of elements the pipeline would emit. */
	inline auto Count() { return FCountClosure{}; }
}
