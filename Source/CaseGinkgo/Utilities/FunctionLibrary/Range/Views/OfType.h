// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/Casts.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"
#include "FilterView.h"
#include "TransformView.h"

/**
 * UE-specific casting operators on pointer-shaped views (UObject*, TObjectPtr<U>, derived types).
 *
 * OfType<T>() — for each upstream element, run Cast<T>(element) and emit only the non-null
 *               results as T*. Conceptually equivalent to `Select(Cast<T>) | Where(non-null)`.
 * Cast<T>()   — project each element via Cast<T>(element), preserving null results. Useful when
 *               you want to preserve positional alignment with the source sequence.
 *
 * Requires T to derive from UObject. These live outside the core pure-template library: this
 * header depends on CoreUObject (already a dependency of this module), but the core headers do not.
 */
namespace UE::Query
{
	template <typename T>
		requires std::is_base_of_v<UObject, T>
	struct TOfTypeClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			auto CastProj = [](const UpValue& Obj) -> T* { return ::Cast<T>(Obj); };
			auto NonNull = [](T* Ptr) { return Ptr != nullptr; };
			return std::forward<TRange>(R)
				| Select(MoveTemp(CastProj))
				| Where(MoveTemp(NonNull));
		}
	};

	/** Keep only elements that successfully Cast<T>; emit them as T*. */
	template <typename T>
	auto OfType() { return TOfTypeClosure<T>{}; }

	template <typename T>
		requires std::is_base_of_v<UObject, T>
	struct TCastClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			auto CastProj = [](const UpValue& Obj) -> T* { return ::Cast<T>(Obj); };
			return std::forward<TRange>(R) | Select(MoveTemp(CastProj));
		}
	};

	/** Project each element via Cast<T>; nulls are preserved in the output. */
	template <typename T>
	auto Cast() { return TCastClosure<T>{}; }
}
