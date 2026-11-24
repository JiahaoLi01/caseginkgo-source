// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Algo/Sort.h"
#include "Algo/Reverse.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"
#include "../Core/SourceView.h"

/**
 * Actions are the in-between category: they consume the upstream view eagerly (materializing it
 * into a buffer), transform the buffer, and re-expose it as a new source view. The result is a
 * CQueryView again — so the pipeline can continue with more operators after OrderBy/Reverse.
 *
 * Cost: a single allocation proportional to the stream length. Unlike terminals, the result is
 * still lazy from this point on (the returned TOwnedArraySourceView drip-feeds on Next()).
 */
namespace UE::Query
{
	namespace Private
	{
		template <typename TRange>
		auto Materialize(TRange&& R)
		{
			using T = typename std::decay_t<TRange>::ValueType;
			TArray<T> Buffer;
			if constexpr (CSizedView<std::decay_t<TRange>>)
			{
				Buffer.Reserve(R.Num());
			}
			while (R.HasNext())
			{
				Buffer.Add(R.Next());
			}
			return Buffer;
		}
	}

	template <typename TProj>
	struct TOrderByClosure : FQueryClosureBase
	{
		TProj Proj;

		explicit TOrderByClosure(TProj InProj) : Proj(MoveTemp(InProj)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using T = typename std::decay_t<TRange>::ValueType;
			TArray<T> Buffer = Private::Materialize(std::forward<TRange>(R));
			Algo::SortBy(Buffer, Proj);
			return TOwnedArraySourceView<T>(MoveTemp(Buffer));
		}
	};

	/** Materialize, sort ascending by Proj(element), then expose as a source view for further chaining. */
	template <typename TProj>
	auto OrderBy(TProj Proj)
	{
		return TOrderByClosure<std::decay_t<TProj>>(MoveTemp(Proj));
	}

	template <typename TProj>
	struct TOrderByDescendingClosure : FQueryClosureBase
	{
		TProj Proj;

		explicit TOrderByDescendingClosure(TProj InProj) : Proj(MoveTemp(InProj)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using T = typename std::decay_t<TRange>::ValueType;
			TArray<T> Buffer = Private::Materialize(std::forward<TRange>(R));
			Algo::SortBy(Buffer, Proj, TGreater<>());
			return TOwnedArraySourceView<T>(MoveTemp(Buffer));
		}
	};

	/** Materialize, sort descending by Proj(element), then re-expose as a source view. */
	template <typename TProj>
	auto OrderByDescending(TProj Proj)
	{
		return TOrderByDescendingClosure<std::decay_t<TProj>>(MoveTemp(Proj));
	}

	struct FReverseClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using T = typename std::decay_t<TRange>::ValueType;
			TArray<T> Buffer = Private::Materialize(std::forward<TRange>(R));
			Algo::Reverse(Buffer);
			return TOwnedArraySourceView<T>(MoveTemp(Buffer));
		}
	};

	/** Materialize, reverse, then re-expose as a source view. */
	inline auto Reverse() { return FReverseClosure{}; }
}
