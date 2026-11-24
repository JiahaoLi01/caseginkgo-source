// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * First()          — drain the very first emitted element and return it by value (copy/move).
 *                    Aborts via check() when the pipeline is empty.
 * FirstOrDefault() — return TOptional<T>: set when an element exists, unset otherwise.
 *
 * Design note on return-by-value: pipelines often terminate the source storage (rvalue arrays,
 * OrderBy buffers). Returning by value dodges dangling-reference hazards that would arise from
 * returning const T& — callers who want to avoid a copy can always `const T& x = view.Next();`
 * without a terminal. The task spec suggested `const T&` for First(); we diverge deliberately
 * for safety and document it in the README.
 */
namespace UE::Query
{
	struct FFirstClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using T = typename std::decay_t<TRange>::ValueType;
			checkf(R.HasNext(), TEXT("UE::Query::First called on empty pipeline; use FirstOrDefault for safe access."));
			return T(R.Next());
		}
	};

	struct FFirstOrDefaultClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using T = typename std::decay_t<TRange>::ValueType;
			if (R.HasNext())
			{
				return TOptional<T>(R.Next());
			}
			return TOptional<T>();
		}
	};

	/** First element as a value. Aborts on empty pipelines. */
	inline auto First() { return FFirstClosure{}; }

	/** First element as TOptional<T>; unset when empty. */
	inline auto FirstOrDefault() { return FFirstOrDefaultClosure{}; }
}
