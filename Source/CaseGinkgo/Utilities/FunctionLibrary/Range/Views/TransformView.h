// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * Select(Func): lazy projection. ValueType is deduced via std::invoke_result_t<Func, const Up&>.
 *
 * Because the function produces new values (not references into upstream storage), TransformView
 * caches the most-recently-produced value in a TOptional<ValueType> and returns a reference to it.
 * The reference is invalidated on the next Next() call (standard view contract).
 *
 * Num() is forwarded when the upstream view is sized — projection is 1:1, so count is preserved.
 */
namespace UE::Query
{
	template <typename TUp, typename TFunc>
		requires CQueryView<TUp>
	class TTransformView
	{
		TUp Up;
		TFunc Func;

		using UpValue = typename std::decay_t<TUp>::ValueType;

	public:
		using ValueType = std::decay_t<std::invoke_result_t<TFunc&, const UpValue&>>;

	private:
		TOptional<ValueType> Cached;

	public:
		TTransformView(TUp InUp, TFunc InFunc)
			: Up(MoveTemp(InUp))
			, Func(MoveTemp(InFunc))
		{
		}

		bool HasNext() { return Up.HasNext(); }

		const ValueType& Next()
		{
			Cached.Emplace(Func(Up.Next()));
			return *Cached;
		}

		int32 Num() const requires CSizedView<TUp>
		{
			return Up.Num();
		}
	};

	template <typename TFunc>
	struct TSelectClosure : FQueryClosureBase
	{
		TFunc Func;

		explicit TSelectClosure(TFunc InFunc) : Func(MoveTemp(InFunc)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const &
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_v<const TFunc&, const UpValue&>,
				"Select: function must be invocable with a single const-reference argument of ValueType.");
			return TTransformView<std::decay_t<TRange>, TFunc>(std::forward<TRange>(R), Func);
		}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) &&
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_v<TFunc&, const UpValue&>,
				"Select: function must be invocable with a single const-reference argument of ValueType.");
			return TTransformView<std::decay_t<TRange>, TFunc>(std::forward<TRange>(R), MoveTemp(Func));
		}
	};

	/** Build a Select closure. `container | Select([](const T& x){ return ...; })`. */
	template <typename TFunc>
	auto Select(TFunc Func)
	{
		return TSelectClosure<std::decay_t<TFunc>>(MoveTemp(Func));
	}
}
