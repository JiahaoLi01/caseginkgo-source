// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * MapMerge(OtherMap) — lazy full merge for pair-valued views. Right-biased on key collisions.
 *
 *   MapA | MapMerge(MapB) | ToMap();
 *
 * Emission follows two phases, all lazy:
 *   Phase 1 — iterate the LEFT (upstream) pair-by-pair:
 *               - if K is in the right map, emit (K, V_right) and record K as "already emitted"
 *               - else emit (K, V_left) unchanged
 *   Phase 2 — iterate the RIGHT map; emit only keys NOT already emitted in phase 1.
 *
 *   MapA = {1:a, 2:b, 3:c}
 *   MapB = {2:B, 4:D}
 *   MapA | MapMerge(MapB) | ToMap()  == {1:a, 2:B, 3:c, 4:D}
 *
 * Contrast with MapCover, which keeps left's key set unchanged and drops right-only keys.
 *
 * Cost & sizing:
 *   - O(Left.Num()) extra memory for a TSet<K> tracking keys emitted during phase 1.
 *   - Upstream ValueType must be TPair<K, V>. The override map's value type must be convertible to V.
 *   - NOT a CSizedView: the emission count depends on key overlap, which we cannot report without
 *     scanning both sides. Downstream Reserve() falls back to growth-based allocation. If you
 *     really need a size hint and accept an upper bound, compose with a sized fallback.
 *
 * Ownership: mirrors MapCover / Concat — lvalue maps are copied into the closure, rvalues moved.
 * The view holds a non-owning const* into the closure's stored copy.
 */
namespace UE::Query
{
	template <typename TUp, typename TRightMap>
		requires CQueryView<TUp>
	class TMapMergeView
	{
		TUp Up;
		const TRightMap* Right = nullptr;

		using UpValue = typename std::decay_t<TUp>::ValueType;

		static_assert(Private::TIsTPair<UpValue>::value,
			"MapMerge: upstream ValueType must be TPair<K,V>.");

		using PairK = std::decay_t<decltype(std::declval<const UpValue&>().Key)>;
		using PairV = std::decay_t<decltype(std::declval<const UpValue&>().Value)>;

		// Keys emitted during phase 1 via the right-override branch. These must be skipped in phase 2.
		TSet<PairK> EmittedRightKeys;

		using RightIteratorType = decltype(std::declval<const TRightMap&>().CreateConstIterator());
		TOptional<RightIteratorType> RightIter;
		bool bInPhase2 = false;

		// Cached holds the "current ready" output element; bPrimed says whether it is up-to-date.
		TOptional<UpValue> Cached;
		bool bPrimed = false;

		void PrimeIfNeeded()
		{
			if (bPrimed)
			{
				return;
			}

			// Phase 1: drain upstream, overriding from the right map on key hits.
			if (!bInPhase2)
			{
				if (Up.HasNext())
				{
					const UpValue& Incoming = Up.Next();
					if (const auto* FoundValue = Right->Find(Incoming.Key))
					{
						EmittedRightKeys.Add(Incoming.Key);
						Cached.Emplace(Incoming.Key, static_cast<PairV>(*FoundValue));
					}
					else
					{
						Cached.Emplace(Incoming);
					}
					bPrimed = true;
					return;
				}
				// Phase 1 done — transition to phase 2.
				bInPhase2 = true;
				RightIter.Emplace(Right->CreateConstIterator());
			}

			// Phase 2: emit right-only keys (those not already seen in phase 1).
			while (RightIter.IsSet() && (bool)(*RightIter))
			{
				const auto& RP = **RightIter;
				++(*RightIter);
				if (!EmittedRightKeys.Contains(RP.Key))
				{
					Cached.Emplace(RP.Key, static_cast<PairV>(RP.Value));
					bPrimed = true;
					return;
				}
			}

			// Fully exhausted — clear Cached so HasNext returns false.
			Cached.Reset();
			bPrimed = true;
		}

	public:
		using ValueType = UpValue;

		TMapMergeView(TUp InUp, const TRightMap& InRight)
			: Up(MoveTemp(InUp))
			, Right(&InRight)
		{
		}

		bool HasNext()
		{
			PrimeIfNeeded();
			return Cached.IsSet();
		}

		const ValueType& Next()
		{
			PrimeIfNeeded();
			check(Cached.IsSet());
			// Mark as needing re-prime on next call; the returned reference is valid until then.
			bPrimed = false;
			return *Cached;
		}
	};

	template <typename TRightMap>
	struct TMapMergeClosure : FQueryClosureBase
	{
		TRightMap Right;

		explicit TMapMergeClosure(TRightMap InMap) : Right(MoveTemp(InMap)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const &
		{
			return TMapMergeView<std::decay_t<TRange>, TRightMap>(
				std::forward<TRange>(R), Right);
		}
	};

	/**
	 * Build a MapMerge closure. Right-biased union.
	 *
	 *   TMap<int32, FString> A = {{1, "a"}, {2, "b"}, {3, "c"}};
	 *   TMap<int32, FString> B = {{2, "B"}, {4, "D"}};
	 *   TMap<int32, FString> R = A | MapMerge(B) | ToMap();
	 *   // R == {{1, "a"}, {2, "B"}, {3, "c"}, {4, "D"}}
	 */
	template <typename TRightMap>
	auto MapMerge(TRightMap&& Right)
	{
		using Stored = std::decay_t<TRightMap>;
		return TMapMergeClosure<Stored>(Stored(std::forward<TRightMap>(Right)));
	}
}
