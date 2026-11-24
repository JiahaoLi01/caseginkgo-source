// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * Fold(Seed, Func)    — left-fold with explicit seed. Func signature: (Seed, const Element&) -> NewSeed.
 *                       The running accumulator is moved on each step so large seeds (e.g. TArray)
 *                       don't pay a per-element copy.
 * Aggregate(Func)     — seedless left-fold; uses the first element as the initial accumulator.
 *                       Returns TOptional<ValueType> — unset on empty pipelines.
 */
namespace UE::Query
{
	template <typename TSeed, typename TFunc>
	struct TFoldClosure : FQueryClosureBase
	{
		TSeed Seed;
		TFunc Func;

		TFoldClosure(TSeed InSeed, TFunc InFunc)
			: Seed(MoveTemp(InSeed))
			, Func(MoveTemp(InFunc))
		{
		}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const &
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_v<const TFunc&, TSeed, const UpValue&>,
				"Fold: function must be invocable as (Seed, const ValueType&) and return a new Seed.");
			TSeed Acc = Seed;
			while (R.HasNext())
			{
				Acc = Func(MoveTemp(Acc), R.Next());
			}
			return Acc;
		}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) &&
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_v<TFunc&, TSeed, const UpValue&>,
				"Fold: function must be invocable as (Seed, const ValueType&) and return a new Seed.");
			TSeed Acc = MoveTemp(Seed);
			while (R.HasNext())
			{
				Acc = Func(MoveTemp(Acc), R.Next());
			}
			return Acc;
		}
	};

	/** Left-fold with an explicit seed: `Nums | Fold(0, [](int32 A, int32 N){ return A + N; })`. */
	template <typename TSeed, typename TFunc>
	auto Fold(TSeed Seed, TFunc Func)
	{
		return TFoldClosure<std::decay_t<TSeed>, std::decay_t<TFunc>>(MoveTemp(Seed), MoveTemp(Func));
	}

	template <typename TFunc>
	struct TAggregateClosure : FQueryClosureBase
	{
		TFunc Func;

		explicit TAggregateClosure(TFunc InFunc) : Func(MoveTemp(InFunc)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const -> TOptional<typename std::decay_t<TRange>::ValueType>
		{
			using T = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_v<const TFunc&, T, const T&>,
				"Aggregate: function must be invocable as (ValueType, const ValueType&) and return ValueType.");
			if (!R.HasNext())
			{
				return TOptional<T>();
			}
			T Acc(R.Next());
			while (R.HasNext())
			{
				Acc = Func(MoveTemp(Acc), R.Next());
			}
			return TOptional<T>(MoveTemp(Acc));
		}
	};

	/** Seedless fold: uses the first element as the initial accumulator. Returns unset on empty. */
	template <typename TFunc>
	auto Aggregate(TFunc Func)
	{
		return TAggregateClosure<std::decay_t<TFunc>>(MoveTemp(Func));
	}
}
