// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * Last()            — drain the view, return the last element by value. check() on empty.
 * LastOrDefault()   — same but returns TOptional<T>; unset on empty.
 * Single()          — assert exactly one element; returns it by value.
 * SingleOrDefault() — zero elements => unset; one element => set; more than one => check().
 *
 * All four terminals copy into a value (never a reference) — the source may be an rvalue or a
 * view-over-temporary, so holding a reference past drain would dangle.
 */
namespace UE::Query
{
	struct FLastClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using T = typename std::decay_t<TRange>::ValueType;
			checkf(R.HasNext(), TEXT("UE::Query::Last called on empty pipeline; use LastOrDefault for safe access."));
			T Last(R.Next());
			while (R.HasNext())
			{
				Last = R.Next();
			}
			return Last;
		}
	};

	struct FLastOrDefaultClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using T = typename std::decay_t<TRange>::ValueType;
			if (!R.HasNext())
			{
				return TOptional<T>();
			}
			T Last(R.Next());
			while (R.HasNext())
			{
				Last = R.Next();
			}
			return TOptional<T>(MoveTemp(Last));
		}
	};

	struct FSingleClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using T = typename std::decay_t<TRange>::ValueType;
			checkf(R.HasNext(), TEXT("UE::Query::Single called on empty pipeline; expected exactly one element."));
			T Result(R.Next());
			checkf(!R.HasNext(), TEXT("UE::Query::Single called on pipeline with more than one element."));
			return Result;
		}
	};

	struct FSingleOrDefaultClosure : FQueryClosureBase
	{
		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			using T = typename std::decay_t<TRange>::ValueType;
			if (!R.HasNext())
			{
				return TOptional<T>();
			}
			T Result(R.Next());
			checkf(!R.HasNext(), TEXT("UE::Query::SingleOrDefault called on pipeline with more than one element."));
			return TOptional<T>(MoveTemp(Result));
		}
	};

	/** Last element as a value. Aborts on empty. */
	inline auto Last() { return FLastClosure{}; }

	/** Last element as TOptional<T>; unset when empty. */
	inline auto LastOrDefault() { return FLastOrDefaultClosure{}; }

	/** Exactly-one assertion. Aborts on empty or >1. */
	inline auto Single() { return FSingleClosure{}; }

	/** Zero-or-one assertion. Returns unset on empty; aborts on >1. */
	inline auto SingleOrDefault() { return FSingleOrDefaultClosure{}; }
}
