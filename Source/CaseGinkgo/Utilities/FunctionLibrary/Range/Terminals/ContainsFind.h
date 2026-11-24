// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * Contains(Value)  — true if any element compares equal to Value (operator==). Short-circuits.
 * ContainsBy(Pred) — true if any element satisfies Pred. (Behaviorally equivalent to Any(Pred);
 *                    provided for API symmetry with Find/Contains naming.)
 * Find(Pred)       — TOptional<T> of the first matching element. Returned by value so it survives
 *                    the pipeline going out of scope.
 */
namespace UE::Query
{
	template <typename TValue>
	struct TContainsClosure : FQueryClosureBase
	{
		TValue Value;

		explicit TContainsClosure(TValue InValue) : Value(MoveTemp(InValue)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		bool operator()(TRange&& R) const
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(requires(const UpValue& V, const TValue& X) { { V == X } -> std::convertible_to<bool>; },
				"Contains: ValueType must be equality-comparable with the supplied value.");
			while (R.HasNext())
			{
				if (R.Next() == Value)
				{
					return true;
				}
			}
			return false;
		}
	};

	/** true if any element equals Value. */
	template <typename TValue>
	auto Contains(TValue Value)
	{
		return TContainsClosure<std::decay_t<TValue>>(MoveTemp(Value));
	}

	template <typename TPred>
	struct TContainsByClosure : FQueryClosureBase
	{
		TPred Pred;

		explicit TContainsByClosure(TPred InPred) : Pred(MoveTemp(InPred)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		bool operator()(TRange&& R) const
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_r_v<bool, const TPred&, const UpValue&>,
				"ContainsBy: predicate must be invocable with a const-reference argument of ValueType and return bool.");
			while (R.HasNext())
			{
				if (Pred(R.Next()))
				{
					return true;
				}
			}
			return false;
		}
	};

	/** true if any element satisfies Pred. */
	template <typename TPred>
	auto ContainsBy(TPred Pred)
	{
		return TContainsByClosure<std::decay_t<TPred>>(MoveTemp(Pred));
	}

	template <typename TPred>
	struct TFindClosure : FQueryClosureBase
	{
		TPred Pred;

		explicit TFindClosure(TPred InPred) : Pred(MoveTemp(InPred)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const -> TOptional<typename std::decay_t<TRange>::ValueType>
		{
			using T = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_r_v<bool, const TPred&, const T&>,
				"Find: predicate must be invocable with a const-reference argument of ValueType and return bool.");
			while (R.HasNext())
			{
				const T& V = R.Next();
				if (Pred(V))
				{
					return TOptional<T>(V);
				}
			}
			return TOptional<T>();
		}
	};

	/** First element satisfying Pred, as TOptional<T>; unset if none match. */
	template <typename TPred>
	auto Find(TPred Pred)
	{
		return TFindClosure<std::decay_t<TPred>>(MoveTemp(Pred));
	}
}
