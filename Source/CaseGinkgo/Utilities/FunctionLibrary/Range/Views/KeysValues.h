// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"
#include "TransformView.h"

/**
 * Keys()   — project a view of TPair<K,V> elements to their keys.
 * Values() — project to their values.
 *
 * Both are thin wrappers around Select with a dedicated functor, so the return is a regular
 * TTransformView (same semantics, Num() preserved when the upstream is sized). Works on any
 * view whose ValueType is TPair<K,V> — not just TMapSourceView.
 */
namespace UE::Query
{
	struct FPairKeyProjector
	{
		template <typename Pair>
		auto operator()(const Pair& P) const -> decltype(P.Key)
		{
			return P.Key;
		}
	};

	struct FPairValueProjector
	{
		template <typename Pair>
		auto operator()(const Pair& P) const -> decltype(P.Value)
		{
			return P.Value;
		}
	};

	/** Project TPair elements to their keys. */
	inline auto Keys() { return Select(FPairKeyProjector{}); }

	/** Project TPair elements to their values. */
	inline auto Values() { return Select(FPairValueProjector{}); }
}
