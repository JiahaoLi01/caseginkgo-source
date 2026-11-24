// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"
#include "../Core/SourceView.h"

/**
 * SelectMany(Func) — lazy flattening projection. For each upstream element, Func returns a
 *                    container (by value or by const-ref), which is adapted into a source view
 *                    and drained before advancing to the next upstream element.
 * Flatten()        — convenience: SelectMany with identity projection, for views whose elements
 *                    are themselves containers (e.g. TArray<TArray<T>>).
 *
 * Lifetime: when Func returns by value, the inner container is moved into an owning source view.
 * When Func returns by const-reference, the inner view is non-owning — callers must ensure the
 * referenced storage stays valid for the duration of each inner drain.
 */
namespace UE::Query
{
	template <typename TUp, typename TFunc>
		requires CQueryView<TUp>
	class TSelectManyView
	{
		TUp Up;
		TFunc Func;

		using UpValue = typename std::decay_t<TUp>::ValueType;
		using InnerResult = std::invoke_result_t<TFunc&, const UpValue&>;
		using InnerViewType = decltype(MakeSourceView(std::declval<InnerResult>()));

	public:
		using ValueType = typename std::decay_t<InnerViewType>::ValueType;

	private:
		TOptional<InnerViewType> Inner;

		void AdvanceToReady()
		{
			while (!Inner.IsSet() || !Inner->HasNext())
			{
				if (!Up.HasNext())
				{
					Inner.Reset();
					return;
				}
				Inner.Emplace(MakeSourceView(Func(Up.Next())));
			}
		}

	public:
		TSelectManyView(TUp InUp, TFunc InFunc)
			: Up(MoveTemp(InUp))
			, Func(MoveTemp(InFunc))
		{
		}

		bool HasNext()
		{
			AdvanceToReady();
			return Inner.IsSet() && Inner->HasNext();
		}

		const ValueType& Next()
		{
			AdvanceToReady();
			check(Inner.IsSet() && Inner->HasNext());
			return Inner->Next();
		}
	};

	template <typename TFunc>
	struct TSelectManyClosure : FQueryClosureBase
	{
		TFunc Func;

		explicit TSelectManyClosure(TFunc InFunc) : Func(MoveTemp(InFunc)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const &
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_v<const TFunc&, const UpValue&>,
				"SelectMany: function must be invocable with a const-reference argument of ValueType.");
			return TSelectManyView<std::decay_t<TRange>, TFunc>(std::forward<TRange>(R), Func);
		}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) &&
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_v<TFunc&, const UpValue&>,
				"SelectMany: function must be invocable with a const-reference argument of ValueType.");
			return TSelectManyView<std::decay_t<TRange>, TFunc>(std::forward<TRange>(R), MoveTemp(Func));
		}
	};

	/** Project each element to a container, then flatten. `Units | SelectMany([](const FUnit& U){ return U.Attacks; })`. */
	template <typename TFunc>
	auto SelectMany(TFunc Func)
	{
		return TSelectManyClosure<std::decay_t<TFunc>>(MoveTemp(Func));
	}

	/** Identity projector used by Flatten to pass inner containers through unchanged. */
	struct FFlattenIdentity
	{
		template <typename C>
		const std::decay_t<C>& operator()(const C& Container) const
		{
			return Container;
		}
	};

	/** Flatten a view of containers: `ArrayOfArrays | Flatten() | ToArray()`. */
	inline auto Flatten() { return SelectMany(FFlattenIdentity{}); }
}
