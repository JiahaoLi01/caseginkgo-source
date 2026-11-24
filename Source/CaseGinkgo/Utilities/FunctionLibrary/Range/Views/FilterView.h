// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * Where(Pred): lazy predicate filter. Keeps upstream elements satisfying Pred.
 *
 * Reference semantics: the emitted reference is borrowed from upstream — no copy, no allocation.
 * Lazy-prime state machine: HasNext()/Next() prime the next element on demand. The returned
 * reference from Next() is invalidated on the next HasNext()/Next() call (standard view contract).
 *
 * Lambda caveat: if a TWhereClosure travels between functions (e.g. returned from a builder),
 * the predicate must NOT capture local references — captures by value or globals only.
 */
namespace UE::Query
{
	template <typename TUp, typename TPred>
		requires CQueryView<TUp>
	class TFilterView
	{
		TUp Up;
		TPred Pred;

		using UpValue = typename std::decay_t<TUp>::ValueType;
		const UpValue* Current = nullptr;
		bool bPrimed = false;

		void PrimeIfNeeded()
		{
			if (bPrimed)
			{
				return;
			}
			while (Up.HasNext())
			{
				const UpValue& V = Up.Next();
				if (Pred(V))
				{
					Current = &V;
					bPrimed = true;
					return;
				}
			}
			Current = nullptr;
			bPrimed = true;
		}

	public:
		using ValueType = UpValue;

		TFilterView(TUp InUp, TPred InPred)
			: Up(MoveTemp(InUp))
			, Pred(MoveTemp(InPred))
		{
		}

		bool HasNext()
		{
			PrimeIfNeeded();
			return Current != nullptr;
		}

		const ValueType& Next()
		{
			PrimeIfNeeded();
			check(Current);
			const ValueType& R = *Current;
			Current = nullptr;
			bPrimed = false;
			return R;
		}
	};

	template <typename TPred>
	struct TWhereClosure : FQueryClosureBase
	{
		TPred Pred;

		explicit TWhereClosure(TPred InPred) : Pred(MoveTemp(InPred)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const &
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_r_v<bool, const TPred&, const UpValue&>,
				"Where: predicate must be invocable with a single const-reference argument of ValueType and return bool.");
			return TFilterView<std::decay_t<TRange>, TPred>(std::forward<TRange>(R), Pred);
		}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) &&
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_r_v<bool, TPred&, const UpValue&>,
				"Where: predicate must be invocable with a single const-reference argument of ValueType and return bool.");
			return TFilterView<std::decay_t<TRange>, TPred>(std::forward<TRange>(R), MoveTemp(Pred));
		}
	};

	/** Build a Where closure. `container | Where([](const T& x){ return ...; })`. */
	template <typename TPred>
	auto Where(TPred Pred)
	{
		return TWhereClosure<std::decay_t<TPred>>(MoveTemp(Pred));
	}
}
