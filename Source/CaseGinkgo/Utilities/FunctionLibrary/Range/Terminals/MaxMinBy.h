// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * MaxBy(Proj) / MinBy(Proj) — return the ELEMENT (not the key) whose Proj(element) is max/min.
 *                             The projected key type must support operator< (MinBy) or operator>
 *                             (MaxBy).
 *
 * Returned as TOptional<ValueType> — unset on empty pipelines. The winning element is copied out
 * (views can't retain a reference across drain) and the cached key is updated alongside to avoid
 * re-projecting the incumbent on every comparison.
 */
namespace UE::Query
{
	template <typename TProj>
	struct TMaxByClosure : FQueryClosureBase
	{
		TProj Proj;

		explicit TMaxByClosure(TProj InProj) : Proj(MoveTemp(InProj)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const -> TOptional<typename std::decay_t<TRange>::ValueType>
		{
			using T = typename std::decay_t<TRange>::ValueType;
			if (!R.HasNext())
			{
				return TOptional<T>();
			}
			T BestVal(R.Next());
			auto BestKey = Proj(BestVal);
			while (R.HasNext())
			{
				const T& V = R.Next();
				auto Key = Proj(V);
				if (Key > BestKey)
				{
					BestVal = V;
					BestKey = MoveTemp(Key);
				}
			}
			return TOptional<T>(MoveTemp(BestVal));
		}
	};

	/** Return the element with the largest Proj(element); unset on empty. */
	template <typename TProj>
	auto MaxBy(TProj Proj)
	{
		return TMaxByClosure<std::decay_t<TProj>>(MoveTemp(Proj));
	}

	template <typename TProj>
	struct TMinByClosure : FQueryClosureBase
	{
		TProj Proj;

		explicit TMinByClosure(TProj InProj) : Proj(MoveTemp(InProj)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const -> TOptional<typename std::decay_t<TRange>::ValueType>
		{
			using T = typename std::decay_t<TRange>::ValueType;
			if (!R.HasNext())
			{
				return TOptional<T>();
			}
			T BestVal(R.Next());
			auto BestKey = Proj(BestVal);
			while (R.HasNext())
			{
				const T& V = R.Next();
				auto Key = Proj(V);
				if (Key < BestKey)
				{
					BestVal = V;
					BestKey = MoveTemp(Key);
				}
			}
			return TOptional<T>(MoveTemp(BestVal));
		}
	};

	/** Return the element with the smallest Proj(element); unset on empty. */
	template <typename TProj>
	auto MinBy(TProj Proj)
	{
		return TMinByClosure<std::decay_t<TProj>>(MoveTemp(Proj));
	}
}
