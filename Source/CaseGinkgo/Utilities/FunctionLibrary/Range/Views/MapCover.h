// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * MapCover(OverrideMap) — lazy left-biased override merge for pair-valued views.
 *
 *   MapA | MapCover(MapB) | ToMap();
 *
 * Iterates the LEFT side (upstream) pair-by-pair. For each (K, V_left):
 *   - if K is present in the right-side OverrideMap, emit (K, V_right);
 *   - otherwise emit (K, V_left) unchanged.
 *
 * Keys present only in the right-side map are NOT introduced — the emitted key-set equals the
 * upstream's key-set. Use Concat + ToMap (or a union-style operator) if you want the right-only
 * keys to surface too.
 *
 * Upstream ValueType must be TPair<K, V>. The override map's value type must be convertible to V.
 * The operator preserves Num() from the upstream view (emission is 1:1).
 *
 * Ownership: rvalue TMap is moved into the closure (owning copy). Lvalue TMap is copied into the
 * closure too — so the closure is self-sufficient and the view holds a pointer to the closure's
 * stored copy. As with every view, complete the pipeline while the closure is still alive.
 *
 * Naming note: this is the `MapCover` operator referenced in the project spec (the "cover" reading
 * matches the semantics: the right map covers/overrides matching keys of the left map).
 */
namespace UE::Query
{
	template <typename TUp, typename TOverrideMap>
		requires CQueryView<TUp>
	class TMapCoverView
	{
		TUp Up;
		const TOverrideMap* Override = nullptr;

		using UpValue = typename std::decay_t<TUp>::ValueType;

		static_assert(Private::TIsTPair<UpValue>::value,
			"MapCover: upstream ValueType must be TPair<K,V>.");

		using PairK = std::decay_t<decltype(std::declval<const UpValue&>().Key)>;
		using PairV = std::decay_t<decltype(std::declval<const UpValue&>().Value)>;

		TOptional<UpValue> Cached;

	public:
		using ValueType = UpValue;

		TMapCoverView(TUp InUp, const TOverrideMap& InOverride)
			: Up(MoveTemp(InUp))
			, Override(&InOverride)
		{
		}

		bool HasNext() { return Up.HasNext(); }

		const ValueType& Next()
		{
			const UpValue& Incoming = Up.Next();
			if (const auto* FoundValue = Override->Find(Incoming.Key))
			{
				Cached.Emplace(Incoming.Key, static_cast<PairV>(*FoundValue));
			}
			else
			{
				Cached.Emplace(Incoming);
			}
			return *Cached;
		}

		int32 Num() const requires CSizedView<TUp> { return Up.Num(); }
	};

	template <typename TOverrideMap>
	struct TMapCoverClosure : FQueryClosureBase
	{
		TOverrideMap Override;

		explicit TMapCoverClosure(TOverrideMap InMap) : Override(MoveTemp(InMap)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const &
		{
			return TMapCoverView<std::decay_t<TRange>, TOverrideMap>(
				std::forward<TRange>(R), Override);
		}
	};

	/**
	 * Build a MapCover closure. Example:
	 *
	 *   TMap<int32, FString> Base    = {{1, "a"}, {2, "b"}, {3, "c"}};
	 *   TMap<int32, FString> Patch   = {{2, "B"}, {4, "D"}};
	 *   TMap<int32, FString> Result  = Base | MapCover(Patch) | ToMap();
	 *   // Result == {{1, "a"}, {2, "B"}, {3, "c"}}   // key 4 is dropped (not in left)
	 */
	template <typename TOverrideMap>
	auto MapCover(TOverrideMap&& Override)
	{
		using Stored = std::decay_t<TOverrideMap>;
		return TMapCoverClosure<Stored>(Stored(std::forward<TOverrideMap>(Override)));
	}
}
