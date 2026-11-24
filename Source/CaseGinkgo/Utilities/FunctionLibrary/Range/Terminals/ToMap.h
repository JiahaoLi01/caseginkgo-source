// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * ToMap()                              — requires upstream ValueType to be TPair<K,V>.
 *                                        Drain into TMap<K,V>; duplicate keys overwrite.
 * ToMap(KeySelector, ValueSelector)    — project each element to (Key, Value) and collect.
 */
namespace UE::Query
{
	// TIsTPair now lives in Core/Concepts.h (shared with MapCover).

	struct FToMapClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using T = typename std::decay_t<TRange>::ValueType;
			static_assert(Private::TIsTPair<T>::value,
				"ToMap(): upstream ValueType must be TPair<K,V>. Use ToMap(KeySelector, ValueSelector) to project.");
			// TPair is a type alias for TTuple<K,V>; its Key/Value are fields, not typedefs,
			// so we deduce the key/value types via decltype on the fields.
			using K = std::decay_t<decltype(std::declval<const T&>().Key)>;
			using V = std::decay_t<decltype(std::declval<const T&>().Value)>;
			TMap<K, V> Result;
			if constexpr (CSizedView<std::decay_t<TRange>>)
			{
				Result.Reserve(R.Num());
			}
			while (R.HasNext())
			{
				const T& Pair = R.Next();
				Result.Add(Pair.Key, Pair.Value);
			}
			return Result;
		}
	};

	/** Materialize into TMap<K,V> when upstream ValueType is TPair<K,V>. */
	inline auto ToMap() { return FToMapClosure{}; }

	template <typename TKeyFn, typename TValueFn>
	struct TToMapProjectedClosure : FQueryClosureBase
	{
		TKeyFn KeyFn;
		TValueFn ValueFn;

		TToMapProjectedClosure(TKeyFn InKey, TValueFn InValue)
			: KeyFn(MoveTemp(InKey))
			, ValueFn(MoveTemp(InValue))
		{
		}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using T = typename std::decay_t<TRange>::ValueType;
			using K = std::decay_t<std::invoke_result_t<const TKeyFn&, const T&>>;
			using V = std::decay_t<std::invoke_result_t<const TValueFn&, const T&>>;
			TMap<K, V> Result;
			if constexpr (CSizedView<std::decay_t<TRange>>)
			{
				Result.Reserve(R.Num());
			}
			while (R.HasNext())
			{
				const T& Element = R.Next();
				Result.Add(KeyFn(Element), ValueFn(Element));
			}
			return Result;
		}
	};

	/** Materialize into TMap<K,V> using explicit key/value projectors. */
	template <typename TKeyFn, typename TValueFn>
	auto ToMap(TKeyFn KeyFn, TValueFn ValueFn)
	{
		return TToMapProjectedClosure<std::decay_t<TKeyFn>, std::decay_t<TValueFn>>(
			MoveTemp(KeyFn), MoveTemp(ValueFn));
	}
}
