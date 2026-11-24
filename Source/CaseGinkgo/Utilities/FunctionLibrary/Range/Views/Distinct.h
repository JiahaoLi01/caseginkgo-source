// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * Distinct()       — emit each element at most once. Requires ValueType to be hashable
 *                    (GetTypeHash defined; trivially true for built-in types, FName, FString,
 *                    UObject*, TObjectPtr, etc.).
 * DistinctBy(Proj) — uniqueness determined by Proj(element); the key type must be hashable.
 *
 * Cost: O(N) memory proportional to the number of distinct keys observed so far. First emission
 * per key is forwarded by reference from upstream — no element copy, just a key copy into a TSet.
 */
namespace UE::Query
{
	template <typename TUp>
		requires CQueryView<TUp>
	class TDistinctView
	{
		TUp Up;

		using UpValue = typename std::decay_t<TUp>::ValueType;
		TSet<UpValue> Seen;
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
				bool bAlreadyInSet = false;
				Seen.Add(V, &bAlreadyInSet);
				if (!bAlreadyInSet)
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

		explicit TDistinctView(TUp InUp) : Up(MoveTemp(InUp)) {}

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

	struct FDistinctClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			return TDistinctView<std::decay_t<TRange>>(std::forward<TRange>(R));
		}
	};

	/** Emit each element at most once (by ValueType). */
	inline auto Distinct() { return FDistinctClosure{}; }

	template <typename TUp, typename TProj>
		requires CQueryView<TUp>
	class TDistinctByView
	{
		TUp Up;
		TProj Proj;

		using UpValue = typename std::decay_t<TUp>::ValueType;
		using KeyType = std::decay_t<std::invoke_result_t<TProj&, const UpValue&>>;
		TSet<KeyType> SeenKeys;
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
				bool bAlreadyInSet = false;
				SeenKeys.Add(Proj(V), &bAlreadyInSet);
				if (!bAlreadyInSet)
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

		TDistinctByView(TUp InUp, TProj InProj)
			: Up(MoveTemp(InUp))
			, Proj(MoveTemp(InProj))
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

	template <typename TProj>
	struct TDistinctByClosure : FQueryClosureBase
	{
		TProj Proj;

		explicit TDistinctByClosure(TProj InProj) : Proj(MoveTemp(InProj)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const &
		{
			return TDistinctByView<std::decay_t<TRange>, TProj>(std::forward<TRange>(R), Proj);
		}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) &&
		{
			return TDistinctByView<std::decay_t<TRange>, TProj>(std::forward<TRange>(R), MoveTemp(Proj));
		}
	};

	/** Emit each element whose projected key is seen for the first time. */
	template <typename TProj>
	auto DistinctBy(TProj Proj)
	{
		return TDistinctByClosure<std::decay_t<TProj>>(MoveTemp(Proj));
	}
}
